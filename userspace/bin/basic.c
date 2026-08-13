/**
 * @file basic.c
 * @brief Simple BASIC interpreter for REIST OS
 *
 * A minimal BASIC interpreter supporting:
 * - PRINT - output text and variables
 * - INPUT - read user input into variables
 * - VAR - set variable values
 * - IF - conditional execution
 * - GOTO - jump to line number
 * - GOSUB/RET - subroutine calls
 * - REM - comments
 * - END - exit program
 */

#include "x86os.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

#define BASIC_MAX_LINES 100
#define BASIC_LINE_CAPACITY 64
#define BASIC_FILE_LINE_CAPACITY (BASIC_LINE_CAPACITY + 4)
#define BASIC_MAX_VARIABLES 64
#define BASIC_VARIABLE_NAME_CAPACITY 8
#define BASIC_FILENAME_CAPACITY 32
#define BASIC_MAX_SERIALIZED_SIZE \
    (BASIC_MAX_LINES * (BASIC_LINE_CAPACITY + 3))

static void *basic_memcpy(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (size-- != 0U) *out++ = *in++;
    return destination;
}

static void *basic_memset(void *destination, int value, size_t size) {
    unsigned char *out = destination;
    while (size-- != 0U) *out++ = (unsigned char)value;
    return destination;
}

