/** Ring-3 Display applet. No scanout mutation or live-mode authority. */
#include "display_model.h"
#include "reist/gui/control.h"
#include "reist/gui/value_controls.h"
#include "reist/gui/surface_client.h"
#include <string.h>

#define FACE 0x00C8C8C8U
#define WHITE 0x00FFFFFFU
#define DARK 0x00202020U
#define BLUE 0x00000088U
typedef struct display_app {
    display_model_t model;
    reist_gui_surface_client_t client;
    reist_gui_list_item_t items[DISPLAY_CHOICE_CAPACITY];
    reist_gui_list_model_t list;
    reist_gui_list_state_t selection;
    reist_gui_control_t buttons[2];
    reist_gui_control_model_t controls;
    reist_gui_control_state_t control_state;
    uint32_t focus, close_requested, redraw, usable, fault_probe, painted;
    uint32_t selection_reported, reported_selection;
} display_app_t;
static display_app_t app;

static void focus_control(uint32_t focus) {
    app.focus = focus;
    reist_gui_value_event_t event;
    reist_gui_value_event_initialize(&event);
    event.type = REIST_GUI_VALUE_EVENT_FOCUS; event.focused = focus == 0U;
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    (void)reist_gui_list_dispatch(&app.list, &app.selection, &event, &result);
    if (focus) {
        reist_gui_control_result_t output;
        reist_gui_control_result_initialize(&output);
        (void)reist_gui_control_focus(&app.controls, &app.control_state, focus,
                                     REIST_GUI_CONTROL_FOCUS_KEYBOARD, &output);
    }
    app.redraw = 1U;
}

static int layout(void) {
    uint32_t width = app.client.width, height = app.client.height;
    app.usable = width >= 360U && height >= 260U;
    if (!app.usable) return 0;
    for (uint32_t i = 0U; i < app.model.count; ++i)
        app.items[i] = (reist_gui_list_item_t){.id=i+1U,
            .label=app.model.choices[i].value,
            .flags=REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED};
    app.list = (reist_gui_list_model_t){
        .version=REIST_GUI_VALUE_API_VERSION, .struct_size=sizeof(app.list),
        .id=10U, .name="Aufloesung", .items=app.items, .item_count=app.model.count,
        .bounds={18,100,width-36U,((height-190U)/24U)*24U}, .row_height=24U,
        .flags=REIST_GUI_VALUE_VISIBLE | REIST_GUI_VALUE_ENABLED};
    reist_gui_list_state_initialize(&app.selection);
    reist_gui_value_result_t value;
    reist_gui_value_result_initialize(&value);
    if (reist_gui_list_configure(&app.list, &app.selection,
                                 app.model.selected+1U, &value) != 0) return -22;
    for (uint32_t i = 0U; i < 2U; ++i)
        app.buttons[i] = (reist_gui_control_t){.id=i+1U,
            .role=REIST_GUI_CONTROL_ROLE_PUSH_BUTTON,
            .label=i ? "Schliessen" : "Speichern", .action=i+1U,
            .bounds={16+(int32_t)i*172,(int32_t)height-72,156,30},
            .flags=REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED |
                   (i ? 0U : REIST_GUI_CONTROL_DEFAULT)};
    app.controls = (reist_gui_control_model_t){
        .version=REIST_GUI_CONTROL_API_VERSION, .struct_size=sizeof(app.controls),
        .controls=app.buttons,.control_count=2U,.surface_width=width,.surface_height=height};
    reist_gui_control_state_initialize(&app.control_state);
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_configure(&app.controls, &app.control_state, &result) != 0) return -22;
    focus_control(app.focus);
    return 0;
}

