#include "reist/mouse_settings.h"

/* Core SDK code must not require the optional libc. All comparisons below
 * are against short literals; never inspect past their terminating byte. */
static int equal_literal(const char *value, const char *literal) {
    for (uint32_t i=0U; i<16U; ++i) {
        if (value[i]!=literal[i]) return 0;
        if (!literal[i]) return 1;
    }
    return 0;
}
static void copy_literal(char out[16], const char *literal) {
    uint32_t i=0U;
    for (; i<15U && literal[i]; ++i) out[i]=literal[i];
    out[i]=0;
}

const char *const reist_mouse_keys[5] = {"mouse.primary_button", "mouse.speed_percent",
    "mouse.acceleration", "mouse.natural_scroll", "mouse.double_click_ms"};

void reist_mouse_settings_defaults(reist_mouse_settings_t *s) {
    if (s) *s=(reist_mouse_settings_t){REIST_MOUSE_SETTINGS_VERSION,sizeof(*s),0,100,REIST_MOUSE_FLAT,0,500};
}
int reist_mouse_settings_valid(const reist_mouse_settings_t *s) {
    return s && s->version==REIST_MOUSE_SETTINGS_VERSION && s->struct_size==sizeof(*s) &&
        s->primary_right<=1U && s->speed_percent>=25U && s->speed_percent<=200U &&
        s->acceleration<=REIST_MOUSE_OFF && s->natural_scroll<=1U &&
        s->double_click_ms>=200U && s->double_click_ms<=1000U;
}
static int number(const char *s, uint32_t *out) {
    uint32_t value=0U, n=0U;
    for (; n<10U && s[n]; ++n) {
        if (s[n]<'0' || s[n]>'9') return -22;
        uint32_t digit=(uint32_t)(s[n]-'0');
        if (value>(UINT32_MAX-digit)/10U) return -22;
        value=value*10U+digit;
    }
    if (!n || s[n]) return -22;
    *out=value; return 0;
}
int reist_mouse_settings_parse(const reist_config_document_t *doc, reist_mouse_settings_t *s) {
    reist_mouse_settings_defaults(s);
    if (!doc || !s || !equal_literal(doc->schema,"reist.input/1") || doc->entry_count>REIST_CONFIG_ENTRY_CAPACITY) return -22;
    reist_mouse_settings_t candidate=*s;
    for (uint32_t i=0;i<5U;++i) {
        const char *v=reist_config_get(doc,reist_mouse_keys[i]);
        if (!v) continue;
        if (i==0U) {
            if (!equal_literal(v,"left") && !equal_literal(v,"right")) return -22;
            candidate.primary_right=equal_literal(v,"right");
        } else if (i==1U) { if (number(v,&candidate.speed_percent)) return -22; }
        else if (i==2U) {
            if (equal_literal(v,"flat")) candidate.acceleration=REIST_MOUSE_FLAT;
            else if (equal_literal(v,"adaptive")) candidate.acceleration=REIST_MOUSE_ADAPTIVE;
            else if (equal_literal(v,"off")) candidate.acceleration=REIST_MOUSE_OFF;
            else return -22;
        } else if (i==3U) {
            if (!equal_literal(v,"false") && !equal_literal(v,"true")) return -22;
            candidate.natural_scroll=equal_literal(v,"true");
        } else if (number(v,&candidate.double_click_ms)) return -22;
    }
    if (!reist_mouse_settings_valid(&candidate)) return -22;
    *s=candidate; return 0;
}
static void decimal(char out[16], uint32_t value) {
    char reverse[10]; uint32_t n=0U,i=0U;
    do { reverse[n++]=(char)('0'+value%10U); value/=10U; } while (value);
    while (n) out[i++]=reverse[--n];
    out[i]=0;
}
int reist_mouse_settings_format(const reist_mouse_settings_t *s, char values[5][16]) {
    if (!values || !reist_mouse_settings_valid(s)) return -22;
    copy_literal(values[0],s->primary_right ? "right" : "left");
    decimal(values[1],s->speed_percent);
    copy_literal(values[2],s->acceleration==REIST_MOUSE_FLAT ? "flat" : s->acceleration==REIST_MOUSE_ADAPTIVE ? "adaptive" : "off");
    copy_literal(values[3],s->natural_scroll ? "true" : "false");
    decimal(values[4],s->double_click_ms); return 0;
}
static int32_t scaled(int32_t delta, uint32_t gain, int32_t *remainder) {
    /* Split before multiplying: checked wide product, only native i386
     * division. Correct opposite-sign residuals to truncation toward zero. */
    int64_t value=(int64_t)(delta/100)*gain;
    int32_t fraction=(delta%100)*(int32_t)gain+*remainder;
    value+=fraction/100;
    *remainder=fraction%100;
    if (value>0 && *remainder<0) { --value; *remainder+=100; }
    else if (value<0 && *remainder>0) { ++value; *remainder-=100; }
    if (value>INT32_MAX) { *remainder=0; return INT32_MAX; }
    if (value<INT32_MIN) { *remainder=0; return INT32_MIN; }
    return (int32_t)value;
}
void reist_mouse_motion_apply(const reist_mouse_settings_t *s, reist_mouse_motion_t *state,
    uint32_t generation, uint64_t now, uint32_t clock_valid, int32_t *dx, int32_t *dy) {
    if (!s || !state || !dx || !dy) return;
    if (state->generation!=generation) { *state=(reist_mouse_motion_t){0}; state->generation=generation; }
    if (!*dx && !*dy) return;
    uint32_t gain=s->acceleration==REIST_MOUSE_OFF ? 100U : s->speed_percent;
    if (s->acceleration==REIST_MOUSE_ADAPTIVE) {
        if (clock_valid && state->clock_valid && now>state->previous_ms && now-state->previous_ms<=100U) {
            uint32_t x=*dx<0 ? (uint32_t)-(int64_t)*dx : (uint32_t)*dx;
            uint32_t y=*dy<0 ? (uint32_t)-(int64_t)*dy : (uint32_t)*dy;
            uint32_t distance=x>y ? x:y;
            uint32_t interval=(uint32_t)(now-state->previous_ms);
            /* Beyond the upper threshold the exact velocity is irrelevant.
             * Otherwise distance < interval <=100, so this product is safe. */
            uint32_t velocity=distance>=interval ? 1000U : distance*1000U/interval;
            uint32_t extra=velocity<=100U ? 0U : velocity>=1000U ? 100U : (uint32_t)(velocity-100U)/9U;
            gain=gain*(100U+extra)/100U;
        }
        state->clock_valid=clock_valid && (!state->clock_valid || now>state->previous_ms);
        state->previous_ms=now;
    }
    if (gain==100U) { state->remainder_x=state->remainder_y=0; return; }
    *dx=scaled(*dx,gain,&state->remainder_x);
    *dy=scaled(*dy,gain,&state->remainder_y);
}
uint32_t reist_mouse_buttons(const reist_mouse_settings_t *s,uint32_t buttons) {
    return s->primary_right ? (buttons&~3U)|((buttons&1U)<<1)|((buttons&2U)>>1) : buttons;
}
int32_t reist_mouse_wheel(const reist_mouse_settings_t *s,int32_t wheel) {
    return !s->natural_scroll ? wheel : wheel==INT32_MIN ? INT32_MAX : -wheel;
}
