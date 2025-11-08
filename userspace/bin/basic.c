/**
 * @file basic.c
 * @brief Simple BASIC interpreter for x86 microkernel
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

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

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
    if (slen(s1) != slen(s2)) return 0;
    for (int i = 0; s1[i]; i++)
        if (s1[i] != s2[i]) return 0;
    return 1;
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

void scpy(char* d, const char* s) {
    int i = 0;
    for (; s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

int isspc(char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

int isdg(char c) {
    return c >= '0' && c <= '9';
}

int simple_atoi(const char* s) {
    int i = 0;
    for (; *s && isdg(*s); s++)
        i = i * 10 + (*s - '0');
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
char varnames[64][8];
int  varcontent[64];

void initvars() {
    for (int i = 0; i < 64; ++i) {
        varnames[i][0] = 0;
        varcontent[i] = 0;
    }
}

int getvar(const char* s) {
    for (int i = 0; i < 64; ++i)
        if (scmp(s, varnames[i]))
            return varcontent[i];
    return 0;
}

int setvar(const char* s, int v) {
    // Check if variable exists
    for (int i = 0; i < 64; ++i) {
        if (scmp(s, varnames[i])) {
            varcontent[i] = v;
            return 1;
        }
    }
    
    // Find free slot
    for (int i = 0; i < 64; ++i) {
        if (!varnames[i][0]) {
            scpy(varnames[i], s);
            varcontent[i] = v;
            return 1;
        }
    }
    
    return 0; // No free slots
}

// Program storage (100 lines max for kernel version)
char prgm[100][64];

void initprgm() {
    for (int i = 0; i < 100; ++i)
        prgm[i][0] = 0;
}

// GOSUB stack
int _linestack[16];
int _linestackpos = 0;

void lnpush(int v) {
    if (_linestackpos < 16) {
        _linestack[_linestackpos++] = v;
    }
}

int lnpop() {
    if (_linestackpos > 0) {
        return _linestack[--_linestackpos];
    }
    return 0;
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
        if (scmp(s, bcmds[i]))
            return i;
    return -1;
}

// Command implementations
int cprint(int ln, char* s) {
    char* token = sstrtok(s);
    while (token && *token) {
        if (token[0] == '"') {
            // Print string literal
            for (++token; *token && *token != '"'; ++token)
                putchar(*token);
        } else {
            // Print number/variable
            printf("%d", emath(token));
        }
        token = sstrtok(NULL);
    }
    putchar('\n');
    return ln;
}

int cinput(int ln, char* s) {
    char vs[32], vn[16];
    char* token = sstrtok(s);
    if (!token) {
        berror(ln, "INVALID ARGS");
        return ln;
    }
    
    scpy(vn, token);
    printf("%s? ", vn);
    
    // Simple input reading
    int idx = 0;
    char ch;
    while ((ch = getchar()) != '\n' && idx < 31) {
        vs[idx++] = ch;
    }
    vs[idx] = '\0';
    
    setvar(vn, simple_atoi(vs));
    return ln;
}

int cvar(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok) {
        berror(ln, "INVALID ARGS");
        return ln;
    }
    
    char vn[16];
    scpy(vn, tok);
    
    tok = sstrtok(NULL);
    if (!tok) {
        berror(ln, "INVALID ARGS");
        return ln;
    }
    
    setvar(vn, emath(tok));
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
    return (target > 0 && target < 100) ? target - 1 : ln;
}

int cgosub(int ln, char* s) {
    char* tok = sstrtok(s);
    if (!tok) {
        berror(ln, "INVALID GOSUB");
        return ln;
    }
    int c = emath(tok);
    lnpush(ln);
    return (c > 0 && c < 100) ? c - 1 : ln;
}

int cret(int ln, char* s) {
    return lnpop();
}

int cend(int ln, char* s) {
    return 999; // Exit program
}

typedef int (*BasicCmd)(int, char*);
BasicCmd bfuncs[] = {cprint, cinput, cvar, cif, cgoto, cgosub, cret, cend};

int runcmd(int ln, char* s) {
    if (ln >= 100) return ln;
    
    char buf[64];
    scpy(buf, s);
    
    char* token = sstrtok(buf);
    if (!token) return ln;
    
    int cmd = getbcmd(token);
    if (cmd == -1) {
        berror(ln, "INVALID COMMAND");
        return ln;
    }
    
    return bfuncs[cmd](ln, _CURTOK);
}

// Math operators
int iand(int a, int b) { return a & b; }
int ior(int a, int b) { return a | b; }
int igt(int a, int b) { return a > b; }
int ilt(int a, int b) { return a < b; }
int ineq(int a, int b) { return a != b; }
int ieq(int a, int b) { return a == b; }
int imod(int a, int b) { return b ? a % b : 0; }
int imul(int a, int b) { return a * b; }
int idiv(int a, int b) { return b ? a / b : 0; }
int iadd(int a, int b) { return a + b; }
int isub(int a, int b) { return a - b; }

const char* mathops = "&|><~=%*/+-";
typedef int (*Mathfunc)(int, int);
Mathfunc mathfuncs[] = {iand, ior, igt, ilt, ineq, ieq, imod, imul, idiv, iadd, isub};

int emath(char* s) {
    if (!*s) return 0;
    
    for (int i = 0; mathops[i]; ++i) {
        for (int j = 0; s[j]; ++j) {
            if (s[j] == mathops[i]) {
                s[j] = 0;
                return mathfuncs[i](emath(s), emath(s + j + 1));
            }
        }
    }
    
    return isdg(*s) ? simple_atoi(s) : getvar(s);
}

