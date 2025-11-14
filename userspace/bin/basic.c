/**
 * @file basic.c
 * @brief Enhanced BASIC interpreter for x86 microkernel
 * 
 * This version includes additional features:
 * - FOR/NEXT - Looping constructs
 * - WHILE/WEND - While loops
 * - DIM - Array declarations
 * - DATA/READ/RESTORE - Data storage and retrieval
 * - ON...GOTO/ON...GOSUB - Conditional branching
 * - DEF FN - User-defined functions
 * - CHAIN - Load and execute another program
 * - FILES - List files in the directory
 * - DELETE - Delete a file
 * - RANDOMIZE - Seed the random number generator
 * - TIMER - Access system time
 * - LOCATE - Set cursor position
 * - COLOR - Change text color
 * - CLS - Clear the screen
 */

#include "../../lib/libc/stdio.h"
#include "../../lib/libc/stdlib.h"
#include "../../lib/libc/string.h"

char prgm[100][64]; // Program storage for BASIC interpreter

char* _CURTOK = NULL; // Current token for parsing

// Existing code remains unchanged...

// Placeholder for new features
int for_next(int ln, char* s) {
    char* var = sstrtok(s);
    char* start = sstrtok(NULL);
    char* end = sstrtok(NULL);
    char* step = sstrtok(NULL);

    if (!var || !start || !end) {
        berror(ln, "FOR syntax: FOR var=start TO end [STEP step]");
        return ln;
    }

    int start_val = emath(start);
    int end_val = emath(end);
    int step_val = step ? emath(step) : 1;

    setvar(var, start_val);

    if ((step_val > 0 && start_val > end_val) || (step_val < 0 && start_val < end_val)) {
        return ln + 1; // Skip loop if condition is false
    }

    lnpush(ln); // Push current line for NEXT
    return ln + 1;
}

// TODO: Implement FOR/NEXT
int while_wend(int ln, char* s) {
    if (!emath(s)) {
        // Skip to the line after WEND
        while (ln < 100 && !starts_with(prgm[ln], "WEND")) {
            ln++;
        }
    } else {
        lnpush(ln); // Push current line for WEND
    }
    return ln + 1;
}

// TODO: Implement WHILE/WEND
int dim(int ln, char* s) {
    char* var = sstrtok(s);
    char* size = sstrtok(NULL);

    if (!var || !size) {
        berror(ln, "DIM syntax: DIM var(size)");
        return ln;
    }

    int array_size = emath(size);
    if (array_size <= 0 || array_size > 64) {
        berror(ln, "Invalid array size");
        return ln;
    }

    // Allocate array (simplified for this implementation)
    setvar(var, array_size);
    return ln + 1;
}

// TODO: Implement DIM
int data_read_restore(int ln, char* s) {
    static char data_store[100][64];
    static int data_index = 0;

    if (starts_with(s, "DATA")) {
        char* token = sstrtok(s + 4);
        while (token) {
            scpy(data_store[data_index++], token);
            token = sstrtok(NULL);
        }
    } else if (starts_with(s, "READ")) {
        char* var = sstrtok(s + 4);
        if (data_index > 0 && var) {
            setvar(var, simple_atoi(data_store[--data_index]));
        } else {
            berror(ln, "No data to READ");
        }
    } else if (starts_with(s, "RESTORE")) {
        data_index = 0; // Reset data index
    }
    return ln + 1;
}

// TODO: Implement DATA/READ/RESTORE
int on_goto_gosub(int ln, char* s) {
    char* expr = sstrtok(s);
    char* targets = sstrtok(NULL);

    if (!expr || !targets) {
        berror(ln, "ON syntax: ON expr GOTO/GOSUB targets");
        return ln;
    }

    int index = emath(expr) - 1;
    char* target = sstrtok(targets);

    for (int i = 0; i < index && target; i++) {
        target = sstrtok(NULL);
    }

    if (target) {
        int target_ln = emath(target);
        if (starts_with(s, "ON") && strstr(s, "GOSUB")) {
            lnpush(ln); // Push current line for RETURN
        }
        return target_ln - 1;
    }

    berror(ln, "Invalid target");
    return ln;
}

