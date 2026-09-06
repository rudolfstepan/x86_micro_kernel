/* Shared frozen fixture for the pinned C oracle and production C++ boundary. */
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "userspace/gui/apps/browser/browser_resources.h"
static browser_resources_t bundle;
int main(void) {
    const char *document="https://example.test/page", *url="https://example.test/a.css";
    browser_resources_init(&bundle,7);
    assert(browser_resources_add(&bundle,document,url,0)==0);
    assert(!browser_resources_store(&bundle,0,url,(const uint8_t*)"p{}",3));
    LARGE_INTEGER frequency,begin,end;
    assert(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0);
    assert(QueryPerformanceCounter(&begin));
    for(unsigned i=0;i<200000;++i) assert(!browser_resources_validate(&bundle,document,7));
    assert(QueryPerformanceCounter(&end));
    printf("{\"resource_validate_ns\":%.3f}\n",(double)(end.QuadPart-begin.QuadPart)*1e9/(double)frequency.QuadPart/200000.0);
}
