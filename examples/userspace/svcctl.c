#include "x86os.h"

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int parse_component(const char *text, uint32_t *component) {
    if (text == 0 || component == 0 || *text == '\0') return -1;
    uint32_t value = 0U;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9' || value > 99U) return -1;
        value = value * 10U + (uint32_t)(*text - '0');
    }
    if (value >= X86OS_COMPONENT_COUNT) return -1;
    *component = value;
    return 0;
}

static void print_state(uint32_t state) {
    if (state == X86OS_COMPONENT_READY) x86os_puts("READY");
    else if (state == X86OS_COMPONENT_QUIESCING) x86os_puts("QUIESCING");
    else if (state == X86OS_COMPONENT_OFFLINE) x86os_puts("DOWN");
    else if (state == X86OS_COMPONENT_STARTING) x86os_puts("STARTING");
    else if (state == X86OS_COMPONENT_FAILED) x86os_puts("FAILED");
    else x86os_puts("UNKNOWN");
}

static int status(uint32_t component, int list_line) {
    x86os_component_request_t request = {0};
    x86os_component_result_t result;
    request.version = X86OS_COMPONENT_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.command = X86OS_COMPONENT_STATUS;
    request.component = component;
    int code = x86os_component_control(&request, &result);
    if (code != 0) return code;
    x86os_puts(list_line ? "COMPONENT RESOURCE " : "COMPONENT STATUS ");
    x86os_print_number((int)component);
    x86os_puts(" name=");
    x86os_puts(result.name);
    x86os_puts(" state=");
    print_state(result.state);
    x86os_puts(" generation=");
    x86os_print_number((int)result.generation);
    if ((result.flags & X86OS_COMPONENT_PROTECTED) != 0U)
        x86os_puts(" PROTECTED");
    if ((result.flags & X86OS_COMPONENT_FENCED) != 0U)
        x86os_puts(" FENCED");
    x86os_putchar('\n');
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && equal(argv[1], "list")) {
        for (uint32_t component = 0U; component < X86OS_COMPONENT_COUNT;
             ++component) {
            int code = status(component, 1);
            if (code != 0) {
                x86os_puts("COMPONENT LIST_FAILED code=");
                x86os_print_number(code);
                x86os_putchar('\n');
                return 1;
            }
        }
        return 0;
    }
    if (argc != 3 ||
        (!equal(argv[1], "status") && !equal(argv[1], "down") &&
         !equal(argv[1], "up") && !equal(argv[1], "restart"))) {
        x86os_puts("Usage: svcctl list\n");
        x86os_puts("       svcctl status|down|up|restart <component>\n");
        return 2;
    }
    uint32_t component = 0U;
    if (parse_component(argv[2], &component) != 0) return 2;
    if (equal(argv[1], "status")) {
        int code = status(component, 0);
        if (code == 0) return 0;
        x86os_puts("COMPONENT STATUS_FAILED code=");
        x86os_print_number(code);
        x86os_putchar('\n');
        return 1;
    }
    x86os_component_request_t request = {0};
    x86os_component_result_t result;
    request.version = X86OS_COMPONENT_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.component = component;
    request.timeout_ms = 2000U;
    request.command = equal(argv[1], "down") ? X86OS_COMPONENT_DOWN :
        equal(argv[1], "up") ? X86OS_COMPONENT_UP : X86OS_COMPONENT_RESTART;
    int code = x86os_component_control(&request, &result);
    if (code != 0) {
        if (code == -1003) x86os_puts("COMPONENT PROTECTED\n");
        else if (code == -1004) x86os_puts("COMPONENT DEPENDENCY_BLOCKED\n");
        else {
            x86os_puts("COMPONENT OPERATION_FAILED code=");
            x86os_print_number(code);
            x86os_putchar('\n');
        }
        return 1;
    }
    x86os_puts(request.command == X86OS_COMPONENT_DOWN
        ? "COMPONENT DOWN_OK component="
        : request.command == X86OS_COMPONENT_UP
            ? "COMPONENT UP_OK component="
            : "COMPONENT RESTART_OK component=");
    x86os_print_number((int)component);
    x86os_puts(" generation=");
    x86os_print_number((int)result.generation);
    x86os_putchar('\n');
    return 0;
}
