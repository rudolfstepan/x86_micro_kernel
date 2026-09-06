/* Host-only fixed-work measurements of real C components; no replacement model. */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#error "Baseline measurements require active assertions (-UNDEBUG)"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include "userspace/gui/apps/browser/browser_response.h"
#include "reist/gui/control.h"
#include "userspace/gui/compositor/desktop_wm.h"

static double monotonic_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    assert(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0);
    assert(QueryPerformanceCounter(&counter));
    return (double)counter.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec ts;
    assert(!clock_gettime(CLOCK_MONOTONIC,&ts));
    return (double)ts.tv_sec+(double)ts.tv_nsec/1e9;
#endif
}

int main(void) {
    const uint32_t iterations=200000;
    static const char response[]="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 8\r\n\r\n<p>x</p>";
    browser_response_t parsed;
    double begin=monotonic_seconds();
    for(uint32_t i=0;i<iterations;++i) {
        assert(!browser_response_open_document((const uint8_t *)response,sizeof(response)-1,"https://example.test/",&parsed));
        assert(parsed.status==200 && parsed.body_length==8);
    }
    double response_ns=(monotonic_seconds()-begin)*1e9/iterations;
    reist_gui_control_t control={.id=1,.role=REIST_GUI_CONTROL_ROLE_PUSH_BUTTON,
        .label="Send",.bounds={10,10,100,30},.flags=REIST_GUI_CONTROL_VISIBLE|REIST_GUI_CONTROL_ENABLED};
    reist_gui_control_model_t model={.version=REIST_GUI_CONTROL_API_VERSION,.struct_size=sizeof(model),
        .controls=&control,.control_count=1,.surface_width=320,.surface_height=240,.damage_margin=3};
    reist_gui_control_state_t state;
    reist_gui_control_result_t result;
    reist_gui_control_event_t event;
    reist_gui_control_state_initialize(&state);
    reist_gui_control_result_initialize(&result);
    assert(!reist_gui_control_configure(&model,&state,&result));
    reist_gui_control_event_initialize(&event);
    event.type=REIST_GUI_CONTROL_EVENT_POINTER_MOTION; event.y=20;
    begin=monotonic_seconds();
    for(uint32_t i=0;i<iterations;++i) {
        event.x=20+(int32_t)(i&1);
        reist_gui_control_result_initialize(&result);
        assert(!reist_gui_control_dispatch(&model,&state,&event,&result));
        assert(state.hovered==0);
    }
    double gui_ns=(monotonic_seconds()-begin)*1e9/iterations;
    desktop_wm_t wm;
    desktop_wm_initialize(&wm,1024,768,0,740,22);
    for(uint32_t i=0;i<DESKTOP_WM_CAPACITY;++i) wm.windows[i].visible=0;
    wm.windows[0].visible=1; wm.windows[0].x=10; wm.windows[0].y=10;
    wm.windows[0].width=300; wm.windows[0].height=200;
    desktop_wm_event_t motion={.type=DESKTOP_WM_EVENT_POINTER_MOTION,.y=60};
    desktop_wm_dispatch_result_t damage;
    begin=monotonic_seconds();
    for(uint32_t i=0;i<iterations;++i) {
        motion.x=20+(int32_t)(i&1);
        assert(!desktop_wm_dispatch(&wm,&motion,&damage));
        assert(wm.pointer_focus==0);
    }
    double wm_ns=(monotonic_seconds()-begin)*1e9/iterations;
    assert(response_ns>0 && gui_ns>0 && wm_ns>0);
    printf("{\"browser_response_ns\":%.3f,\"gui_dispatch_ns\":%.3f,\"wm_dispatch_ns\":%.3f}\n",response_ns,gui_ns,wm_ns);
    return 0;
}
