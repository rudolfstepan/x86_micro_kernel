#ifndef REIST_BROWSER_CSS_LAYOUT_HPP
#define REIST_BROWSER_CSS_LAYOUT_HPP
#include <stdint.h>
/* Checked used-value geometry. Inputs and outputs are CSS px, not device px. */
#ifdef __cplusplus
extern "C" {
#endif
int browser_css_box_size(int32_t,int32_t,int32_t,int32_t,int,int32_t *);
#define BROWSER_CSS_ITEMS 128U
typedef struct browser_css_flex_item {
    int32_t basis, minimum, maximum, grow, shrink, before, after;
    int32_t size, position;
    uint32_t line;
} browser_css_flex_item;
/* Direction/align enums stay in the LibCSS consumer; these are used geometry. */
int browser_css_flex_line(browser_css_flex_item *,uint32_t,int32_t,int32_t,uint32_t,uint32_t);
int browser_css_grid_columns(int32_t *,const int32_t *,const int32_t *,uint32_t,int32_t,int32_t);
#ifdef __cplusplus
}
#endif
#endif
