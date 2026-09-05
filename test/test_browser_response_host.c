#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/apps/browser/browser_response.h"

static browser_response_t response;
static int open_response(const char *head, const char *url, uint32_t image) {
    return browser_response_open((const uint8_t *)head, strlen(head), url, image, &response);
}
int main(void) {
    for (unsigned code = 301; code <= 308; ++code) {
        if (code == 304 || code == 305 || code == 306) continue;
        char head[256]; snprintf(head,sizeof(head),"HTTP/1.1 %u Redirect\r\nLocation: ../new?p=1\r\nContent-Length: 0\r\n\r\n",code);
        assert(open_response(head,"https://example.test/old/page#part",0)==1);
        assert(response.status==code && !strcmp(response.redirect,"https://example.test/new?p=1#part"));
    }
    assert(open_response("HTTP/1.0 302 Moved\r\nLocation: //cdn.test/a#new\r\n\r\n", "https://example.test/#old",1)==1);
    assert(!strcmp(response.redirect,"https://cdn.test/a#new"));
    assert(open_response("HTTP/1.0 302 Moved\r\nLocation: http://example.test/\r\n\r\n", "https://example.test/",0)<0);
    assert(open_response("HTTP/1.0 302 Moved\r\nLocation: file:///etc/passwd\r\n\r\n", "https://example.test/",0)<0);
    assert(open_response("HTTP/1.0 302 Moved\r\n\r\n", "https://example.test/",0)<0);
    assert(open_response("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=\"UTF-8\"\r\nContent-Length: 2\r\n\r\nok","https://example.test/",0)==0);
    assert(response.body_length==2);
    assert(open_response("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=latin1\r\n\r\nok","https://example.test/",0)==-95);
    assert(open_response("HTTP/1.0 200 OK\r\nContent-Type: application/pdf\r\n\r\npdf","https://example.test/",0)==-95);
    assert(open_response("HTTP/1.0 200 OK\r\nContent-Encoding: gzip\r\n\r\nx","https://example.test/",0)==-95);
    assert(open_response("HTTP/1.0 200 OK\r\nContent-Type: image/png\r\n\r\nPNG","https://example.test/",1)==0);
    assert(open_response("HTTP/1.0 404 Missing\r\nContent-Type: text/html\r\n\r\nmissing","https://example.test/",0)<0);
    assert(response.status==404);
    assert(open_response("HTTP/1.1 103 Early Hints\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ndecoded","https://example.test/",0)==0);
    assert(response.body_length==7); /* --include body is ALREADY dechunked. */
    assert(open_response("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nshort","https://example.test/",0)<0);
    char unterminated[REIST_CURL_LOCATION_CAPACITY]; memset(unterminated,'x',sizeof(unterminated));
    assert(open_response("HTTP/1.1 200 OK\r\n\r\nx",unterminated,0)==-22 && !response.status);
    assert(browser_response_open(NULL,0,"https://example.test/",0,&response)==-22 && !response.status);
    static const char valid[]="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 4\r\n\r\nhtml";
    for (size_t length=0; length<sizeof(valid)-1; ++length)
        assert(browser_response_open((const uint8_t *)valid,length,"https://example.test/",0,&response)<0);
    char large[REIST_CURL_HEADER_CAPACITY+1]; memset(large,'x',sizeof(large));
    memcpy(large,"HTTP/1.1 302 Found\r\nLocation: ",30);
    memcpy(large+sizeof(large)-5,"\r\n\r\n",5);
    assert(open_response(large,"https://example.test/",0)<0);
    puts("BROWSER_RESPONSE_HOST_OK"); return 0;
}