static void print_number(int value, bool unsigned_value) {
    char digits[11];
    unsigned int count = 0;
    uint32_t magnitude;
    if (!unsigned_value && value < 0) {
        x86os_putchar('-');
        magnitude = 0U - (uint32_t)value;
    } else {
        magnitude = (uint32_t)value;
    }
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void basic_printf(const char *format, ...) {
    va_list values;
    va_start(values, format);
    while (*format != '\0') {
        if (*format != '%') {
            x86os_putchar(*format++);
            continue;
        }
        ++format;
        if (*format == 's') x86os_puts(va_arg(values, const char*));
        else if (*format == 'd') print_number(va_arg(values, int), false);
        else if (*format == 'u') print_number((int)va_arg(values, unsigned int), true);
        else if (*format == '%') x86os_putchar('%');
        if (*format != '\0') ++format;
    }
    va_end(values);
}

static void basic_get_input_line(char *buffer, int capacity) {
    int length = 0;
    for (;;) {
        char ch = (char)x86os_getchar();
        if (ch == '\r' || ch == '\n') {
            x86os_putchar('\n');
            break;
        }
        if (ch == '\b') {
            if (length > 0) {
                --length;
                x86os_puts("\b \b");
            }
        } else if (ch >= ' ' && ch <= '~' && length + 1 < capacity) {
            buffer[length++] = ch;
            x86os_putchar(ch);
        }
    }
    buffer[length] = '\0';
}

#define printf basic_printf
#define putchar x86os_putchar
#define getchar x86os_getchar
#define malloc x86os_malloc
#define free x86os_free
#define memcpy basic_memcpy
#define memset basic_memset
#define get_input_line basic_get_input_line

int scmp_nocase(const char* s1, const char* s2);

// Simplified implementations for kernel environment
int slen(const char* s) {
    int i = 0;
    for (; s[i]; i++);
    return i;
}

// Convert character to uppercase
char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

// Case-sensitive string comparison
int scmp(const char* s1, const char* s2) {
    return scmp_nocase(s1, s2);
}

// Case-insensitive string comparison
int scmp_nocase(const char* s1, const char* s2) {
    if (slen(s1) != slen(s2)) return 0;
    for (int i = 0; s1[i]; i++)
        if (to_upper(s1[i]) != to_upper(s2[i])) return 0;
    return 1;
}

// Check if string starts with prefix (case-insensitive)
int starts_with(const char* str, const char* prefix) {
    for (int i = 0; prefix[i]; i++) {
        if (!str[i] || to_upper(str[i]) != to_upper(prefix[i])) {
            return 0;
        }
    }
    return 1;
}

bool scpy_bounded(char* d, size_t capacity, const char* s) {
    if (!d || !s || capacity == 0) return false;

    size_t i = 0;
    while (s[i]) {
        if (i + 1 >= capacity) {
            d[0] = 0;
            return false;
        }
        d[i] = s[i];
        ++i;
    }
    d[i] = 0;
    return true;
}

int isspc(char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

int isdg(char c) {
    return c >= '0' && c <= '9';
}

int simple_atoi(const char* s) {
    int i = 0;
    for (; *s && isdg(*s); s++) {
        int digit = *s - '0';
        if (i > (INT_MAX - digit) / 10) return INT_MAX;
        i = i * 10 + digit;
    }
    return i;
}

// Token handling
char* _CURTOK = NULL;

char* sstrtok(char* s) {
    static char* saved = NULL;

    if (s != NULL) {
        saved = s;
    }

    if (saved == NULL || *saved == '\0') {
        return NULL;
    }

    // Skip leading spaces
    while (*saved && isspc(*saved)) {
        saved++;
    }

    if (*saved == '\0') {
        return NULL;
    }

    char* token_start = saved;

    // Find end of token
    while (*saved && !isspc(*saved)) {
        saved++;
    }

    if (*saved) {
        *saved = '\0';
        saved++;
        _CURTOK = saved;
    } else {
        _CURTOK = saved;
    }

    return token_start;
}

// Variable storage (64 variables max)
char varnames[BASIC_MAX_VARIABLES][BASIC_VARIABLE_NAME_CAPACITY];
int  varcontent[BASIC_MAX_VARIABLES];

void initvars() {
    for (int i = 0; i < BASIC_MAX_VARIABLES; ++i) {
        varnames[i][0] = 0;
        varcontent[i] = 0;
    }
}

int getvar(const char* s) {
    for (int i = 0; i < BASIC_MAX_VARIABLES; ++i)
        if (scmp_nocase(s, varnames[i]))
            return varcontent[i];
    return 0;
}

int setvar(const char* s, int v) {
    if (!s || !*s || slen(s) >= BASIC_VARIABLE_NAME_CAPACITY) {
        return 0;
    }

    // Check if variable exists
    for (int i = 0; i < BASIC_MAX_VARIABLES; ++i) {
        if (scmp_nocase(s, varnames[i])) {
            varcontent[i] = v;
            return 1;
        }
    }

    // Find free slot
    for (int i = 0; i < BASIC_MAX_VARIABLES; ++i) {
        if (!varnames[i][0]) {
            if (!scpy_bounded(varnames[i], sizeof(varnames[i]), s)) {
                return 0;
            }
            varcontent[i] = v;
            return 1;
        }
    }

    return 0; // No free slots
}

// Program storage (100 lines max for kernel version)
char prgm[BASIC_MAX_LINES][BASIC_LINE_CAPACITY];

// GOSUB stack
int _linestack[16];
int _linestackpos = 0;

void initprgm() {
    for (int i = 0; i < BASIC_MAX_LINES; ++i)
        prgm[i][0] = 0;
    _linestackpos = 0;
}

bool lnpush(int v) {
    if (_linestackpos >= (int)(sizeof(_linestack) / sizeof(_linestack[0]))) {
        return false;
    }
    _linestack[_linestackpos++] = v;
    return true;
}

bool lnpop(int* value) {
    if (!value || _linestackpos <= 0) return false;
    *value = _linestack[--_linestackpos];
    return true;
}

// Error handling
void berror(int linenum, const char* e) {
    if (linenum == -1)
        printf("ERROR: %s\n", e);
    else
        printf("ERROR AT %d: %s\n", linenum, e);
}

// Forward declarations
int emath(char*);
int runcmd(int, char*);

// Command types
typedef enum {
    PRINT, INPUT, VAR, IF, GOTO, GOSUB, RET, END
} BasicCommands;

const char* bcmds[] = {
    "PRINT", "INPUT", "VAR", "IF", "GOTO", "GOSUB", "RET", "END"
};

int getbcmd(const char* s) {
    for (int i = 0; i <= END; ++i)
        if (scmp_nocase(s, bcmds[i]))
            return i;
    return -1;
}

// Command implementations
int cprint(int ln, char* s) {
    char* cursor = s;
    while (cursor && *cursor) {
        while (*cursor && isspc(*cursor)) ++cursor;
        if (!*cursor) break;

        if (*cursor == '"') {
            ++cursor;
            while (*cursor && *cursor != '"') {
                putchar(*cursor++);
            }
            if (*cursor != '"') {
                berror(ln, "UNTERMINATED STRING");
                return ln;
            }
            ++cursor;
        } else {
            char* expression = cursor;
            while (*cursor && !isspc(*cursor)) ++cursor;
            char saved = *cursor;
            *cursor = 0;
            printf("%d", emath(expression));
            *cursor = saved;
        }
    }
    putchar('\n');
    return ln;
}

int cinput(int ln, char* s) {
    char vs[32], vn[BASIC_VARIABLE_NAME_CAPACITY];
    char* token = sstrtok(s);
    if (!token) {
        berror(ln, "INVALID ARGS");
        return ln;
    }

    if (!scpy_bounded(vn, sizeof(vn), token)) {
        berror(ln, "INVALID VARIABLE NAME");
        return ln;
    }
    printf("%s? ", vn);

    // Simple input reading
    int idx = 0;
    bool truncated = false;
    char ch;
    while ((ch = getchar()) != '\n') {
        if (idx < (int)sizeof(vs) - 1) vs[idx++] = ch;
        else truncated = true;
    }
    vs[idx] = '\0';

    if (truncated) {
        berror(ln, "INPUT TOO LONG");
        return ln;
    }

    if (!setvar(vn, simple_atoi(vs))) {
        berror(ln, "VARIABLE TABLE FULL");
    }
    return ln;
}

int cvar(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok) {
        berror(ln, "INVALID ARGS");
        return ln;
    }

    char vn[BASIC_VARIABLE_NAME_CAPACITY];
    if (!scpy_bounded(vn, sizeof(vn), tok)) {
        berror(ln, "INVALID VARIABLE NAME");
        return ln;
    }

    tok = sstrtok(NULL);
    if (!tok) {
        berror(ln, "INVALID ARGS");
        return ln;
    }

    if (!setvar(vn, emath(tok))) {
        berror(ln, "VARIABLE TABLE FULL");
    }
    return ln;
}

int cif(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok || !_CURTOK) {
        berror(ln, "INVALID IF STATEMENT");
        return ln;
    }
    return emath(tok) ? runcmd(ln, _CURTOK) : ln;
}