static int fill(reist_gui_rect_t rect, uint32_t color) {
    return reist_gui_surface_client_paint_fill(&app.client, rect, color);
}
static int text(int32_t x, int32_t y, uint32_t width,
                const char *label, uint32_t fg, uint32_t bg) {
    size_t length = 0U;
    while (length < REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY && label[length]) ++length;
    return reist_gui_surface_client_paint_text(&app.client, x,y,width,label,(uint32_t)length,fg,bg);
}
static int bevel(reist_gui_rect_t rect, uint32_t inset) {
    uint32_t top = inset ? 0x00808080U : WHITE, bottom = inset ? WHITE : DARK;
    if (fill((reist_gui_rect_t){rect.x,rect.y,rect.width,2},top) ||
        fill((reist_gui_rect_t){rect.x,rect.y,2,rect.height},top) ||
        fill((reist_gui_rect_t){rect.x,rect.y+(int32_t)rect.height-2,rect.width,2},bottom) ||
        fill((reist_gui_rect_t){rect.x+(int32_t)rect.width-2,rect.y,2,rect.height},bottom)) return -5;
    return 0;
}
static int paint(void) {
    if (reist_gui_surface_client_paint_begin(&app.client) != 0) return -5;
    if (fill((reist_gui_rect_t){0,0,app.client.width,app.client.height},FACE)) return -5;
    if (!app.usable) {
        if (text(4,4,app.client.width,"Fenster vergroessern; Esc schliesst",DARK,FACE)) return -5;
        return reist_gui_surface_client_paint_commit(&app.client);
    }
    char active[16], saved[16];
    display_mode_text(active, app.model.caps.width, app.model.caps.height);
    display_mode_text(saved, app.model.saved_width, app.model.saved_height);
    if (text(16,14,104,"Aktiv:",DARK,FACE) || text(128,14,200,active,DARK,FACE) ||
        text(16,38,104,"Gespeichert:",DARK,FACE) || text(128,38,200,saved,DARK,FACE) ||
        text(16,68,app.client.width-32,"32 Bit (24 RGB), unveraendert",DARK,FACE)) return -5;
    reist_gui_rect_t list = app.list.bounds;
    if (fill(list,WHITE) || bevel((reist_gui_rect_t){list.x-2,list.y-2,list.width+4,list.height+4},1U)) return -5;
    uint32_t rows = list.height / app.list.row_height;
    for (uint32_t row=0U; row<rows && row+app.selection.top_index<app.model.count; ++row) {
        uint32_t index = row+app.selection.top_index;
        uint32_t bg = index == app.selection.selected ? BLUE : WHITE;
        int32_t y = list.y+(int32_t)(row*24U);
        if (fill((reist_gui_rect_t){list.x,y,list.width,24},bg) ||
            text(list.x+6,y+4,list.width-12,app.items[index].label,
                 bg == BLUE ? WHITE : DARK,bg)) return -5;
    }
    for (uint32_t i=0U;i<2U;++i) {
        reist_gui_rect_t rect=app.buttons[i].bounds;
        uint32_t down=app.control_state.captured==i && app.control_state.armed;
        if (fill(rect,FACE) || bevel(rect,down) ||
            text(rect.x+12+(int32_t)down,rect.y+7+(int32_t)down,rect.width-24,
                 app.buttons[i].label,
                 i || (app.model.writable && !app.model.child) ? DARK : 0x00808080U,FACE)) return -5;
        if (app.focus==i+1U && bevel((reist_gui_rect_t){rect.x+4,rect.y+4,rect.width-8,rect.height-8},1U)) return -5;
    }
    if (text(16,(int32_t)app.client.height-26,app.client.width-32,app.model.status,DARK,FACE)) return -5;
    return reist_gui_surface_client_paint_commit(&app.client);
}

static int render(void) {
    int status=paint();
    /* QMP admission is not guest consumption. Only diagnostic launches report
     * a changed selection after the compositor accepted its complete paint. */
    if (status==0 && app.fault_probe && app.usable &&
        app.model.selected<app.model.count && (!app.selection_reported ||
            app.reported_selection!=app.model.selected)) {
        x86os_puts("DISPLAY_SELECTION_READY value=");
        x86os_puts(app.model.choices[app.model.selected].value);
        x86os_putchar('\n');
        app.reported_selection=app.model.selected; app.selection_reported=1U;
    }
    return status;
}

