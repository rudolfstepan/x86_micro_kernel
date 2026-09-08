#ifndef REIST_BROWSER_FONT_ENGINE_HPP
#define REIST_BROWSER_FONT_ENGINE_HPP
#include <stdint.h>
#include <stddef.h>
#define BROWSER_FONT_GLYPHS 1024U
#define BROWSER_FONT_ALPHA (512U*1024U)
#define BROWSER_FONT_FLAG 256U
#define BROWSER_FONT_FACE_SHIFT 9U
#define BROWSER_FONT_FACE_MASK (15U<<BROWSER_FONT_FACE_SHIFT)
/* Private profile6, integer CSS-pixel advances; alpha is straight coverage. */
typedef struct browser_font_glyph {
    uint32_t key,offset;
    int16_t advance,left,top;
    uint16_t width,height,reserved;
} browser_font_glyph;
typedef struct browser_font_atlas {
    uint32_t count,used;
    browser_font_glyph glyphs[BROWSER_FONT_GLYPHS];
    uint8_t alpha[BROWSER_FONT_ALPHA];
} browser_font_atlas;
static inline uint32_t browser_font_key(uint32_t face,uint32_t height,uint32_t scalar) {
    return (face<<27)|((height-1)<<21)|scalar;
}
#ifdef __cplusplus
extern "C" {
#endif
int browser_font_begin(browser_font_atlas *,int (*)(void));
void browser_font_finish(void);
int browser_font_get(uint32_t,uint32_t,uint32_t,const browser_font_glyph **);
int browser_font_check(const uint8_t *,size_t);
uint32_t browser_font_live_bytes(void);
uint32_t browser_font_rasterizations(void);
void browser_font_fail_after(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