int cgoto(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok) {
        berror(ln, "INVALID GOTO");
        return ln;
    }
    int target = emath(tok);
    return (target > 0 && target < BASIC_MAX_LINES) ? target - 1 : ln;
}

int cgosub(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok) {
        berror(ln, "INVALID GOSUB");
        return ln;
    }
    int c = emath(tok);
    if (c <= 0 || c >= BASIC_MAX_LINES) {
        berror(ln, "INVALID GOSUB TARGET");
        return ln;
    }
    if (!lnpush(ln)) {
        berror(ln, "GOSUB STACK OVERFLOW");
        return ln;
    }
    return c - 1;
}

int cret(int ln, char* s) {
    (void)s;
    int target = 0;
    if (!lnpop(&target)) {
        berror(ln, "RET WITHOUT GOSUB");
        return ln;
    }
    return target;
}

int cend(int ln, char* s) {
    (void)ln;
    (void)s;
    return 999; // Exit program
}

typedef int (*BasicCmd)(int, char*);
BasicCmd bfuncs[] = {cprint, cinput, cvar, cif, cgoto, cgosub, cret, cend};

int runcmd(int ln, char* s) {
    if (ln >= BASIC_MAX_LINES) return ln;

    char buf[BASIC_LINE_CAPACITY];
    if (!scpy_bounded(buf, sizeof(buf), s)) {
        berror(ln, "LINE TOO LONG");
        return ln;
    }

    char* token = sstrtok(buf);
    if (!token) return ln;

    int cmd = getbcmd(token);
    if (cmd == -1) {
        berror(ln, "INVALID COMMAND");
        return ln;
    }

    return bfuncs[cmd](ln, _CURTOK);
}

