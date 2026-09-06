#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/apps/browser/browser_response.h"
#include "userspace/gui/apps/browser/browser_scene.h"
int main(void) {
    static char long_url[REIST_HTML_URL_CAPACITY+1],resolved[REIST_HTML_URL_CAPACITY];
    static reist_html_url_workspace_t resolver;
    strcpy(long_url,"https://example.test/path?q="); size_t prefix=strlen(long_url);
    memset(long_url+prefix,'x',8192-prefix); long_url[8192]=0;
    assert(!reist_html_url_resolve_wide("https://example.test/",long_url,resolved,sizeof(resolved),&resolver));
    assert(!strcmp(long_url,resolved));
    assert(reist_html_url_resolve("https://example.test/",long_url,resolved,sizeof(resolved))<0);
    static reist_curl_url_t parsed;
    assert(!reist_curl_parse_http_url(long_url,&parsed) && strlen(parsed.path)>8000);
    long_url[8192]='x'; long_url[8193]=0;
    assert(reist_html_url_resolve_wide("https://example.test/",long_url,resolved,sizeof(resolved),&resolver)<0 && !resolved[0]);
    long_url[8192]=0;
    browser_response_t r;
    const char html[]="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=ISO-8859-1\r\n\r\n<p>caf\xe9</p>";
    assert(browser_response_open((const uint8_t *)html,sizeof(html)-1,"https://example.test/",0,&r)==-95);
    assert(!browser_response_open_document((const uint8_t *)html,sizeof(html)-1,"https://example.test/",&r));
    assert(r.encoding==BROWSER_ENCODING_WINDOWS1252 && r.body_length==11);
    assert(browser_encoding_label("  ISO-8859-1 ",13)==BROWSER_ENCODING_WINDOWS1252);
    assert(browser_encoding_label("utf8",4)==BROWSER_ENCODING_UTF8);
    assert(browser_encoding_label("utf-7",5)==UINT32_MAX);
    browser_css_request_t q={.header={BROWSER_HTML_MAGIC,BROWSER_HTML_DOCUMENT_VERSION,
        sizeof(q)+BROWSER_DOCUMENT_INPUT_CAPACITY+BROWSER_RESOURCE_HEADER_BYTES,
        1,2,3,0,0,BROWSER_DOCUMENT_INPUT_CAPACITY,0,{BROWSER_ENCODING_WINDOWS1252,0}},
        .version=BROWSER_CSS_DOCUMENT_VERSION,.width=800,.height=600,.document_url="https://example.test/"};
    assert(!browser_css_request_validate(&q));
    ++q.header.input_length; assert(browser_css_request_validate(&q)==-84); --q.header.input_length;
    q.header.reserved[0]=99; assert(browser_css_request_validate(&q)==-84);
    q.header.reserved[0]=0; q.header.version=BROWSER_HTML_VERSION;
    assert(browser_css_request_validate(&q)==-84);
    puts("BROWSER_PUBLIC_ADMISSION_OK");
    return 0;
}
