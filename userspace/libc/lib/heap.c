/* ISO C11 memory allocation over an explicitly owned, fixed Ring-3 arena.
 * Metadata is out-of-band, checked before mutation, and never found by reading
 * backwards from a caller's pointer. This is corruption detection, not memory
 * protection against code executing in the same process. */
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HEAP_SLOTS (REIST_LIBC_OBJECT_LIMIT * 2U + 1U)
#define HEAP_END UINT32_MAX
#define HEAP_ALIGN ((uint32_t)_Alignof(max_align_t))
typedef struct { uint32_t offset, span, requested, next, check; } heap_block_t;
static heap_block_t heap_blocks[HEAP_SLOTS];
static unsigned char *heap_base;
static uintptr_t heap_base_check;
static uint32_t heap_capacity, heap_capacity_check, heap_live, heap_bytes, heap_peak;
static int heap_errno;
int *reist_libc_errno(void) { return &heap_errno; }
static uint32_t block_check(const heap_block_t *b) {
    return ~(b->offset ^ b->span ^ b->requested ^ b->next ^ 0x52434831U);
}
static void seal(heap_block_t *b) { b->check=block_check(b); }
static _Noreturn void bad_heap(void) { reist_libc_fail(REIST_LIBC_FAULT_HEAP); }
/* A contiguous, nonoverlapping partition proves pointer ranges before use.
 * Positive spans plus exact total coverage also detect cycles in <=8193 steps. */
