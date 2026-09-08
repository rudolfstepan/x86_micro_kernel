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
#define BROWSER_SCRIPT_ATTRIBUTE_VERSION 2U
#define BROWSER_SCRIPT_EXTERNAL_VERSION 3U
#define BROWSER_SCRIPT_REFERENCE 8193U
#define BROWSER_SCRIPT_ATTRIBUTE_NAME 255U
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
typedef struct browser_script_mutation { uint32_t node,length,offset,operation,name_length,name_offset; } browser_script_mutation_t;
int browser_script_message_valid(const browser_script_message_t *,size_t,const browser_html_header_t *,uint32_t,uint32_t,uint32_t,int);
int browser_script_journal(const char *,size_t,browser_script_mutation_t *,uint32_t *,uint32_t *);
int browser_script_journal_version(const char *,size_t,uint32_t,browser_script_mutation_t *,uint32_t *,uint32_t *);
int browser_script_attribute_name(const char *,uint32_t);
/* WHATWG JavaScript MIME essence, ASCII-insensitive, not parameter sniffing.
 * Inline so existing response-only consumers need no scripting linkage. */
static inline int browser_script_mime(const char *s,uint32_t n) {
    static const char *const types[]={"application/ecmascript","application/javascript",
        "application/x-ecmascript","application/x-javascript","text/ecmascript",
        "text/javascript","text/javascript1.0","text/javascript1.1","text/javascript1.2",
        "text/javascript1.3","text/javascript1.4","text/javascript1.5","text/jscript",
        "text/livescript","text/x-ecmascript","text/x-javascript"};
    if(!s || n>32) return 0;
    for(unsigned i=0;i<16;++i) {
        uint32_t j=0;
        while(j<n && types[i][j]) {
            unsigned char c=(unsigned char)s[j]; if(c>='A' && c<='Z') c+=32;
            if(c!=(unsigned char)types[i][j]) break;
            ++j;
        }
        if(j==n && !types[i][j]) return 1;
    }
    return 0;
}
int browser_script_unhex(const char *,uint32_t,char *);
/* Parser adapter configured only by an admitted, separately owned HTMLWORK. */
int browser_html_script_setup(uint32_t,uint32_t,const browser_html_header_t *,uint32_t,const char *);
void browser_html_script_finish(void);
#ifdef __cplusplus
}
#endif
#endif
