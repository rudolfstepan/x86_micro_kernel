#include "x86os.h"

int main(void) {
    x86os_puts("REIST OS system information\n");
    x86os_puts("Architecture : i386\n");
    x86os_puts("Execution    : Ring 3 userspace\n");
    x86os_puts("ABI          : MYPR / INT 0x80\n");
    x86os_puts("Paging       : private address space\n");
    return 0;
}