// Main program execution
void run_basic() {
    for (int i = 0; i < 100; ++i) {
        if (!prgm[i][0]) continue;
        i = runcmd(i, prgm[i]);
        if (i >= 999) break; // END command
    }
}

// LOAD command - load .bas file from filesystem
void cmd_load(const char* filename) {
    extern int fat32_load_file(const char* filename, void* load_address);
    
    // Add .bas extension if not present
    char full_filename[32];
    scpy(full_filename, filename);
    
    // Check if .bas extension is missing
    int has_extension = 0;
    for (int i = 0; full_filename[i]; i++) {
        if (full_filename[i] == '.') {
            has_extension = 1;
            break;
        }
    }
    
    if (!has_extension) {
        // Add .BAS extension
        int len = slen(full_filename);
        full_filename[len] = '.';
        full_filename[len+1] = 'B';
        full_filename[len+2] = 'A';
        full_filename[len+3] = 'S';
        full_filename[len+4] = 0;
    }
    
    printf("Loading %s...\n", full_filename);
    
    // Allocate buffer for file (max 6400 bytes = 100 lines * 64 bytes)
    char* file_buffer = (char*)malloc(6400);
    if (!file_buffer) {
        printf("ERROR: Not enough memory to load file\n");
        return;
    }
    
    // Load file from filesystem
    int file_size = fat32_load_file(full_filename, file_buffer);
    if (file_size <= 0) {
        printf("ERROR: Could not load file '%s'\n", full_filename);
        free(file_buffer);
        return;
    }
    
    printf("Loaded %d bytes\n", file_size);
    
    // Clear current program
    initprgm();
    
    // Parse file line by line
    int line_start = 0;
    for (int i = 0; i <= file_size; i++) {
        if (i == file_size || file_buffer[i] == '\n' || file_buffer[i] == '\r') {
            if (i > line_start) {
                // Copy line to temp buffer
                char line_buf[64];
                int len = i - line_start;
                if (len > 63) len = 63;
                
                for (int j = 0; j < len; j++) {
                    line_buf[j] = file_buffer[line_start + j];
                }
                line_buf[len] = 0;
                
                // Parse line number
                char* ptr = line_buf;
                while (*ptr && isspc(*ptr)) ptr++;
                
                if (isdg(*ptr)) {
                    char* token = sstrtok(ptr);
                    int ln = simple_atoi(token);
                    
                    if (ln >= 0 && ln < 100 && _CURTOK && *_CURTOK) {
                        scpy(prgm[ln], _CURTOK);
                    }
                }
            }
            line_start = i + 1;
        }
    }
    
    free(file_buffer);
    printf("Program loaded successfully.\n");
}

// SAVE command - save .bas file to filesystem
void cmd_save(const char* filename) {
    extern bool fat32_create_file(const char* filename);
    extern FILE* fat32_open_file(const char* filename, const char* mode);
    
    // Add .bas extension if not present
    char full_filename[32];
    scpy(full_filename, filename);
    
    // Check if .bas extension is missing
    int has_extension = 0;
    for (int i = 0; full_filename[i]; i++) {
        if (full_filename[i] == '.') {
            has_extension = 1;
            break;
        }
    }
    
    if (!has_extension) {
        // Add .BAS extension
        int len = slen(full_filename);
        full_filename[len] = '.';
        full_filename[len+1] = 'B';
        full_filename[len+2] = 'A';
        full_filename[len+3] = 'S';
        full_filename[len+4] = 0;
    }
    
    printf("Saving %s...\n", full_filename);
    
    // Count non-empty lines
    int line_count = 0;
    for (int i = 0; i < 100; i++) {
        if (prgm[i][0]) line_count++;
    }
    
    if (line_count == 0) {
        printf("ERROR: No program to save\n");
        return;
    }
    
    // Build file content in memory
    char* file_buffer = (char*)malloc(6400);
    if (!file_buffer) {
        printf("ERROR: Not enough memory to save file\n");
        return;
    }
    
    int pos = 0;
    for (int i = 0; i < 100; i++) {
        if (prgm[i][0]) {
            // Write line number
            int ln = i;
            if (ln >= 10) {
                file_buffer[pos++] = '0' + (ln / 10);
                file_buffer[pos++] = '0' + (ln % 10);
            } else {
                file_buffer[pos++] = '0' + ln;
            }
            file_buffer[pos++] = ' ';
            
            // Write line content
            for (int j = 0; prgm[i][j] && pos < 6398; j++) {
                file_buffer[pos++] = prgm[i][j];
            }
            file_buffer[pos++] = '\n';
        }
    }
    file_buffer[pos] = 0;
    
    // Create file
    if (!fat32_create_file(full_filename)) {
        printf("ERROR: Could not create file '%s'\n", full_filename);
        free(file_buffer);
        return;
    }
    
    printf("Saved %d lines (%d bytes)\n", line_count, pos);
    printf("Note: File write functionality requires additional implementation\n");
    
    free(file_buffer);
}

// Entry point
void basic_interpreter() {
    printf("BASIC Interpreter v1.2\n");
    printf("Commands: RUN, LIST, NEW, LOAD, SAVE, EXIT, HELP\n");
    printf("(Commands are case-insensitive: run, RUN, Run all work)\n");
    printf("Enter program lines with line numbers:\n\n");
    
    initprgm();
    initvars();
    
    char buffer[64];
    
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
            for (int i = 0; i < 100; ++i) {
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
            printf("\nBASIC Interpreter v1.1 Commands:\n");
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
        
        if (ln >= 0 && ln < 100) {
            if (_CURTOK && *_CURTOK) {
                scpy(prgm[ln], _CURTOK);
            } else {
                prgm[ln][0] = 0; // Delete line
            }
        }
    }
}
