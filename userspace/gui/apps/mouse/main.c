/** Dedicated Ring-3 Mouse applet. Saved policy is not applied mid-session. */
#include "mouse_model.h"
#include "reist/gui/control.h"
#include "reist/gui/value_controls.h"
#include "reist/gui/surface_client.h"
#include <string.h>

#define FACE 0x00C8C8C8U
#define WHITE 0x00FFFFFFU
#define DARK 0x00202020U
#define BLUE 0x00000088U
typedef struct mouse_app {
    mouse_model_t model;
    reist_gui_surface_client_t client;
    reist_gui_control_t buttons[9];
    reist_gui_control_model_t controls;
    reist_gui_control_state_t control;
    reist_gui_range_model_t ranges[2];
    reist_gui_range_state_t range[2];
    uint32_t focus, usable, redraw, close, fault_probe, painted, reported;
    uint64_t last_test_ms;
    const char *test_label;
    reist_mouse_settings_t reported_settings;
} mouse_app_t;
static mouse_app_t app;
static const uint32_t focus_ids[]={10,1,2,3,4,5,11,6,7,8,9};

static void focus(uint32_t index) {
    app.focus=index;
    uint32_t id=focus_ids[index];
    for (uint32_t i=0;i<2;++i) {
        reist_gui_value_event_t event; reist_gui_value_event_initialize(&event);
        event.type=REIST_GUI_VALUE_EVENT_FOCUS; event.focused=id==10+i;
        reist_gui_value_result_t result; reist_gui_value_result_initialize(&result);
        (void)reist_gui_range_dispatch(&app.ranges[i],&app.range[i],&event,&result);
    }
    if (id<10) {
        reist_gui_control_result_t result; reist_gui_control_result_initialize(&result);
        (void)reist_gui_control_focus(&app.controls,&app.control,id,REIST_GUI_CONTROL_FOCUS_KEYBOARD,&result);
    }
    app.redraw=1;
}
static int layout(void) {
    uint32_t w=app.client.width,h=app.client.height;
    app.usable=w>=520 && h>=420;
    if (!app.usable) return 0;
    static const char *const labels[]={"Primaertaste rechts","Linear","Adaptiv","Aus (1:1)",
        "Natuerlich scrollen","Doppelklick hier testen","Speichern","Standard","Schliessen"};
    for (uint32_t i=0;i<9;++i) {
        reist_gui_rect_t rect={16,94,w-32,26};
        if (i>=1 && i<=3) rect=(reist_gui_rect_t){16+(int32_t)(i-1)*(int32_t)((w-32)/3),152,(w-38)/3,26};
        if (i==4) rect.y=193;
        if (i==5) rect=(reist_gui_rect_t){16,296,w-32,44};
        if (i>=6) rect=(reist_gui_rect_t){16+(int32_t)(i-6)*(int32_t)((w-32)/3),(int32_t)h-68,(w-44)/3,30};
        app.buttons[i]=(reist_gui_control_t){.id=i+1,.label=labels[i],.action=i+1,.bounds=rect,
            .role=i>=5 ? REIST_GUI_CONTROL_ROLE_PUSH_BUTTON :
                  i==0 || i==4 ? REIST_GUI_CONTROL_ROLE_CHECKBOX : REIST_GUI_CONTROL_ROLE_RADIO_BUTTON,
            .group=i>=1 && i<=3 ? 1U : 0U,
            .flags=REIST_GUI_CONTROL_VISIBLE|REIST_GUI_CONTROL_ENABLED};
        app.buttons[i].initial_check=i==0 ? app.model.draft.primary_right : i==4 ? app.model.draft.natural_scroll :
            i>=1 && i<=3 ? app.model.draft.acceleration==i-1 : 0U;
    }
    app.controls=(reist_gui_control_model_t){.version=REIST_GUI_CONTROL_API_VERSION,
        .struct_size=sizeof(app.controls),.controls=app.buttons,.control_count=9,.surface_width=w,.surface_height=h};
    reist_gui_control_state_initialize(&app.control);
    reist_gui_control_result_t result; reist_gui_control_result_initialize(&result);
    if (reist_gui_control_configure(&app.controls,&app.control,&result)) return -22;
    for (uint32_t i=0;i<2;++i) {
        app.ranges[i]=(reist_gui_range_model_t){.version=REIST_GUI_VALUE_API_VERSION,
            .struct_size=sizeof(app.ranges[i]),.id=10+i,.name=i ? "Doppelklickzeit" : "Geschwindigkeit",
            .bounds={230,i ? 244 : 48,w-260,24},.minimum=i ? 200 : 25,.maximum=i ? 1000 : 200,
            .step=i ? 50U : 25U,.page_step=i ? 100U : 25U,.role=REIST_GUI_RANGE_SLIDER,
            .orientation=REIST_GUI_HORIZONTAL,.flags=REIST_GUI_VALUE_VISIBLE|REIST_GUI_VALUE_ENABLED};
        reist_gui_range_state_initialize(&app.range[i]);
        reist_gui_value_result_t value; reist_gui_value_result_initialize(&value);
        if (reist_gui_range_configure(&app.ranges[i],&app.range[i],
            (int32_t)(i ? app.model.draft.double_click_ms : app.model.draft.speed_percent),&value)) return -22;
    }
    focus(app.focus); return 0;
}
static int fill(reist_gui_rect_t rect,uint32_t color) {
    return reist_gui_surface_client_paint_fill(&app.client,rect,color);
}
static int text(int32_t x,int32_t y,uint32_t width,const char *label,uint32_t color) {
    size_t n=0; while (n<REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY && label[n]) ++n;
    return reist_gui_surface_client_paint_text(&app.client,x,y,width,label,(uint32_t)n,color,FACE);
}
static int bevel(reist_gui_rect_t r,uint32_t inset) {
    uint32_t a=inset ? 0x00808080U : WHITE,b=inset ? WHITE : DARK;
    return fill((reist_gui_rect_t){r.x,r.y,r.width,2},a) || fill((reist_gui_rect_t){r.x,r.y,2,r.height},a) ||
        fill((reist_gui_rect_t){r.x,r.y+(int32_t)r.height-2,r.width,2},b) ||
        fill((reist_gui_rect_t){r.x+(int32_t)r.width-2,r.y,2,r.height},b);
}
static void report_settings(const char *marker,const reist_mouse_settings_t *s) {
    x86os_puts(marker);
    x86os_puts(" speed="); x86os_print_number((int)s->speed_percent);
    x86os_puts(" profile="); x86os_print_number((int)s->acceleration);
    x86os_puts(" right="); x86os_print_number((int)s->primary_right);
    x86os_puts(" natural="); x86os_print_number((int)s->natural_scroll);
    x86os_puts(" double="); x86os_print_number((int)s->double_click_ms); x86os_putchar('\n');
}
static int paint(void) {
    if (reist_gui_surface_client_paint_begin(&app.client) ||
        fill((reist_gui_rect_t){0,0,app.client.width,app.client.height},FACE)) return -5;
    if (!app.usable) {
        if (text(0,0,app.client.width,"Fenster vergroessern; Esc schliesst",DARK)) return -5;
        return reist_gui_surface_client_paint_commit(&app.client);
    }
    char values[5][16];
    if (reist_mouse_settings_format(&app.model.draft,values)) return -22;
    if (text(16,14,app.client.width-32,"Wirksam beim naechsten Desktopstart",DARK) ||
        text(16,52,160,"Geschwindigkeit (%)",DARK) || text(180,52,48,values[1],DARK) ||
        text(16,130,200,"Beschleunigung",DARK) || text(16,248,160,"Doppelklick (ms)",DARK) ||
        text(180,248,48,values[4],DARK)) return -5;
    for (uint32_t i=0;i<2;++i) {
        reist_gui_rect_t r=app.ranges[i].bounds;
        uint32_t offset=(uint32_t)(app.range[i].value-app.ranges[i].minimum)*(r.width-1)/
            (uint32_t)(app.ranges[i].maximum-app.ranges[i].minimum);
        /* Thumb centre follows the public range controller's full-width map. */
        int32_t thumb=r.x+(int32_t)offset-5;
        if (thumb<r.x) thumb=r.x;
        if (thumb>r.x+(int32_t)r.width-10) thumb=r.x+(int32_t)r.width-10;
        if (fill((reist_gui_rect_t){r.x,r.y+11,r.width,3},DARK) ||
            fill((reist_gui_rect_t){thumb,r.y,10,24},FACE) || bevel((reist_gui_rect_t){thumb,r.y,10,24},0)) return -5;
        if (focus_ids[app.focus]==10+i && bevel((reist_gui_rect_t){r.x-3,r.y-3,r.width+6,r.height+6},1)) return -5;
    }
    for (uint32_t i=0;i<9;++i) {
        reist_gui_rect_t r=app.buttons[i].bounds;
        uint32_t down=app.control.captured==i && app.control.armed;
        const char *label=i==5 ? app.test_label : app.buttons[i].label;
        if (i<5) {
            reist_gui_rect_t box={r.x,r.y+4,18,18};
            if (fill(box,WHITE) || bevel(box,1)) return -5;
            if (app.control.check[i] && fill((reist_gui_rect_t){r.x+5,r.y+9,8,8},DARK)) return -5;
            if (text(r.x+26,r.y+6,r.width-26,label,DARK)) return -5;
        } else if (bevel(r,down) || text(r.x+10+(int32_t)down,r.y+7+(int32_t)down,r.width-20,label,
            i==6 && (!app.model.writable || app.model.child) ? 0x00808080U : DARK)) return -5;
        if (focus_ids[app.focus]==i+1 && bevel((reist_gui_rect_t){r.x+2,r.y+2,r.width-4,r.height-4},1)) return -5;
    }
    if (text(16,(int32_t)app.client.height-25,app.client.width-32,app.model.status,DARK)) return -5;
    int status=reist_gui_surface_client_paint_commit(&app.client);
    if (!status && app.fault_probe && (!app.reported || memcmp(&app.reported_settings,&app.model.draft,sizeof(app.model.draft)))) {
        report_settings("MOUSE_DRAFT_READY",&app.model.draft);
        app.reported_settings=app.model.draft; app.reported=1;
    }
    return status;
}
static int inside(reist_gui_rect_t r,int32_t x,int32_t y) {
    return x>=r.x && y>=r.y && (uint32_t)(x-r.x)<r.width && (uint32_t)(y-r.y)<r.height;
}
static void activate(uint32_t action) {
    if (action==1) app.model.draft.primary_right=app.control.check[0];
    else if (action>=2 && action<=4) app.model.draft.acceleration=action-2;
    else if (action==5) app.model.draft.natural_scroll=app.control.check[4];
    else if (action==6) {
        uint64_t now=0;
        if (!x86os_monotonic_ms(&now) && app.last_test_ms && now>=app.last_test_ms &&
            now-app.last_test_ms<=app.model.draft.double_click_ms) {
            app.test_label="Doppelklick erkannt"; app.last_test_ms=0;
            if (app.fault_probe) x86os_puts("MOUSE_TEST_DOUBLE_OK\n");
        } else { app.test_label="Noch einmal klicken"; app.last_test_ms=now; }
    } else if (action==7) (void)mouse_model_save(&app.model);
    else if (action==8) { reist_mouse_settings_defaults(&app.model.draft); app.last_test_ms=0; (void)layout(); }
    else if (action==9) app.close=1;
    app.redraw=1;
}
static void input(const reist_gui_surface_input_t *in) {
    if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed) {
        if (in->key==27 || in->key==0x101) { app.close=1; return; }
        if (in->key==7 && app.fault_probe && !app.model.child) {
            x86os_puts("MOUSE_APPLET_FAULT\n"); __asm__ __volatile__("ud2"); return;
        }
        if (app.usable && in->key==9) { focus((app.focus+1)%11); return; }
    }
    if (!app.usable) return;
    uint32_t scroll=in->type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL && reist_gui_surface_scroll_valid(in);
    if (scroll && app.fault_probe) {
        x86os_puts("MOUSE_WHEEL delta="); x86os_print_number(in->delta_y); x86os_putchar('\n');
    }
    if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON && in->button==1 && in->pressed) {
        for (uint32_t i=0;i<11;++i) {
            uint32_t id=focus_ids[i];
            reist_gui_rect_t r=id>=10 ? app.ranges[id-10].bounds : app.buttons[id-1].bounds;
            if (inside(r,in->x,in->y)) { focus(i); break; }
        }
    }
    for (uint32_t i=0;i<2;++i) {
        reist_gui_value_event_t event; reist_gui_value_event_initialize(&event);
        if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed && focus_ids[app.focus]==10+i) {
            event.type=REIST_GUI_VALUE_EVENT_KEYBOARD;
            if (in->key==0x104 || in->key==0x103) event.key=REIST_GUI_VALUE_KEY_LEFT;
            else if (in->key==0x105 || in->key==0x102) event.key=REIST_GUI_VALUE_KEY_RIGHT;
            else continue;
        } else if (scroll && inside(app.ranges[i].bounds,in->x,in->y)) {
            focus(i ? 6 : 0); event.type=REIST_GUI_VALUE_EVENT_KEYBOARD;
            event.key=in->delta_y<0 ? REIST_GUI_VALUE_KEY_RIGHT : REIST_GUI_VALUE_KEY_LEFT;
        } else if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON && in->button==1) {
            event.type=REIST_GUI_VALUE_EVENT_POINTER_BUTTON; event.button=1; event.pressed=in->pressed;
            event.x=in->x; event.y=in->y;
        } else if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
            event.type=REIST_GUI_VALUE_EVENT_POINTER_MOTION; event.x=in->x; event.y=in->y;
        } else continue;
        reist_gui_value_result_t result; reist_gui_value_result_initialize(&result);
        if (!reist_gui_range_dispatch(&app.ranges[i],&app.range[i],&event,&result) && result.changed) {
            if (i) app.model.draft.double_click_ms=(uint32_t)app.range[i].value;
            else app.model.draft.speed_percent=(uint32_t)app.range[i].value;
            app.redraw=1;
        }
    }
    reist_gui_control_event_t event; reist_gui_control_event_initialize(&event);
    if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed && focus_ids[app.focus]<10 &&
        (in->key==13 || in->key==10 || in->key==' ')) {
        event.type=REIST_GUI_CONTROL_EVENT_KEYBOARD; /* Logical key; no pointer edge. */
        event.key=in->key==' ' ? REIST_GUI_CONTROL_KEY_SPACE : REIST_GUI_CONTROL_KEY_ENTER;
    } else if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON && in->button==1) {
        event.type=REIST_GUI_CONTROL_EVENT_POINTER_BUTTON; event.button=1; event.pressed=in->pressed;
        event.x=in->x; event.y=in->y;
    } else if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        event.type=REIST_GUI_CONTROL_EVENT_POINTER_MOTION; event.x=in->x; event.y=in->y;
    } else return;
    reist_gui_control_result_t result; reist_gui_control_result_initialize(&result);
    if (!reist_gui_control_dispatch(&app.controls,&app.control,&event,&result)) {
        if (result.damage_count || result.full_redraw) app.redraw=1;
        if (result.activated) activate(result.action);
    }
}
int main(int argc,char **argv) {
    x86os_ipc_handle_t endpoint=0;
    for (int i=1;i<argc;++i) if (!strcmp(argv[i],"--fault-probe")) app.fault_probe=1;
    if (reist_gui_surface_endpoint_from_argv(argc,argv,&endpoint)) {
        x86os_puts("MOUSE_COMMAND_READY\nMaus: Systemsteuerung > Maus\n");
        mouse_model_initialize(&app.model);
        if (argc==2 && !strcmp(argv[1],"--list")) report_settings("MOUSE_SETTINGS_SAVED_PROFILE",&app.model.saved);
        return app.model.writable ? 0 : 1;
    }
    if (reist_gui_surface_client_init(&app.client,endpoint)) return 1;
    int status=-9;
    for (uint32_t attempt=0;attempt<250;++attempt) {
        status=reist_gui_surface_client_create(&app.client,REIST_GUI_SURFACE_ROLE_TOPLEVEL,620,452);
        if (!status || (status!=-9 && status!=-13)) break;
        (void)x86os_sleep_ms(1);
    }
    if (status || reist_gui_surface_client_ack_configure(&app.client,app.client.configured_serial) ||
        reist_gui_surface_client_set_title(&app.client,"Maus") || reist_gui_surface_client_enable_scroll(&app.client)) goto cleanup;
    mouse_model_initialize(&app.model); app.test_label="Doppelklick hier testen";
    if (layout()) goto cleanup;
    app.redraw=1; x86os_puts("MOUSE_APPLET_READY\n");
    for (;;) {
        if (mouse_model_poll(&app.model)) app.redraw=1;
        if (app.model.fatal) { status=-5; break; }
        if (app.close) {
            if (!app.model.child) break;
            (void)mouse_model_cancel(&app.model);
        }
        if (app.redraw) {
            int painted=-11;
            for (uint32_t attempt=0;attempt<20;++attempt) {
                painted=paint(); if (!painted) break;
                (void)x86os_sleep_ms(5);
            }
            if (painted) { status=painted; break; }
            if (!app.painted) { app.painted=1; x86os_puts("MOUSE_APPLET_PAINTED\n"); }
            app.redraw=0;
        }
        reist_gui_surface_message_t message;
        status=reist_gui_surface_client_receive(&app.client,&message,0);
        if (status==-11) { (void)x86os_sleep_ms(5); continue; }
        if (status) break;
        if (message.type==REIST_GUI_SURFACE_CLOSE) app.close=1;
        else if (message.type==REIST_GUI_SURFACE_CONFIGURE) {
            if (reist_gui_surface_client_accept_configure(&app.client,&message) || layout()) break;
            app.redraw=1;
        } else if (message.type==REIST_GUI_SURFACE_INPUT) input(&message.input);
    }
cleanup:
    (void)mouse_model_cancel(&app.model);
    (void)mouse_model_poll(&app.model);
    (void)reist_gui_surface_client_destroy(&app.client);
    (void)x86os_ipc_release(endpoint);
    x86os_puts("MOUSE_APPLET_CLOSED\n");
    return status && status!=-11 ? 1 : 0;
}
