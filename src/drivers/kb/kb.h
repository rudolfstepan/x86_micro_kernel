#ifndef KEYBOARD_H    /* This is an "include guard" */
#define KEYBOARD_H 

extern void kb_handler(void* r);

// unsigned char get_scancode_from_keyboard();
// unsigned char get_key_press();
// char scancode_to_ascii(unsigned char scancode);

void kb_install();
void kb_wait_enter();
char getchar();
void get_input_line(char *buffer, int max_len);

#endif