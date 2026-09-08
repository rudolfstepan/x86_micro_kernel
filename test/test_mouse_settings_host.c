#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reist/mouse_settings.h"
static void step(reist_mouse_settings_t *s,reist_mouse_motion_t *m,int32_t x,int32_t y,
                 uint32_t gen,uint64_t time,int32_t want_x,int32_t want_y) {
    reist_mouse_motion_apply(s,m,gen,time,1,&x,&y); assert(x==want_x && y==want_y);
}
static void native_division_reference(void) {
    static const int32_t deltas[]={INT32_MIN,INT32_MIN+1,-10001,-101,-100,-99,-1,
        1,99,100,101,10001,INT32_MAX-1,INT32_MAX};
    for (uint32_t speed=25;speed<=200;++speed) for (uint32_t adaptive=0;adaptive<=1;++adaptive)
        for (uint32_t i=0;i<sizeof(deltas)/sizeof(deltas[0]);++i) for (int32_t rem=-99;rem<=99;rem+=33) {
            reist_mouse_settings_t s; reist_mouse_settings_defaults(&s);
            s.speed_percent=speed; s.acceleration=adaptive ? REIST_MOUSE_ADAPTIVE : REIST_MOUSE_FLAT;
            uint32_t gain=speed*(adaptive ? 2U : 1U);
            reist_mouse_motion_t m={.generation=1,.clock_valid=1,.previous_ms=1,.remainder_x=rem};
            int32_t x=deltas[i],y=0;
            int64_t wide=(int64_t)x*gain+rem, want=wide/100;
            int32_t residue=(int32_t)(wide%100);
            if (gain==100) { want=x; residue=0; }
            if (want>INT32_MAX) { want=INT32_MAX; residue=0; }
            if (want<INT32_MIN) { want=INT32_MIN; residue=0; }
            reist_mouse_motion_apply(&s,&m,1,2,1,&x,&y);
            assert(x==(int32_t)want && m.remainder_x==residue);
        }
}
int main(void) {
    native_division_reference();
    reist_mouse_settings_t s,defaults;
    reist_mouse_settings_defaults(&defaults); assert(sizeof(defaults)==28);
    reist_config_document_t doc;
    static const char empty[]="schema=reist.input/1\nkeyboard.layout=de\nfuture.option=kept\n";
    assert(!reist_config_parse(empty,sizeof(empty)-1,"reist.input/1",&doc));
    assert(!reist_mouse_settings_parse(&doc,&s) && !memcmp(&s,&defaults,sizeof(s)));
    const char *bad[]={"middle","24","wrong","yes","1001"};
    for (unsigned i=0;i<5;++i) {
        assert(!reist_config_parse(empty,sizeof(empty)-1,"reist.input/1",&doc));
        assert(!reist_config_set(&doc,reist_mouse_keys[i],bad[i]));
        assert(reist_mouse_settings_parse(&doc,&s)==-22 && !memcmp(&s,&defaults,sizeof(s)));
    }
    for (unsigned speed=25;speed<=200;++speed) for (unsigned profile=0;profile<3;++profile) {
        s=defaults; s.speed_percent=speed; s.acceleration=profile;
        s.primary_right=1; s.natural_scroll=1; s.double_click_ms=1000;
        char values[5][16]; assert(!reist_mouse_settings_format(&s,values));
        assert(!reist_config_parse(empty,sizeof(empty)-1,"reist.input/1",&doc));
        for (unsigned i=0;i<5;++i) assert(!reist_config_set(&doc,reist_mouse_keys[i],values[i]));
        reist_mouse_settings_t result; assert(!reist_mouse_settings_parse(&doc,&result));
        assert(!memcmp(&s,&result,sizeof(s)));
        reist_mouse_motion_t state={0};
        step(&s,&state,100,-100,3,1,profile==REIST_MOUSE_OFF ? 100 : (int32_t)speed,
            profile==REIST_MOUSE_OFF ? -100 : -(int32_t)speed);
    }
    s=defaults; reist_mouse_motion_t m={0};
    step(&s,&m,INT32_MIN,INT32_MAX,1,1,INT32_MIN,INT32_MAX);
    s.speed_percent=25;
    for (unsigned i=0;i<3;++i) step(&s,&m,1,-1,1,2+i,0,0);
    step(&s,&m,1,-1,1,5,1,-1);
    step(&s,&m,1,-1,1,6,0,0);
    step(&s,&m,1,-1,2,7,0,0); /* New generation discards old fractions. */
    assert(m.remainder_x==25 && m.remainder_y==-25);
    s.speed_percent=200; step(&s,&m,INT32_MAX,INT32_MIN,3,8,INT32_MAX,INT32_MIN);
    s.acceleration=REIST_MOUSE_ADAPTIVE; m=(reist_mouse_motion_t){0};
    step(&s,&m,100,0,1,10,200,0); /* First sample: base only. */
    step(&s,&m,100,0,1,20,400,0);
    step(&s,&m,100,0,1,20,200,0); /* Equal/nonmonotone clock fallback. */
    step(&s,&m,100,0,1,19,200,0);
    step(&s,&m,100,0,1,300,200,0); /* Long gap cannot accumulate gain. */
    int32_t x=100,y=0; reist_mouse_motion_apply(&s,&m,1,0,0,&x,&y); assert(x==200);
    s.primary_right=1;
    for (uint32_t b=0;b<32;++b) {
        uint32_t mapped=reist_mouse_buttons(&s,b);
        assert((mapped&~3U)==(b&~3U)); assert(reist_mouse_buttons(&s,mapped)==b);
    }
    s.natural_scroll=1;
    assert(reist_mouse_wheel(&s,INT32_MIN)==INT32_MAX && reist_mouse_wheel(&s,1)==-1);
    s.natural_scroll=0; assert(reist_mouse_wheel(&s,INT32_MIN)==INT32_MIN);
    puts("MOUSE_TEST_OK"); return 0;
}
