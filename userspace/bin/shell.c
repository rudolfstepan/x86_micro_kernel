#include "x86os.h"

#define SHELL_LINE_CAPACITY 256
#define SHELL_PATH_CAPACITY 256
#define SHELL_MAX_ARGUMENTS 16
#define SHELL_MAX_PATH_ENTRIES 8

static char search_paths[SHELL_MAX_PATH_ENTRIES][SHELL_PATH_CAPACITY];
static unsigned search_path_count;

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

static char drive_letter(const x86os_drive_info_t* drive) {
    if (drive->name[3] < '0' || drive->name[3] > '9' || drive->name[4] != '\0') {
        return 0;
    }
    if (drive->type == X86OS_DRIVE_FDD) return (char)('A' + drive->name[3] - '0');
    if (drive->type == X86OS_DRIVE_ATA) return (char)('C' + drive->name[3] - '0');
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

static void read_line(char line[SHELL_LINE_CAPACITY]) {
    unsigned length = 0;
    for (;;) {
        char value = (char)x86os_getchar();
        if (value == '\r' || value == '\n') {
            x86os_putchar('\n');
            break;
        }
        if (value == '\b' || value == 0x7f) {
            if (length != 0U) {
                --length;
                x86os_puts("\b \b");
            }
        } else if (value >= ' ' && value <= '~' &&
                   length + 1U < SHELL_LINE_CAPACITY) {
            line[length++] = value;
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

static void show_help(void) {
    x86os_puts("Built-ins: cd path pwd help exit\n");
    x86os_puts("Other commands are loaded as .PRG programs.\n");
}

static void run_program(int argc, const char* argv[SHELL_MAX_ARGUMENTS]) {
    char program[SHELL_PATH_CAPACITY];
    unsigned length = text_length(argv[0]);
    unsigned suffix = has_program_extension(argv[0]) ? 0U : 4U;
    if (length + suffix + 1U > sizeof(program)) {
        x86os_puts("Command name is too long.\n");
        return;
    }
    for (unsigned index = 0; index < length; ++index) program[index] = argv[0][index];
    if (suffix != 0U) {
        program[length++] = '.';
        program[length++] = 'P';
        program[length++] = 'R';
        program[length++] = 'G';
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
    if (x86os_getcwd(search_paths[0], sizeof(search_paths[0])) == 0) {
        search_path_count = 1;
    }
    x86os_puts("x86 OS userspace shell\nType HELP for available commands.\n\n");
    for (;;) {
        show_prompt();
        read_line(line);
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
        } else if (text_equal(argv[0], "cd")) {
            if (argc != 2) x86os_puts("Usage: cd <directory>\n");
            else {
                char path[SHELL_PATH_CAPACITY];
                if (resolve_shell_path(argv[1], path) < 0 ||
                    x86os_chdir(path) < 0) x86os_puts("Directory not found.\n");
            }
        } else {
            run_program(argc, argv);
        }
    }
}
