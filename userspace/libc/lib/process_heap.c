#include <reist/libc.h>
#include <x86os.h>
#include <errno.h>
_Static_assert(sizeof(reist_libc_backing_t)==28U, "i386 backing v1 ABI changed");

static void *process_acquire(void *context, size_t capacity) {
    (void)context;
    return x86os_malloc(capacity);
}
static void process_release(void *context, void *storage, size_t capacity) {
    (void)context; (void)capacity;
    /* The public void free wrapper cannot report an ownership failure. Do not
     * silently lose backing after libc has retired its region metadata. */
    if (x86os_syscall(X86OS_SYS_FREE, (uintptr_t)storage, 0U, 0U) != 0U)
        reist_libc_fail(REIST_LIBC_FAULT_HEAP);
}
int reist_libc_init_process(size_t budget) {
    if (!budget || budget > REIST_LIBC_PROCESS_LIMIT || budget % 4096U)
        return -EINVAL;
    reist_libc_backing_t provider = {REIST_LIBC_BACKING_VERSION, sizeof(provider),
        (uint32_t)budget, 256U*1024U, NULL, process_acquire, process_release};
    return reist_libc_init_backing(&provider);
}