// Recursive-descent expression parser.  Each loop makes binary operators
// left-associative, while the call hierarchy implements normal precedence.
typedef struct {
    const char* cursor;
    bool error;
} basic_expression_parser_t;

static void expression_skip_spaces(basic_expression_parser_t* parser) {
    while (isspc(*parser->cursor)) ++parser->cursor;
}

static bool expression_name_delimiter(char c) {
    return c == 0 || isspc(c) || c == '(' || c == ')' ||
           c == '&' || c == '|' || c == '>' || c == '<' || c == '~' ||
           c == '=' || c == '%' || c == '*' || c == '/' || c == '+' ||
           c == '-';
}

static int expression_parse_or(basic_expression_parser_t* parser);

static int expression_parse_primary(basic_expression_parser_t* parser) {
    expression_skip_spaces(parser);

    if (*parser->cursor == '(') {
        ++parser->cursor;
        int value = expression_parse_or(parser);
        expression_skip_spaces(parser);
        if (*parser->cursor != ')') {
            parser->error = true;
            return 0;
        }
        ++parser->cursor;
        return value;
    }

    if (isdg(*parser->cursor)) {
        int value = 0;
        while (isdg(*parser->cursor)) {
            int digit = *parser->cursor++ - '0';
            if (value > (INT_MAX - digit) / 10) {
                parser->error = true;
                return 0;
            }
            value = value * 10 + digit;
        }
        return value;
    }

    char name[BASIC_VARIABLE_NAME_CAPACITY];
    size_t length = 0;
    while (!expression_name_delimiter(*parser->cursor)) {
        if (length + 1 >= sizeof(name)) {
            parser->error = true;
            return 0;
        }
        name[length++] = *parser->cursor++;
    }
    if (length == 0) {
        parser->error = true;
        return 0;
    }
    name[length] = 0;
    return getvar(name);
}

static int expression_parse_unary(basic_expression_parser_t* parser) {
    expression_skip_spaces(parser);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return expression_parse_unary(parser);
    }
    if (*parser->cursor == '-') {
        ++parser->cursor;
        int value = expression_parse_unary(parser);
        return (int)(0u - (uint32_t)value);
    }
    return expression_parse_primary(parser);
}

static int expression_parse_product(basic_expression_parser_t* parser) {
    int value = expression_parse_unary(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        char op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') break;
        ++parser->cursor;
        int rhs = expression_parse_unary(parser);
        if ((op == '/' || op == '%') && rhs == 0) {
            parser->error = true;
            return 0;
        }
        if (op == '*') {
            value = (int)((uint32_t)value * (uint32_t)rhs);
        } else if (op == '/') {
            value = (value == INT_MIN && rhs == -1) ? INT_MIN : value / rhs;
        } else {
            value = (value == INT_MIN && rhs == -1) ? 0 : value % rhs;
        }
    }
    return value;
}

static int expression_parse_sum(basic_expression_parser_t* parser) {
    int value = expression_parse_product(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        char op = *parser->cursor;
        if (op != '+' && op != '-') break;
        ++parser->cursor;
        int rhs = expression_parse_product(parser);
        value = op == '+'
                    ? (int)((uint32_t)value + (uint32_t)rhs)
                    : (int)((uint32_t)value - (uint32_t)rhs);
    }
    return value;
}

static int expression_parse_relation(basic_expression_parser_t* parser) {
    int value = expression_parse_sum(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        char op = *parser->cursor;
        if (op != '<' && op != '>') break;
        ++parser->cursor;
        int rhs = expression_parse_sum(parser);
        value = op == '<' ? value < rhs : value > rhs;
    }
    return value;
}

static int expression_parse_equality(basic_expression_parser_t* parser) {
    int value = expression_parse_relation(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        char op = *parser->cursor;
        if (op != '=' && op != '~') break;
        ++parser->cursor;
        int rhs = expression_parse_relation(parser);
        value = op == '=' ? value == rhs : value != rhs;
    }
    return value;
}

