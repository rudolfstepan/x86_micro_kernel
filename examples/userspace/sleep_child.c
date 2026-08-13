#include "x86os.h"

int main(void) {
    return x86os_sleep_ms(400) == 0 ? 41 : 77;
}
