#include "x86os.h"

#define LS_COLUMNS 4U
#define LS_COLUMN_WIDTH 19U
#define LS_PAGE_LINES 22U

typedef struct {
    int long_format;
    int one_per_line;
    int show_hidden;
    int classify_dirs;
    int human_sizes;
    int pager;
} ls_options_t;

static unsigned int text_length(const char *text) {
    unsigned int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void print_spaces(unsigned int count) {
    while (count-- != 0U) x86os_putchar(' ');
}

static void print_unsigned(uint64_t value, unsigned int width) {
    char digits[20];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    if (width > count) print_spaces(width - count);
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_human_size(uint64_t value) {
    static const char suffix[] = "BKMGT";
    unsigned int unit = 0;
    uint64_t rounded = value;
    while (rounded >= 1024U && unit + 1U < sizeof(suffix) - 1U) {
        rounded = (rounded + 512U) / 1024U;
        ++unit;
    }
    print_unsigned(rounded, 5U);
    x86os_putchar(suffix[unit]);
}

static int page_line(ls_options_t *options, uint32_t *lines) {
    if (!options->pager || ++*lines < LS_PAGE_LINES) return 1;
    x86os_puts("-- More -- (Q quits)");
    int key = x86os_getchar();
    x86os_putchar('\r');
    x86os_puts("                    \r");
    *lines = 0;
    return key != 'q' && key != 'Q' && key != 0x1b;
}

static void print_compact_name(const x86os_file_info_t *entry,
                               const ls_options_t *options) {
    unsigned int length = text_length(entry->name);
    unsigned int marker = options->classify_dirs &&
                          entry->type == X86OS_DIRECTORY ? 1U : 0U;
    unsigned int limit = LS_COLUMN_WIDTH - marker - 1U;
    unsigned int shown = length > limit ? limit : length;
    for (unsigned int i = 0; i < shown; ++i) x86os_putchar(entry->name[i]);
    if (length > limit && shown != 0U) x86os_putchar('~');
    else if (marker != 0U) x86os_putchar('/');
    unsigned int used = shown + (length > limit || marker != 0U ? 1U : 0U);
    if (used < LS_COLUMN_WIDTH) print_spaces(LS_COLUMN_WIDTH - used);
}

static void print_long_entry(const x86os_file_info_t *entry,
                             const ls_options_t *options) {
    x86os_putchar(entry->type == X86OS_DIRECTORY ? 'd' : '-');
    x86os_puts("rw------- ");
    if (entry->type == X86OS_DIRECTORY) x86os_puts("     - ");
    else if (options->human_sizes) print_human_size(entry->size);
    else print_unsigned(entry->size, 6U);
    x86os_putchar(' ');
    x86os_puts(entry->name);
    if (options->classify_dirs && entry->type == X86OS_DIRECTORY)
        x86os_putchar('/');
    x86os_putchar('\n');
}

static void usage(void) {
    x86os_puts("Usage: ls [-1Calph] [--pager] [path]\n");
    x86os_puts("  -C columns (default)  -1 one entry per line  -l long format\n");
    x86os_puts("  -a include hidden     -p append / to dirs   -h human sizes\n");
    x86os_puts("  --pager paginate output (off by default)\n");
}

static int parse_options(int argc, char **argv, ls_options_t *options,
                         const char **path) {
    *path = ".";
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (text_equal(argument, "--help")) { usage(); return 1; }
        if (text_equal(argument, "--pager")) { options->pager = 1; continue; }
        if (text_equal(argument, "--no-pager")) { options->pager = 0; continue; }
        if (argument[0] != '-' || argument[1] == '\0') {
            if (*path != 0 && !text_equal(*path, ".")) return -1;
            *path = argument;
            continue;
        }
        for (unsigned int flag = 1; argument[flag] != '\0'; ++flag) {
            switch (argument[flag]) {
                case '1': options->one_per_line = 1; options->long_format = 0; break;
                case 'C': options->one_per_line = 0; options->long_format = 0; break;
                case 'l': options->long_format = 1; options->one_per_line = 1; break;
                case 'a': options->show_hidden = 1; break;
                case 'p': options->classify_dirs = 1; break;
                case 'h': options->human_sizes = 1; break;
                default: return -1;
            }
        }
    }
    return 0;
}

static int emit_entry(const x86os_file_info_t *entry, ls_options_t *options,
                      uint32_t *column, uint32_t *lines) {
    if (!options->show_hidden && entry->name[0] == '.') return 1;
    if (options->long_format) {
        if (!page_line(options, lines)) return 0;
        print_long_entry(entry, options);
    } else if (options->one_per_line) {
        if (!page_line(options, lines)) return 0;
        x86os_puts(entry->name);
        if (options->classify_dirs && entry->type == X86OS_DIRECTORY)
            x86os_putchar('/');
        x86os_putchar('\n');
    } else {
        if (*column == 0U && !page_line(options, lines)) return 0;
        print_compact_name(entry, options);
        *column = (*column + 1U) % LS_COLUMNS;
        if (*column == 0U) x86os_putchar('\n');
    }
    return 1;
}

int main(int argc, char **argv) {
    ls_options_t options = {0};
    const char *path = 0;
    int parsed = parse_options(argc, argv, &options, &path);
    if (parsed > 0) return 0;
    if (parsed < 0) { x86os_puts("ls: invalid option or too many paths\n"); usage(); return 2; }

    x86os_file_info_t target;
    if (x86os_stat(path, &target) < 0) {
        x86os_puts("ls: path not found\n");
        return 1;
    }
    uint32_t column = 0;
    uint32_t lines = 0;
    if (target.type != X86OS_DIRECTORY) {
        unsigned int length = text_length(path);
        target.name[0] = '\0';
        if (length >= sizeof(target.name)) length = sizeof(target.name) - 1U;
        for (unsigned int i = 0; i < length; ++i) target.name[i] = path[i];
        target.name[length] = '\0';
        (void)emit_entry(&target, &options, &column, &lines);
    } else {
        for (uint32_t index = 0;;) {
            x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
            int result = x86os_readdir_batch(path, index, entries);
            if (result < 0) { x86os_puts("ls: read error\n"); return 1; }
            if (result == 0) break;
            for (int item = 0; item < result; ++item) {
                if (!emit_entry(&entries[item], &options, &column, &lines))
                    return 0;
            }
            index += (uint32_t)result;
        }
    }
    if (column != 0U) x86os_putchar('\n');
    return 0;
}
