/* Differential admission proof: committed C oracle and the production C++ TU. */
#include "userspace/gui/apps/browser/browser_response.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern "C" int baseline_open_kind(const uint8_t*,size_t,const char*,uint32_t,browser_response_t*);
extern "C" int baseline_open_document(const uint8_t*,size_t,const char*,browser_response_t*);
extern "C" int baseline_open(const uint8_t*,size_t,const char*,uint32_t,browser_response_t*);
using reist::browser::ValidatedResponse;
static_assert(!__is_constructible(ValidatedResponse));
static_assert(!__is_constructible(ValidatedResponse,browser_response_t,int));
static_assert(__is_trivially_destructible(ValidatedResponse));

static void same(const browser_response_t& a,const browser_response_t& b) {
    assert(a.status==b.status && a.body_offset==b.body_offset && a.body_length==b.body_length);
    assert(a.encoding==b.encoding && !memcmp(a.redirect,b.redirect,sizeof(a.redirect)));
}
static unsigned checks;
static void compare(const uint8_t* bytes,size_t length,const char* url,uint32_t kind,bool document) {
    browser_response_t old{},current{};
    int expected=document ? baseline_open_document(bytes,length,url,&old) : baseline_open_kind(bytes,length,url,kind,&old);
    int actual=document ? browser_response_open_document(bytes,length,url,&current) : browser_response_open_kind(bytes,length,url,kind,&current);
    assert(actual==expected); same(old,current);
    auto typed=ValidatedResponse::open(bytes,length,url,kind,document);
    if(expected<0) {
        assert(!typed && !typed.value_if() && typed.error_if()->code==expected);
        same(old,typed.error_if()->diagnostic);
    } else {
        assert(typed && !typed.error_if());
        const auto* value=typed.value_if();
        same(old,value->metadata()); assert(value->decision()==expected);
        assert(value->metadata().body_offset<=length);
        assert(value->metadata().body_length==length-value->metadata().body_offset);
    }
    ++checks;
}
int main() {
    const char* fixtures[]={
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 8\r\n\r\n<p>x</p>",
        "HTTP/1.0 200 OK\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n\r\nPNG",
        "HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=latin1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-16le\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8; charset=utf-8\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=\"unterminated\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx",
        "HTTP/1.1 103 Early Hints\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ndecoded",
        "HTTP/1.1 101 Switching\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: unsupported\r\n\r\nx",
        "HTTP/1.0 302 Moved\r\nLocation: //cdn.test/a#new\r\n\r\n",
        "HTTP/1.0 302 Moved\r\nLocation: http://example.test/\r\n\r\n",
        "HTTP/1.0 302 Moved\r\nLocation: file:///etc/passwd\r\n\r\n",
        "HTTP/1.0 302 Moved\r\n\r\n",
        "HTTP/1.0 404 Missing\r\n\r\nmissing", "bad\r\n\r\n"
    };
    const char* url="https://example.test/old/page#part";
    for(const char* f:fixtures) for(uint32_t kind=0;kind<4;++kind) {
        compare((const uint8_t*)f,strlen(f),url,kind,false);
        if(!kind) for(size_t n=0;n<=strlen(f);++n) compare((const uint8_t*)f,n,url,kind,true);
    }
    char response[256];
    for(unsigned code=200;code<=599;++code) {
        snprintf(response,sizeof(response),"HTTP/1.1 %u Status\r\nLocation: ../new?p=1\r\nContent-Length: 0\r\n\r\n",code);
        compare((const uint8_t*)response,strlen(response),url,0,true);
    }
    for(unsigned mode=0;mode<3;++mode) {
        compare(nullptr,0,url,mode,false);
        compare((const uint8_t*)fixtures[0],strlen(fixtures[0]),nullptr,mode,false);
        compare((const uint8_t*)fixtures[0],strlen(fixtures[0]),"file:///a",mode,false);
        if(sizeof(size_t)>4) compare((const uint8_t*)fixtures[0],(size_t)UINT32_MAX+1,url,mode,false);
    }
    static char large[REIST_CURL_HEADER_CAPACITY+1], bad_url[REIST_CURL_LOCATION_CAPACITY];
    memset(large,'x',sizeof(large)); memset(bad_url,'x',sizeof(bad_url));
    compare((const uint8_t*)large,sizeof(large),url,0,true);
    compare((const uint8_t*)fixtures[0],strlen(fixtures[0]),bad_url,0,true);
    /* Fixed seed, bounded single-byte mutation of a valid response. */
    unsigned seed=0x318;
    for(unsigned i=0;i<4096;++i) {
        strcpy(response,fixtures[0]); seed=seed*1664525U+1013904223U;
        response[seed%strlen(fixtures[0])]=(char)(seed>>24);
        compare((const uint8_t*)response,strlen(fixtures[0]),url,0,true);
    }
    /* Legacy boolean accepts every nonzero value as image, not enum kind. */
    browser_response_t a{},b{};
    assert(baseline_open((const uint8_t*)fixtures[3],strlen(fixtures[3]),url,99,&a)==
        browser_response_open((const uint8_t*)fixtures[3],strlen(fixtures[3]),url,99,&b)); same(a,b);
    assert(browser_response_open(nullptr,0,nullptr,0,nullptr)==-22);
    // A scratch-backed parser must still return independent value snapshots,
    // including diagnostics; later failure/success must not mutate old results.
    auto retained=ValidatedResponse::open((const uint8_t*)fixtures[0],strlen(fixtures[0]),url,0,true);
    assert(retained && retained.value_if()->metadata().body_length==8);
    auto rejected=ValidatedResponse::open((const uint8_t*)fixtures[19],strlen(fixtures[19]),url,0,true);
    assert(!rejected && rejected.error_if()->diagnostic.status==404);
    for(unsigned i=0;i<64;++i) {
        compare((const uint8_t*)fixtures[3],strlen(fixtures[3]),url,1,false);
        compare(nullptr,0,nullptr,0,false);
        assert(retained.value_if()->metadata().status==200 && retained.value_if()->metadata().body_length==8);
        assert(rejected.error_if()->code==-5 && rejected.error_if()->diagnostic.status==404);
    }
    printf("BROWSER_RESPONSE_CPP_OK checks=%u\n",checks);
}
