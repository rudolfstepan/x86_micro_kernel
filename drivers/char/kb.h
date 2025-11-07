#ifndef KEYBOARD_H    /* This is an "include guard" */
#define KEYBOARD_H 

#include <stdbool.h>
#include <stdint.h>

// Special key codes (returned as multi-byte sequences or special values)
#define KEY_ESCAPE      0x01
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_F11         0x57
#define KEY_F12         0x58

// Extended keys (E0 prefix)
#define KEY_UP          0x48
#define KEY_DOWN        0x50
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D
#define KEY_HOME        0x47
#define KEY_END         0x4F
#define KEY_PAGE_UP     0x49
#define KEY_PAGE_DOWN   0x51
#define KEY_INSERT      0x52
#define KEY_DELETE      0x53

// Control keys
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_BACKSPACE   0x08

// Keyboard state structure
typedef struct {
    bool shift_left : 1;
    bool shift_right : 1;
    bool ctrl_left : 1;
    bool ctrl_right : 1;
    bool alt_left : 1;
    bool alt_right : 1;
    bool caps_lock : 1;
    bool num_lock : 1;
    bool scroll_lock : 1;
    bool extended : 1;  // E0 prefix received
} kbd_state_t;

// Public API
extern void kb_handler(void* r);
void kb_install(void);
void kb_wait_enter(void);
char getchar(void);
char getchar_nonblocking(void);
char input_queue_pop(void);
void get_input_line(char *buffer, int max_len);

// Keyboard state queries
bool kb_is_ctrl_pressed(void);
bool kb_is_alt_pressed(void);
bool kb_is_shift_pressed(void);
kbd_state_t kb_get_state(void);

#endif