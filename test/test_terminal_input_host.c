#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "include/reist/abi/syscall.h"

typedef struct {
    int pid, parent_pid;
    uint32_t generation, parent_generation;
    bool is_running, terminating, has_exited;
    struct { uint32_t kind; } domain_profile;
    char image_path[64];
} Process;
#define PROCESS_DOMAIN_COMPOSITOR 9U
typedef struct { unsigned held; } spinlock_t;
#define SPINLOCK_INIT {0}
#define KASSERT assert
#define KASSERT_IRQ_DISABLED() assert(!irq_on)
#define TERMINAL_INPUT_DEPTH 8U
static unsigned irq_on = 1, flushes, wakes;
static uint32_t irq_save(void) { unsigned old=irq_on; irq_on=0; return old; }
static void irq_restore(uint32_t old) { irq_on=old; }
static void spinlock_acquire(spinlock_t *lock) { assert(!irq_on && !lock->held); lock->held=1; }
static void spinlock_release(spinlock_t *lock) { assert(lock->held); lock->held=0; }
static uint32_t spinlock_acquire_irq(spinlock_t *lock) { uint32_t f=irq_save(); spinlock_acquire(lock); return f; }
static void spinlock_release_irq(spinlock_t *lock,uint32_t f) { spinlock_release(lock); irq_restore(f); }
static bool spinlock_is_owned_by_current(spinlock_t *lock) { return lock->held; }
static bool process_table_lock_is_owned(void) { return true; }
static uint32_t process_table_lock_irqsave(void) { return irq_save(); }
static void process_table_unlock_irqrestore(uint32_t f) { irq_restore(f); }
#define MAX_PROGRAMS 256U
static Process process_list[MAX_PROGRAMS];
static Process *current_reader, *release_on_wait;
static Process *scheduler_current_process(void) { return current_reader; }
#define KEYBOARD_DRAIN_BUDGET 16U
#define KEYBOARD_POLL_INTERVAL_MS 10U
#define TASK_BLOCK_WAITING 1
#define KASSERT_CAN_SLEEP() assert(irq_on)
static unsigned input_waiters, waits;
static void xhci_poll(void) {}
static void ohci_poll(void) {}
static void kb_drain_output_locked(unsigned n) { assert(n==16); }
static void kb_service_leds_locked(void) {}
static void kb_poll_controller(void) {}
static uint64_t pit_monotonic_ms(void) { return 100; }
static int wait_queue_block_until_spinlocked(unsigned *,int,uint64_t,spinlock_t *,uint32_t);
#define INPUT_QUEUE_SIZE 256U
static char input_queue[INPUT_QUEUE_SIZE];
static unsigned input_queue_head, input_queue_tail;
#define input_queue_lock terminal_input_lock
static void kb_input_owner_changed_locked(void) { input_queue_head=input_queue_tail=0; ++flushes; }
static void kb_input_owner_wake(void) { assert(!irq_on); ++wakes; }
static char input_queue_pop_locked(int pid,uint32_t generation);
typedef struct { int pid; uint32_t generation; } x86os_process_identity_t;
static int x86os_spawnv(const char *, int, const char *const *);
static int x86os_process_identity_of(int, x86os_process_identity_t *);
static int x86os_terminal_input(uint32_t, int, uint32_t);
static int x86os_wait(int, int *);
static int x86os_kill(int);
/* PRODUCTION */

static int control(Process *caller, Process *target, unsigned op) {
    reist_terminal_input_request_t r={REIST_TERMINAL_INPUT_VERSION,sizeof(r),op,0,
                                      target?target->pid:0,target?target->generation:0};
    return terminal_input_control_locked(caller,target,&r);
}
static char pop(Process *caller) {
    uint32_t f=spinlock_acquire_irq(&terminal_input_lock);
    char c=input_queue_pop_locked(caller?caller->pid:0,caller?caller->generation:0);
    spinlock_release_irq(&terminal_input_lock,f);
    return c;
}
static void push(char c) { input_queue[input_queue_tail++]=c; input_queue_tail%=INPUT_QUEUE_SIZE; }
static int wait_queue_block_until_spinlocked(unsigned *q,int kind,uint64_t deadline,spinlock_t *lock,uint32_t f) {
    assert(q==&input_waiters && kind==TASK_BLOCK_WAITING && deadline==110);
    assert(lock==&terminal_input_lock && !irq_on && lock->held);
    assert(++waits==1 && release_on_wait);
    spinlock_release_irq(lock,f);
    assert(control(release_on_wait,0,REIST_TERMINAL_RELEASE)==0);
    push('w');
    return 0;
}

/* Execute the actual GTEST caller against the actual mediator. Only the
 * process/SDK boundary is simulated; no replacement ownership model. */
