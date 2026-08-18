#ifndef REIST_DISPLAY_CONTROL_H
#define REIST_DISPLAY_CONTROL_H

#include <stdint.h>

#define DISPLAY_CONTROL_ABI_VERSION 1U
#define DISPLAY_CONTROL_ACTIVATE 1U
#define DISPLAY_CONTROL_DEACTIVATE 2U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
} display_control_request_t;

int display_control_activate(void);
int display_control_deactivate(void);
void display_control_prepare(void);
void display_control_present_rect(uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);

#endif