static int inside(reist_gui_rect_t r,int32_t x,int32_t y) {
    return x>=r.x && y>=r.y && (uint32_t)(x-r.x)<r.width && (uint32_t)(y-r.y)<r.height;
}
static void list_key(uint32_t key) {
    reist_gui_value_event_t event; reist_gui_value_event_initialize(&event);
    event.type=REIST_GUI_VALUE_EVENT_KEYBOARD; event.key=key;
    reist_gui_value_result_t result; reist_gui_value_result_initialize(&result);
    if (reist_gui_list_dispatch(&app.list,&app.selection,&event,&result)==0 && result.changed) {
        app.model.selected=app.selection.selected; app.redraw=1U;
    }
}
static void input(const reist_gui_surface_input_t *in) {
    if (app.fault_probe && !app.model.child && in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD &&
        in->pressed && in->key==7U) {
        x86os_puts("DISPLAY_APPLET_FAULT\n");
        __asm__ __volatile__("ud2"); /* explicit diagnostic launch + real Ctrl-G only */
        return;
    }
    if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed &&
        (in->key==0x101U || in->key==27U)) { app.close_requested=1U; return; }
    if (!app.usable) return;
    if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed) {
        if (in->key==9U) { focus_control((app.focus+1U)%3U); return; }
        if (!app.focus && (in->key==0x102U || in->key==0x103U)) {
            list_key(in->key==0x102U ? REIST_GUI_VALUE_KEY_UP : REIST_GUI_VALUE_KEY_DOWN); return;
        }
    }
    if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL && reist_gui_surface_scroll_valid(in)) {
        if (inside(app.list.bounds,in->x,in->y) && in->delta_y) {
            focus_control(0U);
            list_key(in->delta_y<0 ? REIST_GUI_VALUE_KEY_UP : REIST_GUI_VALUE_KEY_DOWN);
        }
        return;
    }
    if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON && in->button==1U && in->pressed) {
        if (inside(app.list.bounds,in->x,in->y)) focus_control(0U);
        for (uint32_t i=0;i<2;++i)
            if (inside(app.buttons[i].bounds,in->x,in->y)) focus_control(i+1U);
    }
    if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON ||
        in->type==REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        reist_gui_value_event_t event; reist_gui_value_event_initialize(&event);
        event.type=in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON
            ? REIST_GUI_VALUE_EVENT_POINTER_BUTTON : REIST_GUI_VALUE_EVENT_POINTER_MOTION;
        event.x=in->x; event.y=in->y;
        if (event.type==REIST_GUI_VALUE_EVENT_POINTER_BUTTON) {
            if (in->button!=1U) return;
            event.button=1U; event.pressed=in->pressed;
        }
        reist_gui_value_result_t result; reist_gui_value_result_initialize(&result);
        if (reist_gui_list_dispatch(&app.list,&app.selection,&event,&result)==0 && result.changed) {
            app.model.selected=app.selection.selected; app.redraw=1U;
        }
    }
    reist_gui_control_event_t event; reist_gui_control_event_initialize(&event);
    if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_BUTTON && in->button==1U) {
        event.type=REIST_GUI_CONTROL_EVENT_POINTER_BUTTON; event.button=1U; event.pressed=in->pressed;
        event.x=in->x; event.y=in->y;
    } else if (in->type==REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        event.type=REIST_GUI_CONTROL_EVENT_POINTER_MOTION; event.x=in->x; event.y=in->y;
    } else if (in->type==REIST_GUI_SURFACE_INPUT_KEYBOARD && in->pressed &&
               (in->key==13U || in->key==10U || (app.focus && in->key==' '))) {
        event.type=REIST_GUI_CONTROL_EVENT_KEYBOARD; event.pressed=1U;
        event.key=in->key==' ' ? REIST_GUI_CONTROL_KEY_SPACE : REIST_GUI_CONTROL_KEY_ENTER;
        if (!app.focus) focus_control(1U);
    } else return;
    reist_gui_control_result_t result; reist_gui_control_result_initialize(&result);
    if (reist_gui_control_dispatch(&app.controls,&app.control_state,&event,&result)!=0) return;
    if (result.damage_count || result.full_redraw) app.redraw=1U;
    if (result.activated) {
        if (result.action==1U) { (void)display_model_save(&app.model); app.redraw=1U; }
        else if (result.action==2U) app.close_requested=1U;
    }
}