static unsigned guest_case, guest_grants, guest_kills, guest_waits, guest_checks;
static int guest_grant_status;
static Process guest_parent;
static int x86os_spawnv(const char *path, int count, const char *const *args) {
    assert(strcmp(path,"/usr/gui/bin/desktop.prg")==0 && count==2);
    assert(strcmp(args[0],path)==0 && strcmp(args[1],"--unicode-probe")==0);
    if (guest_case==1) return -12;
    process_list[1]=(Process){.pid=41,.generation=123,.parent_pid=40,
        .parent_generation=50,.is_running=true};
    if (guest_case==4) --process_list[1].parent_generation;
    return 41;
}
static int x86os_process_identity_of(int pid, x86os_process_identity_t *identity) {
    assert(pid==41 && guest_waits==0 && guest_grants==0);
    if (guest_case==2 || guest_case==7) return -5;
    identity->pid=pid;
    identity->generation=process_list[1].generation-(guest_case==3 ? 1U : 0U);
    return 0;
}
static int x86os_terminal_input(uint32_t operation, int pid, uint32_t generation) {
    if (operation==REIST_TERMINAL_TRANSFER) {
        assert(++guest_grants==1 && guest_waits==0);
    } else {
        assert(operation==REIST_TERMINAL_CHECK && guest_waits==1);
        ++guest_checks;
    }
    reist_terminal_input_request_t request={1,sizeof(request),operation,0,pid,generation};
    int result=process_terminal_input(&guest_parent,&request);
    if (operation==REIST_TERMINAL_TRANSFER) guest_grant_status=result;
    return result;
}
static int x86os_kill(int pid) {
    assert(pid==41 && ++guest_kills==1 && guest_waits==0);
    assert(process_begin_exit(&process_list[1],process_list[1].generation));
    /* Exit may already be admitted when the caller attempts cancellation. */
    return guest_case==7 ? -1 : 0;
}
static int x86os_wait(int pid, int *status) {
    assert(pid==41 && ++guest_waits==1);
    if (guest_kills==0) {
        assert(guest_grants==1 && guest_grant_status==0);
        push('u');
        assert(pop(&guest_parent)==0 && pop(&process_list[1])=='u');
        push('v');
        assert(process_begin_exit(&process_list[1],process_list[1].generation));
    }
    process_list[1].is_running=false;
    *status=guest_case==5 ? 1 : 0;
    return guest_case==6 ? -5 : pid;
}
static void test_guest_handoff(void) {
    for (guest_case=0; guest_case<8; ++guest_case) {
        Process root={.pid=1,.generation=5,.image_path="/bin/shell.prg",.is_running=true};
        guest_parent=(Process){.pid=40,.generation=50,.parent_pid=1,
            .parent_generation=5,.is_running=true};
        guest_grants=guest_kills=guest_waits=guest_checks=0;
        guest_grant_status=-1;
        assert(control(&root,0,REIST_TERMINAL_ATTACH_CONSOLE)==0);
        assert(control(&root,&guest_parent,REIST_TERMINAL_TRANSFER)==0);
        assert(test_unicode_raster()==(guest_case==0 ? 0 : -1));
        assert(guest_waits==(guest_case==1 ? 0U : 1U));
        assert(guest_kills==((guest_case>=2 && guest_case<=4) || guest_case==7 ? 1U : 0U));
        if (guest_case==0) assert(guest_checks==1);
        assert(control(&guest_parent,0,REIST_TERMINAL_CHECK)==0);
        assert(pop(&guest_parent)==0); /* child bytes were flushed on exit */
        push('g'); assert(pop(&root)==0 && pop(&guest_parent)=='g');
        terminal_input_process_cleanup(guest_parent.pid,guest_parent.generation);
        push('s'); assert(pop(&root)=='s');
        terminal_input_process_cleanup(root.pid,root.generation);
    }
    puts("GUEST_UNICODE_TERMINAL_HANDOFF_OK cases=8");
}
int main(void) {
    Process shell={.pid=1,.generation=5,.image_path="/bin/shell.prg"};
    Process desktop={.pid=2,.generation=8,.parent_pid=1,.parent_generation=5};
    Process stranger={.pid=3,.generation=9};
    Process service={.pid=4,.generation=10,.domain_profile={PROCESS_DOMAIN_COMPOSITOR}};
    assert(control(&shell,0,REIST_TERMINAL_ATTACH_CONSOLE)==0);
    push('a'); assert(pop(&stranger)==0 && pop(0)==0 && pop(&shell)=='a');
    assert(control(&stranger,0,REIST_TERMINAL_ACQUIRE_SERVICE)==-13);
    push('b'); unsigned before=flushes;
    assert(control(&shell,&stranger,REIST_TERMINAL_TRANSFER)==-13);
    assert(flushes==before && pop(&shell)=='b');
    push('x'); assert(control(&shell,&desktop,REIST_TERMINAL_TRANSFER)==0);
    assert(pop(&desktop)==0); /* old console bytes were flushed */
    push('c'); assert(pop(&shell)==0 && pop(&stranger)==0 && pop(&desktop)=='c');
    current_reader=&shell;
    push('q'); assert(test_getchar_nonblocking()==0);
    current_reader=&desktop; assert(test_getchar_nonblocking()=='q');
    current_reader=&shell; release_on_wait=&desktop;
    push('z'); assert(test_getchar()=='w' && waits==1);
    assert(control(&shell,&desktop,REIST_TERMINAL_TRANSFER)==0);
    before=flushes;
    assert(control(&shell,&desktop,REIST_TERMINAL_TRANSFER)==0 && flushes==before);
    Process stale=desktop; --stale.generation;
    push('d'); terminal_input_process_cleanup(stale.pid,stale.generation);
    assert(pop(&stale)==0 && pop(&desktop)=='d');
    assert(control(&stranger,0,REIST_TERMINAL_RELEASE)==0); /* no authority to revoke others */
    push('e'); assert(pop(&desktop)=='e');
    reist_terminal_input_request_t malformed={99,sizeof(malformed),REIST_TERMINAL_RELEASE,0,0,0};
    before=flushes;
    assert(terminal_input_control_locked(&desktop,0,&malformed)==-22);
    malformed.version=REIST_TERMINAL_INPUT_VERSION; malformed.reserved=1;
    assert(terminal_input_control_locked(&desktop,0,&malformed)==-22);
    malformed.reserved=0; --malformed.struct_size;
    assert(terminal_input_control_locked(&desktop,0,&malformed)==-22);
    assert(flushes==before);
    push('f'); terminal_input_process_cleanup(desktop.pid,desktop.generation);
    assert(pop(&shell)==0 && pop(&desktop)==0);
    before=flushes; terminal_input_process_cleanup(desktop.pid,desktop.generation);
    assert(flushes==before);
    assert(control(&service,0,REIST_TERMINAL_ACQUIRE_SERVICE)==0);
    push('g'); assert(pop(&shell)==0 && pop(&service)=='g');
    assert(control(&service,0,REIST_TERMINAL_RELEASE)==0);
    assert(control(&service,0,REIST_TERMINAL_RELEASE)==0);
    push('h'); assert(pop(&shell)=='h');
    ++service.generation;
    assert(control(&service,0,REIST_TERMINAL_ACQUIRE_SERVICE)==0);
    terminal_input_process_cleanup(service.pid,service.generation-1);
    push('i'); assert(pop(&shell)==0 && pop(&service)=='i');
    terminal_input_process_cleanup(service.pid,service.generation);
    push('j'); assert(pop(&shell)=='j');
    Process children[8]; memset(children,0,sizeof(children));
    Process *parent=&shell;
    for (unsigned i=0;i<8;++i) {
        children[i].pid=20+(int)i; children[i].generation=100+i;
        children[i].parent_pid=parent->pid;
        children[i].parent_generation=parent->generation;
        before=flushes;
        assert(control(parent,&children[i],REIST_TERMINAL_TRANSFER)==(i<7?0:-28));
        if(i==7) assert(flushes==before);
        parent=&children[i];
    }
    push('v'); terminal_input_process_cleanup(children[0].pid,children[0].generation);
    assert(pop(&shell)==0 && pop(&children[6])==0);
    /* Production process-table admission, including death before teardown. */
    shell.is_running=true;
    process_list[0]=desktop; process_list[0].is_running=true;
    reist_terminal_input_request_t transfer={1,sizeof(transfer),REIST_TERMINAL_TRANSFER,0,desktop.pid,desktop.generation};
    process_list[0].terminating=true;
    assert(process_terminal_input(&shell,&transfer)==-116);
    process_list[0].terminating=false; --transfer.target_generation;
    assert(process_terminal_input(&shell,&transfer)==-116);
    ++transfer.target_generation;
    assert(process_terminal_input(&shell,&transfer)==0);
    push('r'); assert(!process_begin_exit(&process_list[0],desktop.generation-1));
    assert(pop(&desktop)=='r'); push('s');
    assert(process_begin_exit(&process_list[0],desktop.generation));
    assert(process_list[0].terminating && pop(&shell)==0 && pop(&desktop)==0);
    assert(!process_begin_exit(&process_list[0],desktop.generation));
    terminal_input_process_cleanup(shell.pid,shell.generation);
    push('k'); assert(pop(&shell)==0 && pop(0)=='k');
    assert(flushes==wakes);
    test_guest_handoff();
    puts("TERMINAL_INPUT_HOST_OK");
    return 0;
}
