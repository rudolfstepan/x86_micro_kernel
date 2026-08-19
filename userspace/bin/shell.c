/**
 * @file userspace/bin/shell.c
 * @brief Interaktive Ring-3-Shell mit History und Programmdispatch.
 *
 * Layer: Ring-3 interactive user program.
 * Contract: Eingabe, Pfade und Rückgabewerte werden vor Dispatch oder Dateizugriff geprüft.
 * Safety: Puffer und History sind fest begrenzt; Fehler beenden nur das aktuelle Kommando.
 */
#include "x86os.h"

#define SHELL_LINE_CAPACITY 256
#define SHELL_PATH_CAPACITY 256
#define SHELL_MAX_ARGUMENTS 16
#define SHELL_MAX_PATH_ENTRIES 8
#define SHELL_HISTORY_CAPACITY 32

enum {
    SHELL_KEY_NONE = 0x100,
    SHELL_KEY_UP,
    SHELL_KEY_DOWN,
};

static char search_paths[SHELL_MAX_PATH_ENTRIES][SHELL_PATH_CAPACITY] = {
    "/bin", "/sbin", "/usr/bin", "/usr/gui/bin",
};
static unsigned search_path_count = 4U;
static char command_history[SHELL_HISTORY_CAPACITY][SHELL_LINE_CAPACITY];
static char history_draft[SHELL_LINE_CAPACITY];
static unsigned history_count;
static unsigned history_next;
static int history_cursor = -1;