static int expression_parse_and(basic_expression_parser_t* parser) {
    int value = expression_parse_equality(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        if (*parser->cursor != '&') break;
        ++parser->cursor;
        value &= expression_parse_equality(parser);
    }
    return value;
}

static int expression_parse_or(basic_expression_parser_t* parser) {
    int value = expression_parse_and(parser);
    while (!parser->error) {
        expression_skip_spaces(parser);
        if (*parser->cursor != '|') break;
        ++parser->cursor;
        value |= expression_parse_and(parser);
    }
    return value;
}

int emath(char* s) {
    if (!s || !*s) return 0;

    basic_expression_parser_t parser = {.cursor = s, .error = false};
    int value = expression_parse_or(&parser);
    expression_skip_spaces(&parser);
    return (!parser.error && *parser.cursor == 0) ? value : 0;
}

// Main program execution
void run_basic() {
    _linestackpos = 0;
    for (int i = 0; i < BASIC_MAX_LINES; ++i) {
        if (!prgm[i][0]) continue;
        i = runcmd(i, prgm[i]);
        if (i >= 999) break; // END command
    }
}

static bool build_basic_filename(char* destination, size_t capacity,
                                 const char* filename) {
    if (!scpy_bounded(destination, capacity, filename) || !destination[0]) {
        return false;
    }

    bool has_extension = false;
    size_t length = 0;
    while (destination[length]) {
        if (destination[length] == '.') has_extension = true;
        ++length;
    }

    if (!has_extension) {
        static const char extension[] = ".BAS";
        if (length + sizeof(extension) > capacity) return false;
        memcpy(destination + length, extension, sizeof(extension));
    }
    return true;
}

// LOAD command - load .bas file from filesystem
void cmd_load(const char* filename) {
    char full_filename[BASIC_FILENAME_CAPACITY];
    if (!build_basic_filename(full_filename, sizeof(full_filename), filename)) {
        printf("ERROR: Invalid or too-long filename\n");
        return;
    }

    printf("Loading %s...\n", full_filename);

    x86os_file_info_t info;
    if (x86os_stat(full_filename, &info) != 0 || info.type != X86OS_FILE) {
        printf("ERROR: Could not find file '%s'\n", full_filename);
        return;
    }
    uint32_t file_size = info.size;
    if (file_size > BASIC_MAX_SERIALIZED_SIZE) {
        printf("ERROR: File is too large (%u bytes, maximum %u)\n",
               file_size, (unsigned int)BASIC_MAX_SERIALIZED_SIZE);
        return;
    }

    char* file_buffer = (char*)malloc((size_t)file_size + 1u);
    if (!file_buffer) {
        printf("ERROR: Not enough memory to load file\n");
        return;
    }

    int descriptor = x86os_open(full_filename);
    uint32_t loaded_size = 0;
    while (descriptor >= 0 && loaded_size < file_size) {
        int amount = x86os_read(descriptor, file_buffer + loaded_size,
                                file_size - loaded_size);
        if (amount <= 0) break;
        loaded_size += (uint32_t)amount;
    }
    if (descriptor >= 0) (void)x86os_close(descriptor);
    if (descriptor < 0 || loaded_size != file_size) {
        printf("ERROR: Could not load file '%s'\n", full_filename);
        free(file_buffer);
        return;
    }
    file_buffer[file_size] = 0;

    char (*loaded_program)[BASIC_LINE_CAPACITY] =
        malloc(sizeof(prgm));
    if (!loaded_program) {
        printf("ERROR: Not enough memory to parse file\n");
        free(file_buffer);
        return;
    }
    memset(loaded_program, 0, sizeof(prgm));

    // Parse file line by line
    uint32_t line_start = 0;
    bool parse_error = false;
    for (uint32_t i = 0; i <= file_size; i++) {
        if (i == file_size || file_buffer[i] == '\n' || file_buffer[i] == '\r') {
            if (i > line_start) {
                char line_buf[BASIC_FILE_LINE_CAPACITY];
                uint32_t length = i - line_start;
                if (length >= sizeof(line_buf)) {
                    printf("ERROR: BASIC source line is too long\n");
                    parse_error = true;
                    break;
                }

                for (uint32_t j = 0; j < length; j++) {
                    line_buf[j] = file_buffer[line_start + j];
                }
                line_buf[length] = 0;

                char* ptr = line_buf;
                while (*ptr && isspc(*ptr)) ptr++;
                if (!isdg(*ptr)) {
                    printf("ERROR: BASIC source line has no line number\n");
                    parse_error = true;
                    break;
                }

                char* token = sstrtok(ptr);
                int line_number = 0;
                for (size_t j = 0; token[j]; ++j) {
                    if (!isdg(token[j]) || line_number > 9) {
                        parse_error = true;
                        break;
                    }
                    line_number = line_number * 10 + (token[j] - '0');
                }
                if (parse_error || line_number >= BASIC_MAX_LINES) {
                    printf("ERROR: Invalid BASIC line number\n");
                    parse_error = true;
                    break;
                }

                char* command = _CURTOK;
                while (command && isspc(*command)) ++command;
                if (command && *command &&
                    !scpy_bounded(loaded_program[line_number],
                                  sizeof(loaded_program[line_number]), command)) {
                    printf("ERROR: BASIC command is too long\n");
                    parse_error = true;
                    break;
                }
            }
            line_start = i + 1;
        }
    }

    if (!parse_error) {
        memcpy(prgm, loaded_program, sizeof(prgm));
        _linestackpos = 0;
        printf("Loaded %u bytes successfully.\n", file_size);
    } else {
        printf("Program was not changed.\n");
    }

    free(loaded_program);
    free(file_buffer);
}

