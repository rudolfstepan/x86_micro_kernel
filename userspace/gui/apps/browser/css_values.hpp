#ifndef REIST_BROWSER_CSS_VALUES_HPP
#define REIST_BROWSER_CSS_VALUES_HPP
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private LibCSS 0.9.2 token adapter v1. No serialized pointers or CSS lexer. */
enum browser_css_token_type {
    BC_IDENT, BC_AT, BC_HASH, BC_FUNCTION, BC_STRING, BC_BAD_STRING, BC_URI,
    BC_UNICODE, BC_CHAR, BC_NUMBER, BC_PERCENT, BC_DIMENSION, BC_LAST_INTERN,
    BC_CDO, BC_CDC, BC_SPACE, BC_COMMENT, BC_INCLUDES, BC_DASHMATCH,
    BC_PREFIXMATCH, BC_SUFFIXMATCH, BC_SUBSTRINGMATCH, BC_EOF
};
typedef struct browser_css_token { uint32_t type; void *string; } browser_css_token;
typedef struct browser_css_values browser_css_values;
enum browser_css_length_unit { BC_PX, BC_EM, BC_REM, BC_PERCENT_UNIT, BC_FR, BC_VW, BC_VH };
typedef struct browser_css_length { int32_t value; uint32_t unit; } browser_css_length;
typedef struct browser_css_track { browser_css_length minimum, maximum; } browser_css_track;
typedef struct browser_css_extra {
    uint32_t grid, tracks, auto_fit;
    browser_css_length row_gap, column_gap, radius;
    browser_css_track columns[16];
    int32_t shadow_x, shadow_y, shadow_blur, shadow_spread;
    uint32_t shadow_color;
} browser_css_extra;
const browser_css_extra *browser_css_values_extra(const browser_css_values *);
void browser_css_values_reset(int (*budget)(void));
void browser_css_values_release(void);
int browser_css_values_active(void);
int browser_css_values_collecting(void);
int browser_css_values_capture(const char *,size_t,const browser_css_token *,uint32_t,uint32_t *,uint32_t *);
int browser_css_values_begin(browser_css_values **,browser_css_values *);
int browser_css_values_resolve(void);
void browser_css_values_destroy(browser_css_values *);
/* Called at the original upstream bytecode position, with upstream priority. */
int browser_css_values_apply(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,void *,void **);
/* Real upstream property handlers consume substituted tokens, never text. */
int css_reist_parse_value(void *,const char *,size_t,const browser_css_token *,uint32_t,uint32_t,void **);
int css_reist_parse_color(void *,const browser_css_token *,uint32_t,uint32_t *,uint32_t *);
#ifdef __cplusplus
}
#endif
#endif
