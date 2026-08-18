#ifndef REIST_DISPLAY_CONTROL_H
#define REIST_DISPLAY_CONTROL_H

#include <stdint.h>

#define DISPLAY_CONTROL_ABI_VERSION 1U
#define DISPLAY_CONTROL_ACTIVATE 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
} display_control_request_t;

int display_control_activate(void);

#endif