// SAVE command - save .bas file to filesystem
void cmd_save(const char* filename) {
    char full_filename[BASIC_FILENAME_CAPACITY];
    if (!build_basic_filename(full_filename, sizeof(full_filename), filename)) {
        printf("ERROR: Invalid or too-long filename\n");
        return;
    }

    printf("Saving %s...\n", full_filename);

    int line_count = 0;
    size_t serialized_size = 0;
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (prgm[i][0]) {
            size_t command_size = (size_t)slen(prgm[i]);
            serialized_size += (i >= 10 ? 2u : 1u) + 1u + command_size + 1u;
            ++line_count;
        }
    }

    if (line_count == 0) {
        printf("ERROR: No program to save\n");
        return;
    }

    if (serialized_size > BASIC_MAX_SERIALIZED_SIZE) {
        printf("ERROR: Program is too large to serialize\n");
        return;
    }

    char* file_buffer = (char*)malloc(serialized_size);
    if (!file_buffer) {
        printf("ERROR: Not enough memory to save file\n");
        return;
    }

    size_t pos = 0;
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (prgm[i][0]) {
            if (i >= 10) {
                file_buffer[pos++] = (char)('0' + (i / 10));
                file_buffer[pos++] = (char)('0' + (i % 10));
            } else {
                file_buffer[pos++] = (char)('0' + i);
            }
            file_buffer[pos++] = ' ';

            for (int j = 0; prgm[i][j]; j++) {
                file_buffer[pos++] = prgm[i][j];
            }
            file_buffer[pos++] = '\n';
        }
    }

    (void)x86os_unlink(full_filename);
    int descriptor = x86os_create(full_filename);
    size_t written = 0;
    while (descriptor >= 0 && written < serialized_size) {
        int amount = x86os_write(descriptor, file_buffer + written,
                                 serialized_size - written);
        if (amount <= 0) break;
        written += (size_t)amount;
    }
    if (descriptor >= 0) (void)x86os_close(descriptor);
    if (descriptor < 0 || written != serialized_size) {
        printf("ERROR: Could not write complete file '%s'\n", full_filename);
        free(file_buffer);
        return;
    }

    free(file_buffer);
    printf("Saved %d lines (%u bytes)\n", line_count,
           (unsigned int)serialized_size);
}

