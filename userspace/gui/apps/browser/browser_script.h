#ifndef BROWSER_SCRIPT_H
#define BROWSER_SCRIPT_H
#include "browser_scene.h"
#include "script_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct browser_script_owner browser_script_owner;
browser_script_owner *browser_script_create(void);
int browser_script_destroy(browser_script_owner *); /* only after successful explicit reap */
void browser_script_cancel(browser_script_owner *);
void browser_script_navigation(browser_script_owner *);
int browser_script_poll(browser_script_owner *);
int browser_script_busy(const browser_script_owner *);
int browser_script_ready(const browser_script_owner *);
int browser_script_stranded(const browser_script_owner *);
uint32_t browser_script_progress(const browser_script_owner *);
int browser_script_prepare(browser_script_owner *,const browser_html_header_t *,int reflow);
uint32_t browser_script_endpoint(const browser_script_owner *);
int browser_script_bind(browser_script_owner *,uint32_t,uint32_t);
/* 1 consumed scripting packet; 0 ordinary final CSS reply; <0 invalid. */
int browser_script_receive(browser_script_owner *,const browser_css_packet_t *,uint32_t);
int browser_script_finish_parse(browser_script_owner *);
void browser_script_commit(browser_script_owner *,int reflow);
int browser_script_has_active(const browser_script_owner *);
void browser_script_deny(browser_script_owner *,int);
/* Trusted test selector only; never derived from document/source/IPC. */
void browser_script_fixture(browser_script_owner *,uint32_t);
uint32_t browser_script_executions(const browser_script_owner *);
#ifdef __cplusplus
}
#endif
#endif
