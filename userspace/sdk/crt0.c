#include "x86os.h"

extern int main(void);

__attribute__((section(".text._start"), noreturn))
void _start(void) {
    int status = main();
    x86os_exit(status);
}