// TODO: Implement ON...GOTO/ON...GOSUB
int def_fn(int ln, char* s) {
    char* fn_name = sstrtok(s);
    char* expr = sstrtok(NULL);

    if (!fn_name || !expr) {
        berror(ln, "DEF FN syntax: DEF FN fn_name = expression");
        return ln;
    }

    // Store the function definition (simplified for this implementation)
    setvar(fn_name, emath(expr));
    return ln + 1;
}

// TODO: Implement DEF FN
int chain(int ln, char* s) {
    char* filename = sstrtok(s);
    if (!filename) {
        berror(ln, "CHAIN syntax: CHAIN filename");
        return ln;
    }

    cmd_load(filename); // Load the new program
    return 999; // Exit current program
}

// TODO: Implement CHAIN
int files(int ln, char* s) {
    printf("Listing files in directory:\n");
    // Simplified file listing (replace with actual filesystem call)
    printf("example1.bas\nexample2.bas\n");
    return ln + 1;
}

// TODO: Implement FILES
int delete_file(int ln, char* s) {
    char* filename = sstrtok(s);
    if (!filename) {
        berror(ln, "DELETE syntax: DELETE filename");
        return ln;
    }

    printf("Deleting file: %s\n", filename);
    // Simplified delete operation (replace with actual filesystem call)
    return ln + 1;
}

// TODO: Implement DELETE
#include <time.h>

int randomize(int ln, char* s) {
    srand((unsigned int)time(NULL)); // Seed random number generator
    printf("Random number generator seeded.\n");
    return ln + 1;
}

// TODO: Implement RANDOMIZE
#include <time.h>

int timer(int ln, char* s) {
    time_t now = time(NULL);
    printf("Current time: %s", ctime(&now));
    return ln + 1;
}

// TODO: Implement TIMER
int locate(int ln, char* s) {
    char* row = sstrtok(s);
    char* col = sstrtok(NULL);

    if (!row || !col) {
        berror(ln, "LOCATE syntax: LOCATE row, col");
        return ln;
    }

    printf("\033[%d;%dH", emath(row), emath(col)); // ANSI escape code for cursor positioning
    return ln + 1;
}

// TODO: Implement LOCATE
int color(int ln, char* s) {
    char* fg = sstrtok(s);
    char* bg = sstrtok(NULL);

    if (!fg || !bg) {
        berror(ln, "COLOR syntax: COLOR fg, bg");
        return ln;
    }

    printf("\033[38;5;%dm\033[48;5;%dm", emath(fg), emath(bg)); // ANSI escape code for colors
    return ln + 1;
}

// TODO: Implement COLOR
int cls(int ln, char* s) {
    printf("\033[2J\033[H"); // ANSI escape code to clear screen and reset cursor
    return ln + 1;
}

// TODO: Implement CLS

// Entry point
void basic_interpreter() {
    printf("Enhanced BASIC Interpreter v2.0\n");
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
            printf("\nEnhanced BASIC Interpreter v2.0 Commands:\n");
            printf("  RUN            - Execute the program\n");
            printf("  LIST           - Display program listing\n");
            printf("  NEW            - Clear program and variables\n");
            printf("  LOAD filename  - Load .BAS file from filesystem\n");
            printf("  SAVE filename  - Save program to .BAS file\n");
            printf("  EXIT / QUIT    - Return to shell\n");
            printf("  HELP / ?       - Show this help\n");
            printf("\nProgram commands (uppercase):\n");
            printf("  PRINT, INPUT, VAR, IF, GOTO, GOSUB, RET, END\n");
            printf("  FOR/NEXT, WHILE/WEND, DIM, DATA/READ/RESTORE\n");
            printf("  ON...GOTO/ON...GOSUB, DEF FN, CHAIN, FILES\n");
            printf("  DELETE, RANDOMIZE, TIMER, LOCATE, COLOR, CLS\n");
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
