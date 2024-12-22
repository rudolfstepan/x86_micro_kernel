extern "C" {
#include "../toolchain/stdio.h"
#include "../toolchain/stdlib.h"
#include "../drivers/video/framebuffer.h"
}

class DirCLI {
public:
    DirCLI() {
        printf("CTOR called\n");
        printf("Class DIR CLI running\n");
    }

    ~DirCLI() {
        printf("DTOR called\n");
    }

    void run() {
        printf("Running DIR CLI\n");
    }
};

int main() {
    DirCLI dirCLI;

    dirCLI.run();
    
    return 0;
}