// Entry point
void basic_interpreter() {
    printf("BASIC Interpreter v1.2\n");
    printf("Commands: RUN, LIST, NEW, LOAD, SAVE, EXIT, HELP\n");
    printf("(Commands are case-insensitive: run, RUN, Run all work)\n");
    printf("Enter program lines with line numbers:\n\n");

    initprgm();
    initvars();

    char buffer[BASIC_LINE_CAPACITY];

    while (1) {
        printf("] ");
        get_input_line(buffer, 63);

        // Check for special commands (case-insensitive)
        if (scmp_nocase(buffer, "RUN") || scmp_nocase(buffer, "run")) {
            printf("\nRunning program...\n");
            run_basic();
            printf("\nProgram ended.\n");
            continue;
        }

        if (scmp_nocase(buffer, "LIST") || scmp_nocase(buffer, "list")) {
            printf("\nProgram listing:\n");
            for (int i = 0; i < BASIC_MAX_LINES; ++i) {
                if (prgm[i][0]) {
                    printf("%d %s\n", i, prgm[i]);
                }
            }
            continue;
        }

        if (scmp_nocase(buffer, "NEW") || scmp_nocase(buffer, "new")) {
            initprgm();
            initvars();
            printf("Program cleared.\n");
            continue;
        }

        // LOAD command (case-insensitive)
        if (starts_with(buffer, "LOAD") || starts_with(buffer, "load")) {
            char* ptr = buffer;
            // Skip past "LOAD" or "load"
            while (*ptr && !isspc(*ptr)) ptr++;
            while (*ptr && isspc(*ptr)) ptr++;

            if (*ptr) {
                cmd_load(ptr);
            } else {
                printf("Usage: LOAD filename\n");
            }
            continue;
        }

        // SAVE command (case-insensitive)
        if (starts_with(buffer, "SAVE") || starts_with(buffer, "save")) {
            char* ptr = buffer;
            // Skip past "SAVE" or "save"
            while (*ptr && !isspc(*ptr)) ptr++;
            while (*ptr && isspc(*ptr)) ptr++;

            if (*ptr) {
                cmd_save(ptr);
            } else {
                printf("Usage: SAVE filename\n");
            }
            continue;
        }

        // HELP command (case-insensitive)
        if (scmp_nocase(buffer, "HELP") || scmp_nocase(buffer, "help") || scmp_nocase(buffer, "?")) {
            printf("\nBASIC Interpreter v1.2 Commands:\n");
            printf("  RUN            - Execute the program\n");
            printf("  LIST           - Display program listing\n");
            printf("  NEW            - Clear program and variables\n");
            printf("  LOAD filename  - Load .BAS file from filesystem\n");
            printf("  SAVE filename  - Save program to .BAS file\n");
            printf("  EXIT / QUIT    - Return to shell\n");
            printf("  HELP / ?       - Show this help\n");
            printf("\nProgram commands (uppercase):\n");
            printf("  PRINT, INPUT, VAR, IF, GOTO, GOSUB, RET, END\n");
            printf("\n");
            continue;
        }

        if (scmp_nocase(buffer, "EXIT") || scmp_nocase(buffer, "exit") ||
            scmp_nocase(buffer, "QUIT") || scmp_nocase(buffer, "quit")) {
            printf("\nExiting BASIC interpreter...\n");
            break;
        }

        // Parse line number and store command
        char* ptr = buffer;
        while (*ptr && isspc(*ptr)) ptr++;

        if (!*ptr) continue;

        if (!isdg(*ptr)) {
            printf("ERROR: Lines must start with a number\n");
            continue;
        }

        char* token = sstrtok(ptr);
        int ln = simple_atoi(token);

        if (ln >= 0 && ln < BASIC_MAX_LINES) {
            if (_CURTOK && *_CURTOK) {
                char* command = _CURTOK;
                while (*command && isspc(*command)) ++command;
                if (!scpy_bounded(prgm[ln], sizeof(prgm[ln]), command)) {
                    printf("ERROR: Program line is too long\n");
                }
            } else {
                prgm[ln][0] = 0; // Delete line
            }
        } else {
            printf("ERROR: Line number must be between 0 and %d\n",
                   BASIC_MAX_LINES - 1);
        }
    }
}

int main(void) {
    basic_interpreter();
    return 0;
}
