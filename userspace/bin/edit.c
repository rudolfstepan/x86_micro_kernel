#include "x86os.h"

/*
 * Experimental full-screen editor. The basic editing workflow works, but the
 * program is not finished yet; richer prompts,
 * selection/clipboard support and broader hardware testing are still open.
 */

#define MAX_LINES 200
#define LINE_CAPACITY 256
#define SCREEN_COLUMNS 80
#define VIEW_ROWS 21

enum { KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END,
       KEY_DELETE, KEY_PAGE_UP, KEY_PAGE_DOWN };

static char lines[MAX_LINES][LINE_CAPACITY];
static unsigned int line_count = 1;
static unsigned int cursor_line;
static unsigned int cursor_column;
static unsigned int first_line;
static unsigned int first_column;
static int modified;
static char status_text[80] = "^S Save   ^X Exit   ^C Abort   Arrows Move   Backspace/Delete Edit";

static unsigned int text_length(const char *text) {
    unsigned int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static void copy_status(const char *text) {
    unsigned int index = 0;
    while (text[index] && index + 1U < sizeof(status_text)) {
        status_text[index] = text[index];
        ++index;
    }
    status_text[index] = '\0';
}

static void copy_line(char *destination, const char *source) {
    unsigned int index = 0;
    while (source[index] && index + 1U < LINE_CAPACITY) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void draw_padded(unsigned int row_number, const char *text) {
    char row[SCREEN_COLUMNS];
    unsigned int column = 0;
    while (text[column] && column < SCREEN_COLUMNS - 1U) {
        row[column] = text[column];
        ++column;
    }
    while (column < SCREEN_COLUMNS - 1U) row[column++] = ' ';
    row[column] = '\0';
    x86os_draw_text(0, row_number, row, SCREEN_COLUMNS - 1U);
}

static void keep_cursor_visible(void) {
    if (cursor_line < first_line) first_line = cursor_line;
    if (cursor_line >= first_line + VIEW_ROWS)
        first_line = cursor_line - VIEW_ROWS + 1U;
    if (cursor_column < first_column) first_column = cursor_column;
    if (cursor_column >= first_column + SCREEN_COLUMNS - 1U)
        first_column = cursor_column - SCREEN_COLUMNS + 2U;
}

static void redraw(const char *path) {
    keep_cursor_visible();
    x86os_clear();
    const char *title = "  x86 nano 1.0                    File: ";
    char header[SCREEN_COLUMNS];
    unsigned int title_column = 0;
    while (*title && title_column < SCREEN_COLUMNS - 1U) {
        header[title_column++] = *title++;
    }
    while (*path && title_column < SCREEN_COLUMNS - 3U) {
        header[title_column++] = *path++;
    }
    if (modified && title_column < SCREEN_COLUMNS - 2U) {
        header[title_column++] = ' ';
        header[title_column++] = '*';
    }
    while (title_column < SCREEN_COLUMNS - 1U) header[title_column++] = ' ';
    header[title_column] = '\0';
    x86os_draw_text(0, 0, header, SCREEN_COLUMNS - 1U);

    for (unsigned int row = 0; row < VIEW_ROWS; ++row) {
        unsigned int line = first_line + row;
        unsigned int column = 0;
        char screen_row[SCREEN_COLUMNS];
        if (line < line_count) {
            const char *text = lines[line];
            unsigned int length = text_length(text);
            for (unsigned int index = first_column;
                 index < length && column < SCREEN_COLUMNS - 1U; ++index) {
                screen_row[column++] = text[index];
            }
        }
        while (column < SCREEN_COLUMNS - 1U) screen_row[column++] = ' ';
        screen_row[column] = '\0';
        x86os_draw_text(0, row + 1U, screen_row, SCREEN_COLUMNS - 1U);
    }

    draw_padded(22, status_text);
    draw_padded(23, "^G Help  ^S Write Out  ^X Exit  ^C Abort  ^K Cut Line");
    draw_padded(24, "^O New Line     ^D Delete      ^A Home       ^E End");
    x86os_set_cursor(cursor_column - first_column,
                     cursor_line - first_line + 1U);
}

static int wait_key(void) {
    /* This is the kernel's foreground input path, including Ctrl+C abort.
     * Other Ctrl combinations are returned as ASCII control codes. */
    return x86os_getchar();
}

static int read_key(void) {
    int ch = wait_key();
    if (ch != 0x1B) return ch;
    if (wait_key() != '[') return KEY_NONE;
    ch = wait_key();
    if (ch == 'A') return 0x100 + KEY_UP;
    if (ch == 'B') return 0x100 + KEY_DOWN;
    if (ch == 'C') return 0x100 + KEY_RIGHT;
    if (ch == 'D') return 0x100 + KEY_LEFT;
    if (ch == 'H') return 0x100 + KEY_HOME;
    if (ch == 'F') return 0x100 + KEY_END;
    if (ch == '3' && wait_key() == '~') return 0x100 + KEY_DELETE;
    if (ch == '5' && wait_key() == '~') return 0x100 + KEY_PAGE_UP;
    if (ch == '6' && wait_key() == '~') return 0x100 + KEY_PAGE_DOWN;
    return KEY_NONE;
}

static void insert_character(char ch) {
    char *line = lines[cursor_line];
    unsigned int length = text_length(line);
    if (length + 1U >= LINE_CAPACITY) {
        copy_status("Line is full");
        return;
    }
    for (unsigned int index = length + 1U; index > cursor_column; --index)
        line[index] = line[index - 1U];
    line[cursor_column++] = ch;
    modified = 1;
}

static void insert_newline(void) {
    if (line_count == MAX_LINES) {
        copy_status("File has reached the line limit");
        return;
    }
    for (unsigned int index = line_count; index > cursor_line + 1U; --index)
        copy_line(lines[index], lines[index - 1U]);
    copy_line(lines[cursor_line + 1U], lines[cursor_line] + cursor_column);
    lines[cursor_line][cursor_column] = '\0';
    ++line_count;
    ++cursor_line;
    cursor_column = 0;
    modified = 1;
}

static void delete_at_cursor(void) {
    char *line = lines[cursor_line];
    unsigned int length = text_length(line);
    if (cursor_column < length) {
        for (unsigned int index = cursor_column; index < length; ++index)
            line[index] = line[index + 1U];
    } else if (cursor_line + 1U < line_count &&
               length + text_length(lines[cursor_line + 1U]) < LINE_CAPACITY) {
        copy_line(line + length, lines[cursor_line + 1U]);
        for (unsigned int index = cursor_line + 1U; index + 1U < line_count; ++index)
            copy_line(lines[index], lines[index + 1U]);
        --line_count;
    } else return;
    modified = 1;
}

static void backspace(void) {
    if (cursor_column > 0) {
        --cursor_column;
        delete_at_cursor();
    } else if (cursor_line > 0) {
        unsigned int previous_length = text_length(lines[cursor_line - 1U]);
        if (previous_length + text_length(lines[cursor_line]) >= LINE_CAPACITY) {
            copy_status("Lines are too long to join");
            return;
        }
        --cursor_line;
        cursor_column = previous_length;
        delete_at_cursor();
    }
}

static void cut_line(void) {
    if (line_count == 1) lines[0][0] = '\0';
    else {
        for (unsigned int index = cursor_line; index + 1U < line_count; ++index)
            copy_line(lines[index], lines[index + 1U]);
        --line_count;
        if (cursor_line == line_count) --cursor_line;
    }
    cursor_column = 0;
    modified = 1;
    copy_status("Line cut");
}

static int load_file(const char *path, int *exists) {
    x86os_file_info_t info;
    *exists = x86os_stat(path, &info) == 0;
    if (!*exists) return 0;
    if (info.type != X86OS_FILE) return -1;
    int descriptor = x86os_open(path);
    if (descriptor < 0) return -1;
    line_count = 1;
    unsigned int column = 0;
    char buffer[256];
    int count;
    while ((count = x86os_read(descriptor, buffer, sizeof(buffer))) > 0) {
        for (int index = 0; index < count; ++index) {
            char ch = buffer[index];
            if (ch == '\r') continue;
            if (ch == '\n') {
                if (line_count == MAX_LINES) { (void)x86os_close(descriptor); return -2; }
                ++line_count;
                column = 0;
            } else if (column + 1U < LINE_CAPACITY) {
                lines[line_count - 1U][column++] = ch;
                lines[line_count - 1U][column] = '\0';
            }
        }
    }
    if (count < 0) { (void)x86os_close(descriptor); return -1; }
    return x86os_close(descriptor) < 0 ? -1 : 0;
}

static int write_all(int descriptor, const char *buffer, unsigned int size) {
    unsigned int offset = 0;
    while (offset < size) {
        int count = x86os_write(descriptor, buffer + offset, size - offset);
        if (count <= 0) return -1;
        offset += (unsigned int)count;
    }
    return 0;
}

static int make_temp_path(const char *path, char temp[256]) {
    unsigned int length = text_length(path);
    if (length == 0 || length >= 256U) return -1;
    unsigned int prefix = 0;
    for (unsigned int i = 0; i < length; ++i) {
        if (path[i] == '/' || path[i] == '\\') prefix = i + 1U;
    }
    static const char leaf[] = "RST00000.TMP";
    if (prefix + sizeof(leaf) > 256U) return -1;
    for (unsigned int i = 0; i < prefix; ++i) temp[i] = path[i];
    for (unsigned int i = 0; i < sizeof(leaf); ++i) temp[prefix + i] = leaf[i];
    unsigned int pid = (unsigned int)x86os_getpid();
    for (unsigned int digit = 0; digit < 5U; ++digit) {
        temp[prefix + 7U - digit] = (char)('0' + (pid % 10U));
        pid /= 10U;
    }
    unsigned int i = 0;
    while (path[i] == temp[i] && path[i] != '\0') ++i;
    return path[i] == temp[i] ? -1 : 0;
}

static int save_file(const char *path, int *exists) {
    char temp[256];
    if (make_temp_path(path, temp) != 0) return -1;
    (void)x86os_unlink(temp);
    int descriptor = x86os_create(temp);
    if (descriptor < 0) return -1;
    for (unsigned int index = 0; index < line_count; ++index) {
        if (write_all(descriptor, lines[index], text_length(lines[index])) != 0 ||
            (index + 1U < line_count && write_all(descriptor, "\r\n", 2) != 0)) {
            (void)x86os_close(descriptor);
            (void)x86os_unlink(temp);
            return -1;
        }
    }
    int sync_result = x86os_fsync(descriptor);
    int close_result = x86os_close(descriptor);
    if (sync_result < 0 || close_result < 0) {
        (void)x86os_unlink(temp);
        return -1;
    }
    if (x86os_rename(temp, path) != 0) {
        (void)x86os_unlink(temp);
        return -1;
    }
    *exists = 1;
    modified = 0;
    copy_status("Wrote file successfully");
    return 0;
}

static void move_cursor(int key) {
    unsigned int length = text_length(lines[cursor_line]);
    if (key == KEY_LEFT) {
        if (cursor_column) --cursor_column;
        else if (cursor_line) {
            --cursor_line;
            cursor_column = text_length(lines[cursor_line]);
        }
    } else if (key == KEY_RIGHT) {
        if (cursor_column < length) ++cursor_column;
        else if (cursor_line + 1U < line_count) { ++cursor_line; cursor_column = 0; }
    } else if (key == KEY_UP && cursor_line) --cursor_line;
    else if (key == KEY_DOWN && cursor_line + 1U < line_count) ++cursor_line;
    else if (key == KEY_HOME) cursor_column = 0;
    else if (key == KEY_END) cursor_column = length;
    else if (key == KEY_PAGE_UP)
        cursor_line = cursor_line > VIEW_ROWS ? cursor_line - VIEW_ROWS : 0;
    else if (key == KEY_PAGE_DOWN) {
        cursor_line += VIEW_ROWS;
        if (cursor_line >= line_count) cursor_line = line_count - 1U;
    }
    length = text_length(lines[cursor_line]);
    if (cursor_column > length) cursor_column = length;
}

int main(int argc, char **argv) {
    if (argc != 2) { x86os_puts("Usage: edit <file>\n"); return 1; }
    int exists = 0;
    int result = load_file(argv[1], &exists);
    if (result != 0) {
        x86os_puts(result == -2 ? "edit: file is too large\n" : "edit: cannot read file\n");
        return 1;
    }
    for (;;) {
        redraw(argv[1]);
        int key = read_key();

        if (key >= 0x100) {
            key -= 0x100;
            if (key == KEY_DELETE) delete_at_cursor();
            else move_cursor(key);
        } else if (key == 19) {
            if (save_file(argv[1], &exists) != 0) copy_status("Error writing file");
        } else if (key == 24) {
            if (!modified) break;
            copy_status("Save modified buffer?  Y Yes  N No  C Cancel");
            redraw(argv[1]);
            int answer = wait_key();
            if (answer == 'y' || answer == 'Y') {
                if (save_file(argv[1], &exists) == 0) break;
                copy_status("Error writing file");
            } else if (answer == 'n' || answer == 'N') break;
        } else if (key == 7) copy_status("^S saves, ^X exits, ^C aborts, arrows move, Enter splits lines");
        else if (key == 11) cut_line();
        else if (key == 1) move_cursor(KEY_HOME);
        else if (key == 5) move_cursor(KEY_END);
        else if (key == 4) delete_at_cursor();
        else if (key == '\b' || key == 0x7F) backspace();
        else if (key == '\r' || key == '\n' || key == 15) insert_newline();
        else if (key >= ' ' && key <= '~') insert_character((char)key);
    }
    x86os_clear();
    return 0;
}