int main(int argc,char **argv) {
    for (int i=1;i<argc;++i)
        if (!strcmp(argv[i],"--fault-probe")) app.fault_probe=1U;
    x86os_ipc_handle_t endpoint=0U;
    if (reist_gui_surface_endpoint_from_argv(argc,argv,&endpoint)!=0) {
        reist_display_mode_request_t caps;
        x86os_puts("DISPLAY_COMMAND_READY\nAnzeige: Systemsteuerung > Anzeige\n");
        x86os_puts("Konfiguration: config set desktop resolution auto|WIDTHxHEIGHT\n");
        if (argc==2 && strcmp(argv[1],"--list")==0 && x86os_display_mode_query(&caps)==0) {
            char value[16]; display_mode_text(value,caps.max_width,caps.max_height);
            x86os_puts("Maximale Achsen (Speichergrenzen gelten): "); x86os_puts(value); x86os_putchar('\n');
        }
        return 0;
    }
    if (reist_gui_surface_client_init(&app.client,endpoint)!=0) return 1;
    int status=-9;
    for (uint32_t attempt=0;attempt<250U;++attempt) {
        status=reist_gui_surface_client_create(&app.client,REIST_GUI_SURFACE_ROLE_TOPLEVEL,620,452);
        if (status==0 || (status!=-9 && status!=-13)) break;
        (void)x86os_sleep_ms(1U);
    }
    if (status || reist_gui_surface_client_ack_configure(&app.client,app.client.configured_serial) ||
        reist_gui_surface_client_set_title(&app.client,"Anzeige") ||
        reist_gui_surface_client_enable_scroll(&app.client)) goto cleanup;
    display_model_initialize(&app.model);
    if (layout()!=0) goto cleanup;
    app.redraw=1U; x86os_puts("DISPLAY_APPLET_READY\n");
    for (;;) {
        if (display_model_poll(&app.model)) app.redraw=1U;
        if (app.close_requested) {
            if (!app.model.child) break;
            int cancelled=display_model_cancel(&app.model);
            if (cancelled!=0 && cancelled!=-11) app.close_requested=0U;
        }
        if (app.redraw) {
            int painted=-11;
            for (uint32_t attempt=0;attempt<20U;++attempt) {
                painted=render(); if (!painted) break;
                (void)x86os_sleep_ms(5U);
            }
            if (painted) break;
            if (!app.painted) { app.painted=1U; x86os_puts("DISPLAY_APPLET_PAINTED\n"); }
            app.redraw=0U;
        }
        reist_gui_surface_message_t message;
        status=reist_gui_surface_client_receive(&app.client,&message,0U);
        if (status==-11) { (void)x86os_sleep_ms(5U); continue; }
        if (status!=0) break;
        if (message.type==REIST_GUI_SURFACE_CLOSE) app.close_requested=1U;
        else if (message.type==REIST_GUI_SURFACE_CONFIGURE) {
            if (reist_gui_surface_client_accept_configure(&app.client,&message) || layout()) break;
            app.redraw=1U;
        } else if (message.type==REIST_GUI_SURFACE_INPUT) input(&message.input);
    }
cleanup:
    (void)display_model_cancel(&app.model);
    (void)display_model_poll(&app.model);
    (void)reist_gui_surface_client_destroy(&app.client);
    (void)x86os_ipc_release(endpoint);
    x86os_puts("DISPLAY_APPLET_CLOSED\n");
    return status && status!=-11 ? 1 : 0;
}
