#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "browser_forms.h"
static browser_forms_t m;
static browser_form_state_t s;
static browser_forms_t other;
static uint32_t string(const char *v) {
    uint32_t o=m.used; size_t n=strlen(v)+1;
    memcpy(m.strings+o,v,n); m.used+=(uint32_t)n; return o;
}
static void control(uint32_t kind,const char *name,const char *value,uint32_t flags) {
    browser_form_control_t *c=&m.controls[m.control_count++];
    c->kind=kind; c->owner=0; c->flags=flags; c->name=string(name);
    c->value=string(value); c->target=BROWSER_FORM_NONE;
}
int main(void) {
    m.version=BROWSER_FORMS_VERSION; m.used=1; m.form_count=1;
    m.forms[0].action=string("/find?discard=yes#result");
    control(BROWSER_FORM_TEXT,"q","caf\xc3\xa9 &+",0);
    control(BROWSER_FORM_HIDDEN,"q","second",0);
    control(BROWSER_FORM_CHECKBOX,"yes","on",BROWSER_FORM_CHECKED);
    control(BROWSER_FORM_CHECKBOX,"no","on",0);
    control(BROWSER_FORM_TEXTAREA,"lines","a\nb\rc\r\nd",0);
    control(BROWSER_FORM_SUBMIT,"go","Go",0);
    control(BROWSER_FORM_SUBMIT,"other","No",0);
    control(BROWSER_FORM_TEXT,"disabled","No",BROWSER_FORM_DISABLED);
    control(BROWSER_FORM_HIDDEN,"_charset_","wrong",0);
    assert(!browser_forms_validate(&m));
    assert(!browser_forms_bind(&m,NULL,&s,1,0));
    char url[256]="unchanged";
    assert(!browser_forms_submit(&m,&s,1,5,"https://example.org/start",url,sizeof(url)));
    assert(!strcmp(url,"https://example.org/find?q=caf%C3%A9+%26%2B&q=second&yes=on&lines=a%0D%0Ab%0D%0Ac%0D%0Ad&go=Go&_charset_=UTF-8#result"));
    assert(browser_forms_submit(&m,&s,2,5,"https://example.org/",url,sizeof(url))<0);
    assert(!browser_forms_focus(&m,&s,0));
    assert(browser_forms_key(&m,&s,8)==1);
    assert(!strcmp(browser_forms_value(&m,&s,0),"caf\xc3\xa9 &"));
    assert(!browser_forms_bind(&m,&m,&s,1,1));
    assert(s.focus==0 && !strcmp(browser_forms_value(&m,&s,0),"caf\xc3\xa9 &"));
    assert(!browser_forms_reset(&m,&s,0));
    assert(!strcmp(browser_forms_value(&m,&s,0),"caf\xc3\xa9 &+"));
    m.forms[0].blocked=1;
    strcpy(url,"unchanged");
    assert(browser_forms_submit(&m,&s,1,5,"https://example.org/",url,sizeof(url))<0);
    assert(!strcmp(url,"unchanged")); m.forms[0].blocked=0;
    assert(browser_forms_submit(&m,&s,1,5,"https://example.org/",url,12)<0);
    assert(!strcmp(url,"unchanged"));
    /* Reflow mismatch is rejected before mutating values/focus. */
    other=m; other.forms[0].blocked=1;
    uint32_t focus=s.focus;
    assert(browser_forms_bind(&other,&m,&s,1,1)<0 && s.focus==focus);
    assert(!browser_forms_bind(&m,NULL,&s,2,0) && s.focus==BROWSER_FORM_NONE);
    assert(browser_forms_submit(&m,&s,1,5,"https://example.org/",url,sizeof(url))<0);
    assert(!browser_forms_focus(&m,&s,0));
    /* Scalar deletion never cuts the two-byte e-acute. */
    assert(browser_forms_key(&m,&s,262)==1);
    for(unsigned i=0;i<4;++i) assert(browser_forms_key(&m,&s,261)==1);
    assert(s.cursor==5 && browser_forms_key(&m,&s,8)==1);
    assert(!strcmp(browser_forms_value(&m,&s,0),"caf &+"));
    m.controls[0].flags|=BROWSER_FORM_READONLY;
    assert(browser_forms_key(&m,&s,'x')==1 && !strcmp(browser_forms_value(&m,&s,0),"caf &+"));
    m.controls[0].flags&=~BROWSER_FORM_READONLY;
    m.controls[7].flags|=BROWSER_FORM_BLOCKED; /* disabled unsupported control is unsuccessful */
    assert(!browser_forms_submit(&m,&s,2,5,"https://example.org/",url,sizeof(url)));
    m.controls[0].flags|=BROWSER_FORM_BLOCKED;
    strcpy(url,"unchanged");
    assert(browser_forms_submit(&m,&s,2,5,"https://example.org/",url,sizeof(url))==-95 && !strcmp(url,"unchanged"));
    m.controls[0].flags&=~BROWSER_FORM_BLOCKED;
    /* Existing URL admission may reject credentials before form policy. Both
     * paths must preserve the candidate buffer and acquire no transport. */
    assert(browser_forms_submit(&m,&s,2,5,"https://user:password@example.org/",url,sizeof(url))<0 && !strcmp(url,"unchanged"));
    uint32_t action=m.forms[0].action;
    m.forms[0].action=string("javascript:bad()");
    assert(browser_forms_submit(&m,&s,2,5,"https://example.org/",url,sizeof(url))<0);
    m.forms[0].action=action;
    /* A malformed worker string/relationship cannot be admitted. */
    uint32_t saved=m.controls[0].name;
    m.controls[0].name=saved+1; assert(browser_forms_validate(&m)<0); m.controls[0].name=saved;
    m.strings[saved]=(char)0xc0; assert(browser_forms_validate(&m)<0); m.strings[saved]='q';
    m.controls[0].name=m.used;
    assert(browser_forms_validate(&m)<0);
    m.controls[0].name=0; m.control_count=BROWSER_FORM_CONTROLS+1;
    assert(browser_forms_validate(&m)<0);
    /* Full fixed admission, not a 16-widget or 64-byte per-value shortcut. */
    memset(&m,0,sizeof(m)); m.version=1; m.used=1; m.form_count=BROWSER_FORM_COUNT;
    for(uint32_t i=0;i<BROWSER_FORM_CONTROLS;++i) control(BROWSER_FORM_TEXT,"n","",0);
    assert(!browser_forms_validate(&m) && !browser_forms_bind(&m,NULL,&s,3,0));
    assert(!browser_forms_focus(&m,&s,0));
    for(uint32_t i=0;i<BROWSER_FORM_BYTES-BROWSER_FORM_CONTROLS;++i) assert(browser_forms_key(&m,&s,'a')==1);
    assert(s.used==BROWSER_FORM_BYTES && browser_forms_key(&m,&s,'b')==-28);
    assert(s.used==BROWSER_FORM_BYTES && s.values[0]=='a');
    assert(!browser_forms_reset(&m,&s,0) && s.used==BROWSER_FORM_CONTROLS);
    m.form_count=BROWSER_FORM_COUNT+1; assert(browser_forms_validate(&m)<0);
    puts("BROWSER_FORMS_OK"); return 0;
}
