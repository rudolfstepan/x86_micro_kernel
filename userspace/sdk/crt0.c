#include "x86os.h"

extern int main(int argc, char** argv);

__attribute__((section(".text._start"), noreturn))
void _start(int argc, char** argv) {
    int status = main(argc, argv);
    x86os_exit(status);
}
