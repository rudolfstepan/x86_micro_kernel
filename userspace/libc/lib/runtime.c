#include <stdlib.h>
#include <x86os.h>
_Noreturn void reist_libc_fail(unsigned code) {
    if (code==REIST_LIBC_FAULT_HEAP) x86os_puts("REIST_LIBC_HEAP_FAULT\n");
    else x86os_puts("REIST_LIBC_ABORT\n");
    x86os_exit(code==REIST_LIBC_FAULT_HEAP ? 70 : 134);
}
_Noreturn void abort(void) { reist_libc_fail(2U); }
