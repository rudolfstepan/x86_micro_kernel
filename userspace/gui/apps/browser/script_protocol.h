#ifndef BROWSER_SCRIPT_PROTOCOL_H
#define BROWSER_SCRIPT_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include "html_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BROWSER_SCRIPT_MAGIC 0x3153434aU
#define BROWSER_SCRIPT_REPLY 0x3152504aU
#define BROWSER_SCRIPT_VERSION 1U
#define BROWSER_SCRIPT_SOURCE (1024U*1024U)
#define BROWSER_SCRIPT_SNAPSHOT (1024U*1024U)
#define BROWSER_SCRIPT_RESULT 65536U
#define BROWSER_SCRIPT_COUNT 32U
#define BROWSER_SCRIPT_JOURNAL (256U*1024U)
#define BROWSER_SCRIPT_MUTATIONS 128U
#define BROWSER_SCRIPT_DEADLINE 20000U
typedef struct browser_script_message {
    uint32_t magic,version,size,request,parent_pid,parent_generation;
    uint32_t child_pid,child_generation,ordinal,snapshot_length,source_length,reserved;
} browser_script_message_t;
#define BROWSER_SCRIPT_WIRE_MAX (sizeof(browser_script_message_t)+BROWSER_SCRIPT_SNAPSHOT+BROWSER_SCRIPT_SOURCE)
typedef struct browser_script_mutation { uint32_t node,length,offset; } browser_script_mutation_t;
int browser_script_message_valid(const browser_script_message_t *,size_t,const browser_html_header_t *,uint32_t,uint32_t,uint32_t,int);
int browser_script_journal(const char *,size_t,browser_script_mutation_t *,uint32_t *,uint32_t *);
int browser_script_unhex(const char *,uint32_t,char *);
/* Parser adapter configured only by an admitted, separately owned HTMLWORK. */
int browser_html_script_setup(uint32_t,uint32_t,const browser_html_header_t *,uint32_t,const char *);
void browser_html_script_finish(void);
#ifdef __cplusplus
}
#endif
#endif
