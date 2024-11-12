#ifndef KEYBOARD_H    /* This is an "include guard" */
#define KEYBOARD_H 

extern void kb_handler(void* r);

// unsigned char get_scancode_from_keyboard();
// unsigned char get_key_press();
// char scancode_to_ascii(unsigned char scancode);

void kb_install();
extern void wait_for_enter();
//int getchar();

#endif