#ifndef BROWSER_FORMS_H
#define BROWSER_FORMS_H
#include <stddef.h>
#include <stdint.h>
#define BROWSER_FORMS_LEGACY_VERSION 1U
#define BROWSER_FORMS_VERSION 2U
#define BROWSER_FORM_COUNT 16U
#define BROWSER_FORM_CONTROLS 256U
#define BROWSER_FORM_OPTIONS 512U
#define BROWSER_FORM_BYTES (128U*1024U)
#define BROWSER_FORM_NONE UINT32_MAX
enum browser_form_kind {
    BROWSER_FORM_TEXT=1, BROWSER_FORM_HIDDEN, BROWSER_FORM_CHECKBOX,
    BROWSER_FORM_RADIO, BROWSER_FORM_TEXTAREA, BROWSER_FORM_SELECT,
    BROWSER_FORM_SUBMIT, BROWSER_FORM_RESET, BROWSER_FORM_BUTTON,
    BROWSER_FORM_LABEL, BROWSER_FORM_UNSUPPORTED
};
enum browser_form_flags {
    BROWSER_FORM_DISABLED=1, BROWSER_FORM_READONLY=2, BROWSER_FORM_CHECKED=4,
    BROWSER_FORM_MULTIPLE=8, BROWSER_FORM_BLOCKED=16
};
typedef struct browser_form { uint32_t action, blocked; } browser_form_t;
typedef struct browser_form_control {
    uint32_t kind, owner, flags, name, value, label, first_option, option_count, target;
} browser_form_control_t;
typedef struct browser_form_option { uint32_t value, label, flags; } browser_form_option_t;
typedef struct browser_forms {
    uint32_t version, form_count, control_count, option_count, used;
    browser_form_t forms[BROWSER_FORM_COUNT];
    browser_form_control_t controls[BROWSER_FORM_CONTROLS];
    browser_form_option_t options[BROWSER_FORM_OPTIONS];
    char strings[BROWSER_FORM_BYTES];
    /* Version 2 compact wire appends one word per live control after strings.
     * Zero is absent; otherwise the UTF-16 unit limit is this value minus one.
     * Limits above the stronger private byte quota saturate at that quota. */
    uint32_t max_length_plus_one[BROWSER_FORM_CONTROLS];
} browser_forms_t;
typedef struct browser_form_state {
    uint32_t generation, focus, capture, cursor, used;
    uint32_t offsets[BROWSER_FORM_CONTROLS], lengths[BROWSER_FORM_CONTROLS];
    uint8_t checked[BROWSER_FORM_CONTROLS], selected[BROWSER_FORM_OPTIONS];
    char values[BROWSER_FORM_BYTES];
    uint32_t units[BROWSER_FORM_CONTROLS];
    uint8_t dirty[BROWSER_FORM_CONTROLS];
} browser_form_state_t;
struct node;
int browser_forms_project(struct node *, browser_forms_t *);
int browser_forms_validate(const browser_forms_t *);
int browser_forms_bind(const browser_forms_t *, const browser_forms_t *, browser_form_state_t *, uint32_t generation, int reflow);
int browser_forms_reset(const browser_forms_t *, browser_form_state_t *, uint32_t owner);
int browser_forms_focus(const browser_forms_t *, browser_form_state_t *, uint32_t index);
/* >0 changed/consumed, 0 unhandled, <0 rejected without losing prior value. */
int browser_forms_key(const browser_forms_t *, browser_form_state_t *, uint32_t key);
int browser_forms_activate(const browser_forms_t *, browser_form_state_t *, uint32_t index);
const char *browser_forms_value(const browser_forms_t *, const browser_form_state_t *, uint32_t index);
int browser_forms_submit(const browser_forms_t *, const browser_form_state_t *, uint32_t generation,
                         uint32_t submitter, const char *base, char *url, size_t capacity);
const char *browser_forms_error(int);
#endif