static unsigned text_length(const char* text) {
    unsigned length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static char lower(char value) {
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static int text_equal(const char* left, const char* right) {
    while (*left != '\0' && lower(*left) == lower(*right)) {
        ++left;
        ++right;
    }
    return lower(*left) == lower(*right);
}

static uint32_t parse_ipv4(const char* text) {
    if (text == 0 || *text == '\0') return 0U;
    uint32_t value = 0U;
    unsigned part = 0U;
    unsigned digits = 0U;
    unsigned dots = 0U;
    for (const char* cursor = text;; ++cursor) {
        char current = *cursor;
        if (current >= '0' && current <= '9') {
            if (digits >= 3U) return 0U;
            part = part * 10U + (unsigned)(current - '0');
            if (part > 255U) return 0U;
            ++digits;
        } else if (current == '.' || current == '\0') {
            if (digits == 0U) return 0U;
            value = (value << 8U) | part;
            part = 0U;
            digits = 0U;
            if (current == '\0') break;
            if (++dots > 3U) return 0U;
        } else {
            return 0U;
        }
    }
    return dots == 3U ? value : 0U;
}

static void print_ipv4(uint32_t value) {
    x86os_print_number((int)((value >> 24U) & 0xFFU));
    x86os_putchar('.');
    x86os_print_number((int)((value >> 16U) & 0xFFU));
    x86os_putchar('.');
    x86os_print_number((int)((value >> 8U) & 0xFFU));
    x86os_putchar('.');
    x86os_print_number((int)(value & 0xFFU));
}

static int network_call(uint32_t operation, uint32_t ip, uint32_t netmask,
                        uint32_t gateway, uint32_t target, uint32_t id,
                        uint32_t sequence, uint32_t timeout,
                        x86os_network_control_result_t* result) {
    x86os_network_control_request_t request = {0};
    request.version = X86OS_NETWORK_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.operation = operation;
    request.ip_address = ip;
    request.netmask = netmask;
    request.gateway = gateway;
    request.target_ip = target;
    request.identifier = id;
    request.sequence = sequence;
    request.timeout_ms = timeout;
    return x86os_network_control(&request, result);
}

static void print_mac(const uint8_t mac[6]) {
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned index = 0U; index < 6U; ++index) {
        if (index != 0U) x86os_putchar(':');
        x86os_putchar(digits[(mac[index] >> 4U) & 0x0FU]);
        x86os_putchar(digits[mac[index] & 0x0FU]);
    }
}

static int network_status(int detailed) {
    x86os_network_control_result_t result = {0};
    int code = network_call(X86OS_NETWORK_STATUS, 0U, 0U, 0U, 0U, 0U,
                            0U, 0U, &result);
    if (code != 0) {
        x86os_puts("NET STATUS failed code=");
        x86os_print_number(code);
        x86os_putchar('\n');
        return code;
    }
    x86os_puts("Network: ");
    x86os_puts(result.available != 0U ? "available" : "not available");
    x86os_puts(" backend=");
    x86os_puts(result.backend);
    x86os_puts(" ready=");
    x86os_puts(result.ready != 0U ? "yes" : "no");
    x86os_puts(" configured=");
    x86os_puts(result.configured != 0U ? "yes\n" : "no\n");
    x86os_puts("MAC=");
    print_mac(result.mac_address);
    x86os_putchar('\n');
    if (detailed || result.configured != 0U) {
        x86os_puts("IP=");
        print_ipv4(result.ip_address);
        x86os_puts(" MASK=");
        print_ipv4(result.netmask);
        x86os_puts(" GW=");
        print_ipv4(result.gateway);
        x86os_putchar('\n');
    }
    return 0;
}

static void network_command(int argc, const char* const argv[]) {
    if (argc == 1 || text_equal(argv[1], "status") ||
        text_equal(argv[1], "info")) {
        if (argc > 2) {
            x86os_puts("Usage: net [status|info|dhcp]\n");
            return;
        }
        (void)network_status(argc > 1 && text_equal(argv[1], "info"));
        return;
    }
    if (text_equal(argv[1], "dhcp")) {
        x86os_puts("DHCP is supervised by REIST.PRG; current state:\n");
        (void)network_status(1);
        return;
    }
    x86os_puts("Usage: net [status|info|dhcp]\n");
}

static void ifconfig_command(int argc, const char* const argv[]) {
    if (argc == 1 || (argc == 2 && text_equal(argv[1], "dhcp"))) {
        (void)network_status(1);
        return;
    }
    if (argc != 4) {
        x86os_puts("Usage: ifconfig <ip> <netmask> <gateway>\n");
        return;
    }
    uint32_t ip = parse_ipv4(argv[1]);
    uint32_t netmask = parse_ipv4(argv[2]);
    uint32_t gateway = parse_ipv4(argv[3]);
    x86os_network_control_result_t result = {0};
    int code = (ip == 0U || netmask == 0U || gateway == 0U) ? -22 :
        network_call(X86OS_NETWORK_CONFIGURE, ip, netmask, gateway, 0U,
                     0U, 0U, 0U, &result);
    if (code != 0) {
        x86os_puts("IFCONFIG failed code=");
        x86os_print_number(code);
        x86os_putchar('\n');
        return;
    }
    x86os_puts("Network interface configured.\n");
}

static void ping_command(int argc, const char* const argv[]) {
    if (argc != 2) {
        x86os_puts("Usage: ping <ip>\n");
        return;
    }
    uint32_t target = parse_ipv4(argv[1]);
    if (target == 0U) {
        x86os_puts("PING: invalid IP address\n");
        return;
    }
    uint32_t sent = 0U;
    uint32_t received = 0U;
    uint32_t sequence = 1U;
    x86os_puts("PING ");
    x86os_puts(argv[1]);
    x86os_puts(" (Ctrl+C to stop)\n");
    for (;;) {
        x86os_network_control_result_t result = {0};
        int code = network_call(X86OS_NETWORK_PING, 0U, 0U, 0U, target,
                                0x1234U, sequence++, 2000U, &result);
        ++sent;
        x86os_puts("reply: ");
        if (code == 0) {
            ++received;
            x86os_puts("received\n");
        } else {
            x86os_puts("timeout/error code=");
            x86os_print_number(code);
            x86os_putchar('\n');
        }
        for (uint32_t elapsed = 0U; elapsed < 1000U; elapsed += 10U) {
            if (x86os_getchar_nonblocking() == 0x03) {
                x86os_puts("^C\n");
                x86os_puts("Packets sent=");
                x86os_print_number((int)sent);
                x86os_puts(" received=");
                x86os_print_number((int)received);
                x86os_putchar('\n');
                return;
            }
            (void)x86os_sleep_ms(10U);
        }
    }
}

static void arp_command(int argc, const char* const argv[]) {
    if (argc != 2 && !(argc == 3 && text_equal(argv[1], "scan"))) {
        x86os_puts("Usage: arp <ip>\n");
        x86os_puts("       arp scan <ip>\n");
        return;
    }
    const char* address = argc == 2 ? argv[1] : argv[2];
    uint32_t target = parse_ipv4(address);
    if (target == 0U) {
        x86os_puts("ARP: invalid IP address\n");
        return;
    }
    x86os_network_control_result_t result = {0};
    int code = -11;
    for (unsigned attempt = 0U; attempt < 5U && code == -11; ++attempt) {
        code = network_call(X86OS_NETWORK_ARP_REQUEST, 0U, 0U, 0U,
                            target, 0U, 0U, 0U, &result);
        if (code == -11 && attempt + 1U < 5U)
            (void)x86os_sleep_ms(250U);
    }
    if (code != 0) {
        x86os_puts("ARP request failed code=");
        x86os_print_number(code);
        x86os_putchar('\n');
        return;
    }
    x86os_puts("ARP request queued for ");
    x86os_puts(address);
    x86os_putchar('\n');
}

static char drive_letter(const x86os_drive_info_t* drive) {
    if (drive->mount_point[0] == '/' && drive->mount_point[1] == '\0')
        return 'C';
    if (drive->name[3] < '0' || drive->name[3] > '9' || drive->name[4] != '\0') {
        return 0;
    }
    if (drive->type == X86OS_DRIVE_FDD) return (char)('A' + drive->name[3] - '0');
    if (drive->type == X86OS_DRIVE_ATA || drive->type == X86OS_DRIVE_AHCI)
        return (char)('C' + drive->name[3] - '0');
    return 0;
}

static int find_drive(char letter, x86os_drive_info_t* result) {
    for (uint32_t index = 0;; ++index) {
        x86os_drive_info_t drive;
        int status = x86os_drive_info(index, &drive);
        if (status == 0) return -1;
        if (status < 0) continue;
        if (lower(drive_letter(&drive)) == lower(letter)) {
            *result = drive;
            return 0;
        }
    }
}

static int path_has_mount(const char* path, const char* mount) {
    unsigned length = text_length(mount);
    if (length == 1U && mount[0] == '/') return path[0] == '/';
    for (unsigned index = 0; index < length; ++index) {
        if (path[index] != mount[index]) return 0;
    }
    return path[length] == '\0' || path[length] == '/';
}

static int current_drive(const char* path, x86os_drive_info_t* result) {
    unsigned best_length = 0;
    int found = -1;
    for (uint32_t index = 0;; ++index) {
        x86os_drive_info_t drive;
        int status = x86os_drive_info(index, &drive);
        if (status == 0) break;
        if (status < 0 || !path_has_mount(path, drive.mount_point)) continue;
        unsigned length = text_length(drive.mount_point);
        if (found < 0 || length > best_length) {
            *result = drive;
            best_length = length;
            found = 0;
        }
    }
    return found;
}

static void print_dos_path(const char* path) {
    x86os_drive_info_t drive;
    if (current_drive(path, &drive) < 0) {
        x86os_puts(path);
        return;
    }
    char letter = drive_letter(&drive);
    if (letter != 0) x86os_putchar(letter);
    else x86os_puts(drive.name);
    x86os_puts(":\\");
    unsigned offset = text_length(drive.mount_point);
    if (offset == 1U) offset = 0;
    while (path[offset] == '/') ++offset;
    for (; path[offset] != '\0'; ++offset) {
        x86os_putchar(path[offset] == '/' ? '\\' : path[offset]);
    }
}

static int resolve_shell_path(const char* input,
                              char output[SHELL_PATH_CAPACITY]) {
    x86os_drive_info_t drive;
    const char* remainder = input;
    if (input[0] != '\0' && input[1] == ':') {
        if (find_drive(input[0], &drive) < 0) return -1;
        remainder = input + 2;
    } else {
        char cwd[SHELL_PATH_CAPACITY];
        if (x86os_getcwd(cwd, sizeof(cwd)) < 0 ||
            current_drive(cwd, &drive) < 0) return -1;
        if (input[0] != '\\') {
            unsigned length = text_length(input);
            if (length + 1U > SHELL_PATH_CAPACITY) return -1;
            for (unsigned index = 0; index <= length; ++index) {
                output[index] = input[index] == '\\' ? '/' : input[index];
            }
            return 0;
        }
    }

    unsigned mount_length = text_length(drive.mount_point);
    unsigned remainder_length = text_length(remainder);
    while (*remainder == '/' || *remainder == '\\') {
        ++remainder;
        --remainder_length;
    }
    unsigned separator = remainder_length != 0U && mount_length > 1U ? 1U : 0U;
    if (mount_length + separator + remainder_length + 1U > SHELL_PATH_CAPACITY) {
        return -1;
    }
    for (unsigned index = 0; index < mount_length; ++index) output[index] = drive.mount_point[index];
    unsigned position = mount_length;
    if (separator != 0U) output[position++] = '/';
    for (unsigned index = 0; index < remainder_length; ++index) {
        output[position++] = remainder[index] == '\\' ? '/' : remainder[index];
    }
    output[position] = '\0';
    return 0;
}

static int copy_text(char* output, unsigned capacity, const char* input) {
    unsigned length = text_length(input);
    if (length + 1U > capacity) return -1;
    for (unsigned index = 0; index <= length; ++index) output[index] = input[index];
    return 0;
}

static int text_same(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static unsigned history_index(unsigned ordinal) {
    unsigned oldest = (history_next + SHELL_HISTORY_CAPACITY - history_count) %
                      SHELL_HISTORY_CAPACITY;
    return (oldest + ordinal) % SHELL_HISTORY_CAPACITY;
}

static void history_reset_navigation(void) {
    history_cursor = -1;
    history_draft[0] = '\0';
}

static void history_add(const char* line) {
    if (line[0] == '\0') {
        history_reset_navigation();
        return;
    }
    if (history_count != 0U) {
        unsigned newest = history_index(history_count - 1U);
        if (text_same(command_history[newest], line)) {
            history_reset_navigation();
            return;
        }
    }
    if (copy_text(command_history[history_next], SHELL_LINE_CAPACITY, line) != 0)
        return;
    history_next = (history_next + 1U) % SHELL_HISTORY_CAPACITY;
    if (history_count < SHELL_HISTORY_CAPACITY) ++history_count;
    history_reset_navigation();
}

static const char* history_previous(const char* current_line) {
    if (history_count == 0U) return 0;
    if (history_cursor < 0) {
        if (copy_text(history_draft, sizeof(history_draft), current_line) != 0)
            return 0;
        history_cursor = (int)history_count - 1;
    } else if (history_cursor > 0) {
        --history_cursor;
    }
    return command_history[history_index((unsigned)history_cursor)];
}

static const char* history_next_line(void) {
    if (history_cursor < 0) return 0;
    if ((unsigned)(history_cursor + 1) < history_count) {
        ++history_cursor;
        return command_history[history_index((unsigned)history_cursor)];
    }
    history_cursor = -1;
    return history_draft;
}

static void history_show(void) {
    for (unsigned ordinal = 0U; ordinal < history_count; ++ordinal) {
        x86os_print_number((int)(ordinal + 1U));
        x86os_puts("  ");
        x86os_puts(command_history[history_index(ordinal)]);
        x86os_putchar('\n');
    }
}

static int join_program_path(const char* directory, const char* program,
                             char output[SHELL_PATH_CAPACITY]) {
    unsigned directory_length = text_length(directory);
    unsigned program_length = text_length(program);
    unsigned separator = directory_length > 1U &&
                         directory[directory_length - 1U] != '/' ? 1U : 0U;
    if (directory_length + separator + program_length + 1U >
        SHELL_PATH_CAPACITY) return -1;
    for (unsigned index = 0; index < directory_length; ++index) {
        output[index] = directory[index];
    }
    unsigned position = directory_length;
    if (separator != 0U) output[position++] = '/';
    for (unsigned index = 0; index <= program_length; ++index) {
        output[position + index] = program[index];
    }
    return 0;
}

static int executable_file(const char* path) {
    x86os_file_info_t info;
    return x86os_stat(path, &info) == 0 && info.type == X86OS_FILE;
}

static int explicit_program_path(const char* path) {
    if (path[1] == ':') return 1;
    for (; *path != '\0'; ++path) {
        if (*path == '/' || *path == '\\') return 1;
    }
    return 0;
}

static void show_search_path(void) {
    x86os_puts("PATH=");
    for (unsigned index = 0; index < search_path_count; ++index) {
        if (index != 0U) x86os_putchar(';');
        print_dos_path(search_paths[index]);
    }
    x86os_putchar('\n');
}

static void set_search_path(const char* value) {
    char component[SHELL_PATH_CAPACITY];
    unsigned component_length = 0;
    unsigned count = 0;
    for (;;) {
        char current = *value++;
        if (current == ';' || current == '\0') {
            if (component_length != 0U && count < SHELL_MAX_PATH_ENTRIES) {
                component[component_length] = '\0';
                int result;
                if (component[1] == ':' || component[0] == '/' ||
                    component[0] == '\\') {
                    result = resolve_shell_path(component, search_paths[count]);
                } else {
                    char cwd[SHELL_PATH_CAPACITY];
                    result = x86os_getcwd(cwd, sizeof(cwd));
                    if (result == 0) {
                        result = join_program_path(cwd, component,
                                                   search_paths[count]);
                    }
                }
                if (result == 0) ++count;
            }
            component_length = 0;
            if (current == '\0') break;
        } else if (component_length + 1U < sizeof(component)) {
            component[component_length++] = current;
        }
    }
    search_path_count = count;
}

static int has_program_extension(const char* name) {
    unsigned length = text_length(name);
    return length >= 4U && name[length - 4U] == '.' &&
           lower(name[length - 3U]) == 'p' &&
           lower(name[length - 2U]) == 'r' &&
           lower(name[length - 1U]) == 'g';
}

typedef struct {
    char common[SHELL_PATH_CAPACITY];
    unsigned length;
    unsigned count;
    int directory;
} completion_t;

static int text_starts_with(const char* text, const char* prefix) {
    while (*prefix != '\0') {
        if (*text == '\0') return 0;
        if (lower(*text++) != lower(*prefix++)) return 0;
    }
    return 1;
}

static void consider_completion(completion_t* completion, const char* name,
                                unsigned length, int directory) {
    if (length + 1U > sizeof(completion->common)) return;
    if (completion->count == 0U) {
        for (unsigned index = 0; index < length; ++index) {
            completion->common[index] = name[index];
        }
        completion->common[length] = '\0';
        completion->length = length;
        completion->directory = directory;
    } else {
        unsigned common = 0;
        while (common < completion->length && common < length &&
               lower(completion->common[common]) == lower(name[common])) {
            ++common;
        }
        completion->length = common;
        completion->common[common] = '\0';
        completion->directory = 0;
    }
    ++completion->count;
}

static void scan_completion_directory(const char* directory,
                                      const char* prefix,
                                      int programs_only,
                                      completion_t* completion) {
    for (uint32_t index = 0;;) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(directory, index, entries);
        if (count <= 0) break;
        for (int entry_index = 0; entry_index < count; ++entry_index) {
            x86os_file_info_t* entry = &entries[entry_index];
            unsigned length = text_length(entry->name);
            if (programs_only) {
                if (entry->type == X86OS_FILE) {
                    if (!has_program_extension(entry->name)) continue;
                    length -= 4U;
                } else if (entry->type != X86OS_DIRECTORY) {
                    continue;
                }
            }
            if (text_starts_with(entry->name, prefix)) {
                consider_completion(completion, entry->name, length,
                                    entry->type == X86OS_DIRECTORY);
            }
        }
        index += (uint32_t)count;
    }
}

static void complete_command(const char* prefix, completion_t* completion) {
    static const char* commands[] = {
        "CD", "CHDIR", "PWD", "HELP", "HISTORY", "PATH", "EXIT",
        "DIR", "TYPE", "MD", "RD", "ERASE", "CLEAR", "NET",
        "IFCONFIG", "PING", "ARP", "GETIP"
    };
    for (unsigned index = 0; index < sizeof(commands) / sizeof(commands[0]);
         ++index) {
        if (text_starts_with(commands[index], prefix)) {
            consider_completion(completion, commands[index],
                                text_length(commands[index]), 0);
        }
    }

    char cwd[SHELL_PATH_CAPACITY];
    int have_cwd = x86os_getcwd(cwd, sizeof(cwd)) == 0;
    scan_completion_directory(have_cwd ? cwd : ".", prefix, 1, completion);
    for (unsigned index = 0; index < search_path_count; ++index) {
        /* PATH commonly starts with the current directory. Avoid counting
         * the same program twice, which would suppress unique completion. */
        if (have_cwd && text_equal(cwd, search_paths[index])) continue;
        scan_completion_directory(search_paths[index], prefix, 1, completion);
    }
}

static void complete_line(char line[SHELL_LINE_CAPACITY], unsigned* length) {
    unsigned token_start = *length;
    while (token_start != 0U && line[token_start - 1U] != ' ' &&
           line[token_start - 1U] != '\t') --token_start;

    unsigned name_start = token_start;
    for (unsigned index = token_start; index < *length; ++index) {
        if (line[index] == '/' || line[index] == '\\' ||
            (index == token_start + 1U && line[index] == ':')) {
            name_start = index + 1U;
        }
    }

    char prefix[SHELL_PATH_CAPACITY];
    unsigned prefix_length = *length - name_start;
    if (prefix_length + 1U > sizeof(prefix)) return;
    for (unsigned index = 0; index < prefix_length; ++index) {
        prefix[index] = line[name_start + index];
    }
    prefix[prefix_length] = '\0';

    completion_t completion;
    completion.common[0] = '\0';
    completion.length = 0;
    completion.count = 0;
    completion.directory = 0;
    int command = token_start == 0U;
    if (command && name_start == token_start) {
        complete_command(prefix, &completion);
    } else {
        char directory[SHELL_PATH_CAPACITY];
        char resolved[SHELL_PATH_CAPACITY];
        unsigned directory_length = name_start - token_start;
        if (directory_length == 0U) {
            directory[0] = '.';
            directory[1] = '\0';
        } else {
            for (unsigned index = 0; index < directory_length; ++index) {
                directory[index] = line[token_start + index];
            }
            directory[directory_length] = '\0';
        }
        if (resolve_shell_path(directory, resolved) < 0) return;
        scan_completion_directory(resolved, prefix, command, &completion);
    }

    if (completion.count == 0U || completion.length < prefix_length) return;
    for (unsigned index = prefix_length; index < completion.length; ++index) {
        if (*length + 1U >= SHELL_LINE_CAPACITY) return;
        line[(*length)++] = completion.common[index];
        x86os_putchar(completion.common[index]);
    }
    if (completion.count == 1U && *length + 1U < SHELL_LINE_CAPACITY) {
        char suffix = completion.directory ? '\\' : ' ';
        line[(*length)++] = suffix;
        x86os_putchar(suffix);
    }
    line[*length] = '\0';
}

static int read_shell_input(void) {
    for (;;) {
        int value = x86os_getchar_nonblocking();
        if (value != 0) return value;
        (void)x86os_sleep_ms(10U);
    }
}

static int read_shell_key(void) {
    int value = read_shell_input();
    if (value != 0x1B) return value;
    if (read_shell_input() != '[') return SHELL_KEY_NONE;
    value = read_shell_input();
    if (value == 'A') return SHELL_KEY_UP;
    if (value == 'B') return SHELL_KEY_DOWN;
    if (value >= '0' && value <= '9') {
        (void)read_shell_input();
    }
    return SHELL_KEY_NONE;
}

static void replace_input_line(char line[SHELL_LINE_CAPACITY],
                               unsigned* length, const char* replacement) {
    unsigned replacement_length = text_length(replacement);
    if (replacement_length >= SHELL_LINE_CAPACITY) return;
    for (unsigned index = 0U; index < *length; ++index) x86os_putchar('\b');
    for (unsigned index = 0U; index < *length; ++index) x86os_putchar(' ');
    for (unsigned index = 0U; index < *length; ++index) x86os_putchar('\b');
    for (unsigned index = 0U; index < replacement_length; ++index) {
        line[index] = replacement[index];
        x86os_putchar(replacement[index]);
    }
    line[replacement_length] = '\0';
    *length = replacement_length;
}

static void read_line(char line[SHELL_LINE_CAPACITY]) {
    unsigned length = 0;
    line[0] = '\0';
    history_reset_navigation();
    for (;;) {
        int key = read_shell_key();
        if (key == SHELL_KEY_UP) {
            const char* previous = history_previous(line);
            if (previous != 0) replace_input_line(line, &length, previous);
            continue;
        }
        if (key == SHELL_KEY_DOWN) {
            const char* next = history_next_line();
            if (next != 0) replace_input_line(line, &length, next);
            continue;
        }
        if (key == SHELL_KEY_NONE) continue;
        char value = (char)key;
        if (value == 0x03) {
            line[0] = '\0';
            x86os_puts("^C\n");
            break;
        }
        if (value == '\r' || value == '\n') {
            x86os_putchar('\n');
            break;
        }
        if (value == '\b' || value == 0x7f) {
            if (length != 0U) {
                --length;
                line[length] = '\0';
                x86os_puts("\b \b");
            }
        } else if (value == '\t') {
            line[length] = '\0';
            complete_line(line, &length);
        } else if (value >= ' ' && value <= '~' &&
                   length + 1U < SHELL_LINE_CAPACITY) {
            line[length++] = value;
            line[length] = '\0';
            x86os_putchar(value);
        }
    }
    line[length] = '\0';
}

static int split_line(char* line, const char* argv[SHELL_MAX_ARGUMENTS]) {
    int argc = 0;
    char* cursor = line;
    while (*cursor != '\0' && argc < SHELL_MAX_ARGUMENTS) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return argc;
}

static void show_prompt(void) {
    char path[SHELL_PATH_CAPACITY];
    if (x86os_getcwd(path, sizeof(path)) == 0) print_dos_path(path);
    else x86os_putchar('?');
    x86os_putchar('>');
}

static const char *usb_failure_name(uint32_t stage) {
    switch (stage) {
        case X86OS_USB_FAILURE_NONE: return "none";
        case X86OS_USB_FAILURE_PORT_RESET: return "port-reset";
        case X86OS_USB_FAILURE_ADDRESS_DEVICE: return "address-device";
        case X86OS_USB_FAILURE_DEVICE_DESCRIPTOR_8: return "device-desc-8";
        case X86OS_USB_FAILURE_EP0_DESCRIPTOR: return "ep0-desc";
        case X86OS_USB_FAILURE_DEVICE_DESCRIPTOR: return "device-desc";
        case X86OS_USB_FAILURE_CONFIG_HEADER: return "config-header";
        case X86OS_USB_FAILURE_CONFIG_LENGTH: return "config-length";
        case X86OS_USB_FAILURE_CONFIG_DESCRIPTOR: return "config-desc";
        case X86OS_USB_FAILURE_NO_BOOT_HID: return "no-boot-hid";
        case X86OS_USB_FAILURE_CONFIGURE_ENDPOINT:
            return "configure-endpoint";
        case X86OS_USB_FAILURE_SET_CONFIGURATION:
            return "set-configuration";
        case X86OS_USB_FAILURE_RELEASE_SLOT: return "release-slot";
        default: return "unknown";
    }
}

static void show_usb_keyboard_startup(void) {
    x86os_usb_diagnostics_t status;
    if (x86os_usb_diagnostics(&status) != 0 ||
        status.version != X86OS_USB_DIAGNOSTICS_VERSION ||
        status.struct_size != sizeof(status)) {
        x86os_puts("USB keyboard: diagnostics unavailable\n");
        return;
    }
    x86os_puts("USB keyboard: ");
    if ((status.state == X86OS_USB_STATE_KEYBOARD_READY ||
         status.state == X86OS_USB_STATE_KEYBOARD_MOUSE_READY) &&
        status.keyboard_port != 0U) {
        x86os_puts("ready port=");
        x86os_print_number((int)status.keyboard_port);
        x86os_puts(" slot=");
        x86os_print_number((int)status.keyboard_slot);
        x86os_puts(" endpoint=");
        x86os_print_number((int)status.keyboard_endpoint);
        x86os_puts(" reports=");
        x86os_print_number((int)status.keyboard_reports);
        x86os_puts(" rejected=");
        x86os_print_number((int)status.rejected_keyboard_reports);
    } else {
        x86os_puts("not ready state=");
        x86os_print_number((int)status.state);
        x86os_puts(" connected=");
        x86os_print_number((int)status.connected_ports);
        x86os_puts(" attempts=");
        x86os_print_number((int)status.attempts);
        x86os_puts(" failure=");
        x86os_puts(usb_failure_name(status.failure_stage));
        x86os_putchar('\n');
        x86os_puts("  candidate port=");
        x86os_print_number((int)status.candidate_port);
        x86os_puts(" speed=");
        x86os_print_number((int)status.candidate_speed);
        x86os_puts(" class=");
        x86os_print_number((int)status.device_class);
        x86os_putchar('/');
        x86os_print_number((int)status.device_subclass);
        x86os_putchar('/');
        x86os_print_number((int)status.device_protocol);
        x86os_puts(" config=");
        x86os_print_number((int)status.configuration_length);
        x86os_puts(" cc=");
        x86os_print_number((int)status.last_completion);
    }
    x86os_putchar('\n');
}

static void show_help(void) {
    x86os_puts("Built-ins: cd path pwd history help exit\n");
    x86os_puts("Aliases: dir type md rd erase clear ren mv cp\n");
    x86os_puts("Network: net ifconfig ping arp getip\n");
    x86os_puts("Use Up/Down to browse command history.\n");
    x86os_puts("Other commands are loaded as .PRG programs.\n");
}

static const char* program_alias(const char* command) {
    if (text_equal(command, "dir")) return "ls";
    if (text_equal(command, "type")) return "cat";
    if (text_equal(command, "md")) return "mkdir";
    if (text_equal(command, "rd")) return "rmdir";
    if (text_equal(command, "erase")) return "del";
    if (text_equal(command, "clear")) return "cls";
    if (text_equal(command, "ren")) return "rename";
    if (text_equal(command, "mv")) return "rename";
    if (text_equal(command, "cp")) return "copy";
    if (text_equal(command, "storage"))
        return "/libexec/reist/storage.prg";
    return command;
}

static const char* resident_program_path(const char* program) {
    static const struct {
        const char* name;
        const char* path;
        const char* legacy;
    } resident[] = {
        {"shell.prg", "/bin/shell.prg", "/shell.prg"},
        {"ls.prg", "/bin/ls.prg", "/ls.prg"},
        {"cat.prg", "/bin/cat.prg", "/cat.prg"},
        {"devctl.prg", "/sbin/devctl.prg", "/devctl.prg"},
        {"mount.prg", "/sbin/mount.prg", "/mount.prg"},
        {"umount.prg", "/sbin/umount.prg", "/umount.prg"},
        {"svcctl.prg", "/sbin/svcctl.prg", "/svcctl.prg"},
        {"drives.prg", "/sbin/drives.prg", "/drives.prg"},
        {"chkdsk.prg", "/sbin/chkdsk.prg", "/chkdsk.prg"},
        {"gtest.prg", "/libexec/reist/gtest.prg", "/gtest.prg"},
    };
    for (unsigned index = 0U;
         index < sizeof(resident) / sizeof(resident[0]); ++index) {
        if (text_equal(program, resident[index].name) ||
            text_equal(program, resident[index].path) ||
            text_equal(program, resident[index].legacy)) return resident[index].path;
    }
    return 0;
}

static void run_program(int argc, const char* argv[SHELL_MAX_ARGUMENTS]) {
    char program[SHELL_PATH_CAPACITY];
    const char* command = program_alias(argv[0]);
    unsigned length = text_length(command);
    unsigned suffix = has_program_extension(command) ? 0U : 4U;
    if (length + suffix + 1U > sizeof(program)) {
        x86os_puts("Command name is too long.\n");
        return;
    }
    int explicit_path = explicit_program_path(command);
    for (unsigned index = 0; index < length; ++index) {
        program[index] = explicit_path ? command[index] : lower(command[index]);
    }
    if (suffix != 0U) {
        program[length++] = '.';
        program[length++] = 'p';
        program[length++] = 'r';
        program[length++] = 'g';
    }
    program[length] = '\0';

    char executable[SHELL_PATH_CAPACITY];
    int found = 0;
    if (explicit_program_path(program)) {
        found = executable_file(program) &&
                copy_text(executable, sizeof(executable), program) == 0;
    } else if (executable_file(program)) {
        found = copy_text(executable, sizeof(executable), program) == 0;
    } else {
        for (unsigned index = 0; index < search_path_count; ++index) {
            if (join_program_path(search_paths[index], program, executable) == 0 &&
                executable_file(executable)) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        const char* resident = resident_program_path(program);
        if (resident != 0 && copy_text(executable, sizeof(executable), resident) == 0) {
            /* The protected kernel rescue cache can satisfy this spawn even
             * when root-storage loss makes stat and directory traversal fail. */
            found = 1;
        }
    }
    if (!found) {
        x86os_puts("Bad command or program file.\n");
        return;
    }

    const char* child_argv[SHELL_MAX_ARGUMENTS];
    child_argv[0] = program;
    for (int index = 1; index < argc; ++index) child_argv[index] = argv[index];
    int pid = x86os_spawnv(executable, argc, child_argv);
    if (pid < 0) {
        x86os_puts("Bad command or program file.\n");
        return;
    }
    int status;
    if (x86os_wait(pid, &status) < 0) x86os_puts("Unable to wait for program.\n");
}

int main(void) {
    char line[SHELL_LINE_CAPACITY];
    const char* argv[SHELL_MAX_ARGUMENTS];
    x86os_puts("REIST OS userspace shell\nType HELP for available commands.\n");
    show_usb_keyboard_startup();
    x86os_putchar('\n');
    for (;;) {
        show_prompt();
        read_line(line);
        history_add(line);
        int argc = split_line(line, argv);
        if (argc == 0) continue;
        if (text_equal(argv[0], "exit")) return 0;
        if (text_length(argv[0]) == 2U && argv[0][1] == ':') {
            char path[SHELL_PATH_CAPACITY];
            if (resolve_shell_path(argv[0], path) < 0 || x86os_chdir(path) < 0) {
                x86os_puts("Drive not available.\n");
            }
            continue;
        }
        if (text_equal(argv[0], "help")) {
            show_help();
        } else if (text_equal(argv[0], "history")) {
            history_show();
        } else if (text_equal(argv[0], "path")) {
            if (argc == 1) show_search_path();
            else if (argc == 2) set_search_path(argv[1]);
            else x86os_puts("Usage: path [directory[;directory...]]\n");
        } else if (text_equal(argv[0], "pwd")) {
            char path[SHELL_PATH_CAPACITY];
            if (x86os_getcwd(path, sizeof(path)) == 0) {
                print_dos_path(path);
                x86os_putchar('\n');
            } else x86os_puts("Unable to read working directory.\n");
        } else if (text_equal(argv[0], "cd") || text_equal(argv[0], "chdir")) {
            if (argc != 2) x86os_puts("Usage: cd <directory>\n");
            else {
                char path[SHELL_PATH_CAPACITY];
                if (resolve_shell_path(argv[1], path) < 0 ||
                    x86os_chdir(path) < 0) x86os_puts("Directory not found.\n");
            }
        } else if (text_equal(argv[0], "net")) {
            network_command(argc, argv);
        } else if (text_equal(argv[0], "ifconfig")) {
            ifconfig_command(argc, argv);
        } else if (text_equal(argv[0], "ping")) {
            ping_command(argc, argv);
        } else if (text_equal(argv[0], "arp")) {
            arp_command(argc, argv);
        } else if (text_equal(argv[0], "getip")) {
            (void)network_status(0);
        } else {
            run_program(argc, argv);
        }
    }
}