static void validate(void) {
    if (!heap_base) {
        if (heap_base_check || heap_capacity || heap_capacity_check ||
            heap_live || heap_bytes || heap_peak) bad_heap();
        return;
    }
    if (heap_base_check != ~(uintptr_t)heap_base ||
        heap_capacity_check != ~heap_capacity || !heap_capacity ||
        heap_capacity>REIST_LIBC_HEAP_LIMIT) bad_heap();
    uint32_t at=0, covered=0, objects=0, bytes=0, visited=0;
    while (at!=HEAP_END) {
        if (at>=HEAP_SLOTS || ++visited>HEAP_SLOTS) bad_heap();
        const heap_block_t *b=&heap_blocks[at];
        if (b->check!=block_check(b) || b->offset!=covered || !b->span ||
            b->span%HEAP_ALIGN || b->span>heap_capacity-covered ||
            b->requested>b->span) bad_heap();
        covered+=b->span;
        if (b->requested) { ++objects; bytes+=b->requested; }
        at=b->next;
    }
    if (covered!=heap_capacity || objects!=heap_live || bytes!=heap_bytes ||
        objects>REIST_LIBC_OBJECT_LIMIT || heap_peak<bytes ||
        heap_peak>heap_capacity) bad_heap();
}
int reist_libc_init(void *storage, size_t capacity) {
    validate();
    if (heap_base) return -EBUSY;
    if (!storage || (uintptr_t)storage%HEAP_ALIGN || capacity<HEAP_ALIGN ||
        capacity>REIST_LIBC_HEAP_LIMIT || capacity%HEAP_ALIGN ||
        (uintptr_t)storage>UINTPTR_MAX-capacity) return -EINVAL;
    memset(heap_blocks,0,sizeof(heap_blocks));
    heap_base=storage; heap_base_check=~(uintptr_t)storage;
    heap_capacity=(uint32_t)capacity; heap_capacity_check=~heap_capacity;
    heap_live=heap_bytes=heap_peak=0;
    heap_blocks[0]=(heap_block_t){0,heap_capacity,0,HEAP_END,0}; seal(&heap_blocks[0]);
    return 0;
}
int reist_libc_reset(void) {
    validate();
    if (heap_live) return -EBUSY;
    if (heap_base) memset(heap_base,0,heap_capacity);
    memset(heap_blocks,0,sizeof(heap_blocks));
    heap_base=NULL; heap_base_check=0;
    heap_capacity=heap_capacity_check=heap_live=heap_bytes=heap_peak=0;
    return 0;
}
int reist_libc_stats(reist_libc_stats_t *stats) {
    if (!stats || stats->version!=REIST_LIBC_VERSION ||
        stats->struct_size!=sizeof(*stats)) return -EINVAL;
    validate();
    *stats=(reist_libc_stats_t){REIST_LIBC_VERSION,sizeof(*stats),heap_capacity,
        heap_live,heap_bytes,heap_peak}; return 0;
}
void *malloc(size_t size) {
    validate();
    if (!size) return NULL;
    if (!heap_base || size>heap_capacity || heap_live>=REIST_LIBC_OBJECT_LIMIT) {
        errno=ENOMEM; return NULL;
    }
    uint32_t span=((uint32_t)size+HEAP_ALIGN-1U)/HEAP_ALIGN*HEAP_ALIGN;
    for (uint32_t at=0; at!=HEAP_END; at=heap_blocks[at].next) {
        heap_block_t *b=&heap_blocks[at];
        if (b->requested || b->span<span) continue;
        if (b->span>span) {
            uint32_t spare=0;
            while (spare<HEAP_SLOTS && heap_blocks[spare].span) ++spare;
            if (spare==HEAP_SLOTS) { errno=ENOMEM; return NULL; }
            heap_blocks[spare]=(heap_block_t){b->offset+span,b->span-span,0,b->next,0};
            seal(&heap_blocks[spare]); b->next=spare; b->span=span;
        }
        b->requested=(uint32_t)size; seal(b); ++heap_live; heap_bytes+=(uint32_t)size;
        if (heap_bytes>heap_peak) heap_peak=heap_bytes;
        return heap_base+b->offset;
    }
    errno=ENOMEM; return NULL;
}
void *calloc(size_t count, size_t size) {
    if (size && count>SIZE_MAX/size) { errno=ENOMEM; return NULL; }
    void *p=malloc(count*size); if (p) memset(p,0,count*size); return p;
}
static uint32_t locate(void *p, uint32_t *previous) {
    if (!heap_base || (uintptr_t)p<(uintptr_t)heap_base ||
        (uintptr_t)p-(uintptr_t)heap_base>=heap_capacity) bad_heap();
    uint32_t prev=HEAP_END;
    for (uint32_t at=0;at!=HEAP_END;at=heap_blocks[at].next) {
        if ((uintptr_t)p==(uintptr_t)heap_base+heap_blocks[at].offset &&
            heap_blocks[at].requested) { if (previous) *previous=prev; return at; }
        prev=at;
    }
    bad_heap(); return HEAP_END;
}
void free(void *p) {
    if (!p) return;
    validate(); uint32_t prev, at=locate(p,&prev); heap_block_t *b=&heap_blocks[at];
    memset(p,0,b->requested); heap_bytes-=b->requested; --heap_live; b->requested=0;
    if (b->next!=HEAP_END && !heap_blocks[b->next].requested) {
        heap_block_t *next=&heap_blocks[b->next]; b->span+=next->span; b->next=next->next;
        memset(next,0,sizeof(*next));
    }
    seal(b);
    if (prev!=HEAP_END && !heap_blocks[prev].requested) {
        heap_blocks[prev].span+=b->span; heap_blocks[prev].next=b->next;
        seal(&heap_blocks[prev]); memset(b,0,sizeof(*b));
    }
}
void *realloc(void *p, size_t size) {
    if (!p) return malloc(size);
    if (!size) { free(p); return NULL; }
    validate(); uint32_t at=locate(p,NULL); heap_block_t *b=&heap_blocks[at];
    uint32_t old=b->requested;
    if (size<=b->span) {
        heap_bytes=heap_bytes-old+(uint32_t)size; b->requested=(uint32_t)size; seal(b);
        if (heap_bytes>heap_peak) heap_peak=heap_bytes;
        return p;
    }
    void *replacement=malloc(size);
    if (!replacement) return NULL;
    memcpy(replacement,p,old); free(p); return replacement;
}
