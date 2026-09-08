#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <setjmp.h>
#undef assert
#define assert(value) do { if (!(value)) { fprintf(stderr,"assertion line %d: %s\n",__LINE__,#value); _Exit(1); } } while (0)

#define PAGE_SIZE 4096U
#define PAGE_USER 4U
#define PAGE_RW 2U
#define PAGE_PRESENT 1U
#define PAGE_TABLE_ENTRIES 1024U
#define USER_PAGE_START 256U
#define USER_PAGE_END 768U
#define USER_HEAP_BASE 0x40800000U
#define USER_HEAP_TOP 0xBF000000U
#define MAX_USER_ALLOCATIONS 128
#define PROCESS_HEAP_MAX_BYTES (512U*1024U*1024U)
#define PROCESS_SCRIPT_HEAP_MAX_BYTES (64U*1024U*1024U)
#define PROCESS_DOMAIN_SCRIPT 10U
#define PROCESS_MEMORY_BATCH_PAGES 64U
typedef struct { uint32_t address, requested_size, mapped_size; bool allocated; } user_allocation_t;
typedef struct {
    int pid; uint32_t generation, heap_next, heap_bytes, heap_budget;
    user_allocation_t user_allocations[MAX_USER_ALLOCATIONS];
    struct { uint32_t kind; } domain_profile;
} Process;
typedef struct { uint64_t managed_bytes, free_frame_bytes; } memory_stats_t;
typedef struct { uint32_t entries[1024]; } page_directory_t;
static uint32_t page_directory[1024];
static Process processes[2], *owner;
static page_directory_t directories[2];
static uint8_t *frames;
static uint32_t frame_used[8192], map_va[8192], map_owner[8192];
static int if_enabled=1, preemption, yields, fail_after=-1, map_failure=-1;
static unsigned allocations, frees;
static jmp_buf abandoned_reaper;
static int abandon_on_yield;
static Process *scheduler_current_process(void) { return owner; }
static uint32_t irq_save(void) { uint32_t old=if_enabled; if_enabled=0; return old; }
static void irq_restore(uint32_t value) { if_enabled=(int)value; }
static int irq_enabled(void) { return if_enabled; }
static bool scheduler_can_sleep(void) { return if_enabled && !preemption; }
static void scheduler_preempt_disable(void) { ++preemption; }
static void scheduler_preempt_enable(void) { assert(preemption); --preemption; }
static int scheduler_yield(void) {
    assert(if_enabled && !preemption); ++yields;
    if (abandon_on_yield) { abandon_on_yield=0; longjmp(abandoned_reaper,1); }
    return 0;
}
static page_directory_t *paging_current_directory(void) { return &directories[owner->pid-1]; }
static void memory_get_stats(memory_stats_t *s) { s->managed_bytes=64U*1024U*1024U; s->free_frame_bytes=32U*1024U*1024U; }
static size_t allocate_frame(void) {
    if (fail_after==0) return 0;
    if (fail_after>0) --fail_after;
    for (unsigned i=0;i<8192;++i) if (!frame_used[i]) {
        frame_used[i]=1; ++allocations;
        memset(frames+i*PAGE_SIZE,0xa5,PAGE_SIZE);
        return (size_t)(frames+i*PAGE_SIZE);
    }
    return 0;
}
static size_t allocate_user_frame(void) { return allocate_frame(); }
static void free_frame(size_t address) {
    unsigned i=(unsigned)((uint8_t *)address-frames)/PAGE_SIZE;
    assert(i<8192 && frame_used[i]); frame_used[i]=0; ++frees;
}
static int map_page(page_directory_t *pd,uint32_t va,uint32_t frame,uint32_t flags) {
    assert(pd==paging_current_directory() && flags==(PAGE_USER|PAGE_RW));
    assert(if_enabled && preemption);
    for (unsigned j=0;j<PAGE_SIZE;++j) assert(!((uint8_t *)(uintptr_t)frame)[j]);
    if (map_failure==0) return -1;
    if (map_failure>0) --map_failure;
    unsigned i=(frame-(uint32_t)(uintptr_t)frames)/PAGE_SIZE;
    assert(i<8192 && frame_used[i] && !map_va[i]);
    map_va[i]=va; map_owner[i]=(unsigned)owner->pid; return 0;
}
static int unmap_page(page_directory_t *pd,uint32_t va,bool release) {
    assert(pd==paging_current_directory() && release);
    for (unsigned i=0;i<8192;++i) if (map_va[i]==va && map_owner[i]==(unsigned)owner->pid) {
        map_va[i]=map_owner[i]=0; free_frame((size_t)(frames+i*PAGE_SIZE)); return 0;
    }
    return -1;
}
static void paging_trim_user_tables(page_directory_t *pd,uint32_t va,uint32_t length) {
    (void)pd; (void)va; (void)length;
}
static unsigned frees_at_lock;
static uint32_t page_table_lock_acquire_irq(void) { frees_at_lock=frees; return irq_save(); }
static void page_table_lock_release_irq(uint32_t flags) {
    assert(frees-frees_at_lock<=66); irq_restore(flags);
}
static void free_page(void *p) { free_frame((size_t)p); }
#define TASK_FINISHED 4
#define TASK_REAPING 5
#define TASK_CPU_NONE -1
#define KASSERT assert
typedef struct {
    uint32_t *kernel_stack, *reap_kernel_stack;
    page_directory_t *page_directory, *reap_page_directory;
    Process *process;
    struct { void *queue; } wait_node;
    int status,running_cpu;
    uint32_t reap_page_cursor;
    uint32_t task_generation;
    bool reap_busy;
} task_t;
static task_t tasks[4];
static int num_tasks=3,current_task,task_table_lock;
static uint32_t process_table_lock_irqsave(void) { return irq_save(); }
static void process_table_unlock_irqrestore(uint32_t flags) { irq_restore(flags); }
static uint32_t task_table_lock_irqsave(void) { return irq_save(); }
static void task_table_unlock_irqrestore(uint32_t flags) { irq_restore(flags); }
static void spinlock_acquire(int *lock) { (void)lock;assert(!if_enabled); }
static void spinlock_release(int *lock) { (void)lock;assert(!if_enabled); }
static void scheduler_free_kernel_stack(uint32_t *p) { assert(preemption);free_page(p); }
static void release_task_resources(task_t *task) {
    assert(!if_enabled && task->running_cpu==TASK_CPU_NONE);
    task->reap_page_directory=task->page_directory; task->page_directory=NULL;
    task->reap_kernel_stack=task->kernel_stack; task->kernel_stack=NULL;
    task->process=NULL; task->status=TASK_REAPING;
}
/* PRODUCTION */
int main(void) {
    frames=VirtualAlloc((void *)0x10000000U,32U*1024U*1024U,
                        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(frames==(void *)0x10000000U);
    owner=&processes[0]; owner->pid=1; owner->generation=1;
    owner->heap_next=USER_HEAP_BASE;
    void *p[32];
    for (unsigned i=0;i<32;++i) { p[i]=process_user_malloc(PAGE_SIZE); assert(p[i]); }
    for (unsigned i=0;i<32;++i) assert(!process_user_free(p[i]));
    assert(allocations==frees);
    owner->domain_profile.kind=PROCESS_DOMAIN_SCRIPT;
    owner->heap_budget=0;
    void *restricted=process_user_malloc(PAGE_SIZE);
    assert(restricted && owner->heap_budget<=PROCESS_SCRIPT_HEAP_MAX_BYTES);
    assert(owner->heap_budget==32U*1024U*1024U); /* low-RAM adaptive bound survives */
    unsigned before=allocations;
    assert(!process_user_malloc(PROCESS_SCRIPT_HEAP_MAX_BYTES+PAGE_SIZE));
    assert(allocations==before && !process_user_free(restricted));
    owner->heap_budget=PROCESS_HEAP_MAX_BYTES;
    restricted=process_user_malloc(PAGE_SIZE);
    assert(restricted && owner->heap_budget==PROCESS_SCRIPT_HEAP_MAX_BYTES);
    assert(!process_user_free(restricted));
    owner->domain_profile.kind=0;
    owner->heap_budget=0;
    for (int fail=0;fail<6;++fail) {
        fail_after=fail;
        assert(!process_user_malloc(8U*PAGE_SIZE));
        assert(allocations==frees && owner->heap_bytes==0);
    }
    fail_after=-1;
    for (int fail=0;fail<6;++fail) {
        map_failure=fail;
        assert(!process_user_malloc(8U*PAGE_SIZE));
        assert(allocations==frees && owner->heap_bytes==0);
    }
    map_failure=-1;
    void *large=process_user_malloc(8U*1024U*1024U);
    assert(large && yields>0 && owner->heap_bytes==8U*1024U*1024U);
    assert(!process_user_malloc(SIZE_MAX));
    assert(!process_user_malloc(PROCESS_HEAP_MAX_BYTES));
    owner=&processes[1]; owner->pid=2; owner->generation=1; owner->heap_next=USER_HEAP_BASE;
    assert(process_user_free(large)<0);
    void *private=process_user_malloc(PAGE_SIZE); assert(private==large);
    assert(!process_user_free(private));
    owner=&processes[0];
    assert(!process_user_free(large));
    assert(process_user_free(large)<0);
    assert(allocations==frees && !owner->heap_bytes && !preemption && if_enabled);
    for (unsigned i=0;i<200;++i) {
        void *a=process_user_malloc(PAGE_SIZE), *b=process_user_malloc(2U*PAGE_SIZE);
        assert(a==(void *)USER_HEAP_BASE && b==(void *)(USER_HEAP_BASE+PAGE_SIZE));
        assert(!process_user_free(a));
        a=process_user_malloc(PAGE_SIZE); assert(a==(void *)USER_HEAP_BASE);
        assert(!process_user_free(a) && !process_user_free(b));
    }
    assert(allocations==frees);
    page_directory_t *pd=(page_directory_t *)allocate_frame();
    uint32_t *table=(uint32_t *)allocate_frame();
    void *shared=(void *)allocate_frame();
    memset(pd,0,PAGE_SIZE); memset(table,0,PAGE_SIZE);
    pd->entries[0]=(uint32_t)(uintptr_t)shared|PAGE_PRESENT;
    pd->entries[USER_PAGE_END]=(uint32_t)(uintptr_t)shared|PAGE_PRESENT;
    pd->entries[USER_PAGE_START]=(uint32_t)(uintptr_t)table|PAGE_PRESENT|PAGE_USER;
    for (unsigned i=0;i<193;++i) table[i]=(uint32_t)allocate_frame()|PAGE_PRESENT|PAGE_USER;
    tasks[1].page_directory=pd;
    tasks[1].kernel_stack=(uint32_t *)allocate_frame();
    tasks[1].status=TASK_FINISHED; tasks[1].running_cpu=TASK_CPU_NONE;
    tasks[1].task_generation=42;
    abandon_on_yield=1;
    if (!setjmp(abandoned_reaper)) { (void)scheduler_reap_finished_tasks(); assert(0); }
    assert(!preemption && if_enabled && tasks[1].status==TASK_REAPING && !tasks[1].reap_busy);
    assert(tasks[1].reap_page_directory==pd && tasks[1].reap_page_cursor>USER_PAGE_START*1024);
    current_task=2; /* a different caller resumes the abandoned collection */
    assert(scheduler_reap_finished_tasks()==1 && tasks[1].status==TASK_FINISHED);
    assert(allocations==frees+1); /* kernel/MMIO shared table remains owned */
    free_page(shared);
    assert(allocations==frees && !preemption && if_enabled);
    puts("PRIVATE_MEMORY_HOST_OK"); return 0;
}
