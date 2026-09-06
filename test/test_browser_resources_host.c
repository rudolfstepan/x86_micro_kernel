#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/apps/browser/browser_resources.h"
static browser_resources_t bundle, decoded;
static uint8_t wire[BROWSER_RESOURCE_WIRE_CAPACITY];
static browser_resource_needs_t needs;
int main(void) {
    static char url[BROWSER_RESOURCE_URL_CAPACITY]; const char *base="https://example.test/dir/page.html";
    assert(!browser_resource_url(base,"../a.css#fragment",url) && !strcmp(url,"https://example.test/a.css"));
    assert(!browser_resource_url(base,"HTTPS://EXAMPLE.TEST:443/a.css?q=Case",url));
    assert(!strcmp(url,"https://example.test/a.css?q=Case"));
    assert(!browser_resource_url(base,"%2e%2e/%61.css?x=%2f",url));
    assert(!strcmp(url,"https://example.test/a.css?x=%2F"));
    assert(browser_resource_url(base,"a.css?x=%2",url));
    assert(browser_resource_url(base,"a.css?x=%GG",url));
    assert(browser_resource_url(base,"data:text/css,p{}",url));
    assert(browser_resource_admit(base,"http://example.test/a.css"));
    assert(browser_resource_admit(base,"/etc/a.css"));
    memset(&bundle,0xa5,sizeof(bundle));
    browser_resources_init(&bundle,7);
    /* Reset work depends on admitted entries, not the maximum URL pool.
     * Unused records must stay inaccessible, without touching a MiB of them. */
    assert(((uint8_t *)bundle.entries)[0]==0xa5);
    assert(!browser_resources_validate(&bundle,base,7));
    assert(browser_resources_validate(&bundle,base,8));
    assert(browser_resources_pack(&bundle,base,wire,sizeof(wire))==16);
    memset(&decoded,0xa5,sizeof(decoded));
    assert(!browser_resources_unpack(wire,16,base,&decoded));
    assert(!decoded.count && !decoded.length && decoded.generation==7);
    assert(browser_resources_unpack(wire,15,base,&decoded));
    assert(browser_resources_unpack(wire,17,base,&decoded));
    uint32_t bad_count=UINT32_MAX; memcpy(wire+8,&bad_count,4);
    assert(browser_resources_unpack(wire,16,base,&decoded));
    assert(browser_resources_add(&bundle,base,"https://example.test/a.css",1)==0);
    for(unsigned i=(unsigned)strlen(bundle.entries[0].url)+1;i<sizeof(bundle.entries[0].url);++i)
        assert(!bundle.entries[0].url[i]);
    assert(!bundle.entries[0].effective[0] && ((uint8_t *)&bundle.entries[1])[0]==0xa5);
    assert(browser_resources_add(&bundle,base,"https://example.test/a.css",1)==0 && bundle.count==1);
    assert(browser_resources_add(&bundle,base,"https://example.test/b.css",9)<0);
    assert(browser_resources_store(&bundle,0,"http://example.test/a.css",(const uint8_t *)"p{}",3)<0 && !bundle.length);
    assert(!browser_resources_store(&bundle,0,"https://cdn.test/a.css",(const uint8_t *)"p{}",3));
    assert(browser_resources_find(&bundle,"https://cdn.test/a.css")==0);
    int n=browser_resources_pack(&bundle,base,wire,sizeof(wire)); assert(n==16+sizeof(browser_resource_t)+3);
    assert(!browser_resources_unpack(wire,(uint32_t)n,base,&decoded));
    assert(decoded.length==3 && !memcmp(decoded.bytes,"p{}",3));
    assert(browser_resources_unpack(wire,(uint32_t)n-1,base,&decoded));
    /* The previous 528-byte resource records remain decodable. */
    uint32_t old_header[]={2,7,1,3}; memset(wire,0,547); memcpy(wire,old_header,16);
    uint32_t old_record[]={0,3,1,1}; memcpy(wire+16,old_record,16);
    strcpy((char *)wire+32,"https://example.test/a.css"); strcpy((char *)wire+288,"https://cdn.test/a.css");
    memcpy(wire+544,"p{}",3);
    assert(!browser_resources_unpack(wire,547,base,&decoded) && decoded.version==BROWSER_RESOURCE_VERSION);
    for(unsigned i=256;i<BROWSER_RESOURCE_URL_CAPACITY;++i)
        assert(!decoded.entries[0].url[i] && !decoded.entries[0].effective[i]);
    bundle.entries[0].offset=1; assert(browser_resources_validate(&bundle,base,7)); bundle.entries[0].offset=0;
    needs=(browser_resource_needs_t){.magic=BROWSER_RESOURCE_NEED_MAGIC,.version=BROWSER_RESOURCE_VERSION,.generation=7,
        .identity={1,1,1,2,3,4,5,6,7,0,{0,0}}};
    assert(!browser_resource_need_add(&needs,"https://example.test/b.css",2));
    needs.size=offsetof(browser_resource_needs_t,items)+needs.count*sizeof(needs.items[0]);
    browser_html_header_t request=needs.identity; request.child_pid=request.child_generation=0;
    assert(!browser_resource_needs_validate(&needs,needs.size,&request,5,6,&bundle,base));
    assert(browser_resource_needs_validate(&needs,needs.size,&request,5,7,&bundle,base));
    ++needs.identity.request; assert(browser_resource_needs_validate(&needs,needs.size,&request,5,6,&bundle,base));
    browser_resources_init(&bundle,8);
    assert(!bundle.count && !bundle.length && browser_resources_find(&bundle,"https://cdn.test/a.css")<0);
    for(unsigned i=0;i<64;++i) {
        snprintf(url,sizeof(url),"https://example.test/%u.css",i);
        assert(browser_resources_add(&bundle,base,url,1)==(int)i);
        assert(!browser_resources_store(&bundle,i,url,NULL,0));
    }
    assert(browser_resources_add(&bundle,base,"https://example.test/overflow.css",1)==-28);
    assert(!browser_resources_validate(&bundle,base,8));
    browser_resources_init(&bundle,9);
    memset(wire,' ',BROWSER_RESOURCE_LIMIT);
    for(unsigned i=0;i<4;++i) {
        snprintf(url,sizeof(url),"https://example.test/%u.css",i);
        assert(browser_resources_add(&bundle,base,url,1)==(int)i);
        assert(!browser_resources_store(&bundle,i,url,wire,BROWSER_RESOURCE_LIMIT));
    }
    assert(bundle.length==BROWSER_RESOURCE_BYTES);
    assert(browser_resources_add(&bundle,base,"https://example.test/extra.css",1)==4);
    assert(browser_resources_store(&bundle,4,"https://example.test/extra.css",wire,1)<0);
    assert(!bundle.entries[4].ready && !browser_resources_validate(&bundle,base,9));
    puts("BROWSER_RESOURCE_BUNDLE_OK"); return 0;
}
