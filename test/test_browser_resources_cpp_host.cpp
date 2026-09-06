/* Compare the real production boundary with the pinned, separately compiled C oracle. */
#include "userspace/gui/apps/browser/browser_resources.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern "C" {
int baseline_browser_resources_validate(const browser_resources_t*,const char*,uint32_t);
int baseline_browser_resources_pack(const browser_resources_t*,const char*,uint8_t*,uint32_t);
int baseline_browser_resources_unpack(const uint8_t*,uint32_t,const char*,browser_resources_t*);
}
using reist::browser::ValidatedResources;
static browser_resources_t bundle, oracle, current;
static uint8_t wire[BROWSER_RESOURCE_WIRE_CAPACITY], old_wire[BROWSER_RESOURCE_WIRE_CAPACITY];
static unsigned checks;
static const char *document="https://example.test/page";
static_assert(!__is_constructible(ValidatedResources));
static_assert(__is_trivially_destructible(ValidatedResources));
static_assert(sizeof(reist::Result<ValidatedResources,int>)<=64);
static_assert(sizeof(browser_resource_t)==16404 && sizeof(browser_resources_t)==2098448);

static void compare(const browser_resources_t *b,uint32_t generation=7) {
    int expected=baseline_browser_resources_validate(b,document,generation);
    assert(browser_resources_validate(b,document,generation)==expected);
    auto result=ValidatedResources::open(b,document,generation);
    assert(bool(result)==!expected);
    if(expected) { assert(!result.value_if() && *result.error_if()==expected); }
    else {
        assert(!result.error_if()); const auto *v=result.value_if();
        assert(&v->snapshot()==b && v->generation()==generation);
        for(uint32_t i=0;i<b->count;++i) {
            assert(v->entry(i)==&b->entries[i]);
            auto bytes=v->bytes(i);
            if(b->entries[i].ready) {
                assert(bytes && bytes.value_if()->size()==b->entries[i].length);
                assert(bytes.value_if()->data()==b->bytes+b->entries[i].offset);
            } else assert(!bytes && *bytes.error_if()==-22);
        }
        assert(!v->entry(b->count) && !v->entry(UINT32_MAX));
        auto absent=v->bytes(UINT32_MAX); assert(!absent && *absent.error_if()==-22);
    }
    ++checks;
}
static void unpack(const uint8_t *bytes,uint32_t length) {
    memset(&oracle,0xa5,sizeof(oracle)); memset(&current,0xa5,sizeof(current));
    int old=baseline_browser_resources_unpack(bytes,length,document,&oracle);
    assert(browser_resources_unpack(bytes,length,document,&current)==old);
    assert(!memcmp(&oracle,&current,sizeof(current))); // Partial C output stays diagnostic-only.
    if(!old) compare(&current,current.generation);
    ++checks;
}
int main() {
    memset(&bundle,0,sizeof(bundle)); browser_resources_init(&bundle,7);
    compare(nullptr); compare(&bundle); compare(&bundle,0); compare(&bundle,8);
    assert(browser_resources_add(&bundle,document,"https://example.test/a.css",0)==0);
    compare(&bundle);
    assert(!browser_resources_store(&bundle,0,"https://example.test/a.css",(const uint8_t*)"p{}",3));
    assert(browser_resources_add(&bundle,document,"https://example.test/b.css",1)==1);
    compare(&bundle);
    // Mutate one field/octet at a time; never use an old borrowed view after mutation.
    uint32_t *fields[]={&bundle.version,&bundle.generation,&bundle.count,&bundle.length,
        &bundle.entries[0].offset,&bundle.entries[0].length,&bundle.entries[0].ready,&bundle.entries[0].depth,
        &bundle.entries[1].offset,&bundle.entries[1].length,&bundle.entries[1].ready,&bundle.entries[1].depth};
    for(auto *field:fields) {
        uint32_t old=*field;
        const uint32_t values[]={0U,1U,2U,3U,7U,8U,64U,65U,262144U,1048576U,UINT32_MAX};
        for(uint32_t value: values) {
            *field=value; compare(&bundle);
        }
        *field=old;
    }
    unsigned seed=0x319;
    for(unsigned i=0;i<4096;++i) {
        char *url=i&1 ? bundle.entries[0].effective : bundle.entries[0].url;
        seed=seed*1664525U+1013904223U;
        unsigned index=seed%40; char old=url[index]; url[index]=(char)(seed>>24);
        compare(&bundle); url[index]=old;
    }
    int size=browser_resources_pack(&bundle,document,wire,sizeof(wire)); assert(size>0);
    assert(baseline_browser_resources_pack(&bundle,document,old_wire,sizeof(old_wire))==size);
    assert(!memcmp(wire,old_wire,(size_t)size));
    const uint32_t capacities[]={0U,15U,16U,(uint32_t)size-1,(uint32_t)size};
    for(uint32_t capacity: capacities) {
        memset(wire,0xa5,sizeof(wire)); memset(old_wire,0xa5,sizeof(old_wire));
        assert(browser_resources_pack(&bundle,document,wire,capacity)==
               baseline_browser_resources_pack(&bundle,document,old_wire,capacity));
        assert(!memcmp(wire,old_wire,sizeof(wire))); ++checks;
    }
    assert(browser_resources_pack(&bundle,document,wire,sizeof(wire))==size);
    const uint32_t lengths[]={0U,1U,15U,16U,(uint32_t)size-1,(uint32_t)size,(uint32_t)size+1,UINT32_MAX};
    for(uint32_t n: lengths) unpack(wire,n);
    unpack(nullptr,0);
    for(unsigned i=0;i<16;++i) {
        uint8_t old=wire[i]; wire[i]^=0xff; unpack(wire,(uint32_t)size); wire[i]=old;
    }
    // Valid and malformed legacy-v2 records, including nonterminated old URLs.
    uint32_t legacy[4]={2,7,1,3}; memset(wire,0,547); memcpy(wire,legacy,16);
    uint32_t record[4]={0,3,1,0}; memcpy(wire+16,record,16);
    strcpy((char*)wire+32,"https://example.test/a.css");
    strcpy((char*)wire+288,"https://example.test/a.css"); memcpy(wire+544,"p{}",3);
    unpack(wire,547); unpack(wire,546); unpack(wire,548);
    memset(wire+32,'x',256); unpack(wire,547);
    // Maximum ready count and all payload bytes without extra validation copies.
    browser_resources_init(&bundle,9);
    for(unsigned i=0;i<BROWSER_RESOURCE_COUNT;++i) {
        char url[64]; snprintf(url,sizeof(url),"https://example.test/%u.css",i);
        assert(browser_resources_add(&bundle,document,url,8)==(int)i);
        assert(!browser_resources_store(&bundle,i,url,(const uint8_t*)"",0));
    }
    compare(&bundle,9);
    bundle.entries[0].length=BROWSER_RESOURCE_LIMIT;
    for(unsigned i=1;i<4;++i) { bundle.entries[i].offset=i*BROWSER_RESOURCE_LIMIT; bundle.entries[i].length=BROWSER_RESOURCE_LIMIT; }
    for(unsigned i=4;i<BROWSER_RESOURCE_COUNT;++i) bundle.entries[i].offset=BROWSER_RESOURCE_BYTES;
    bundle.length=BROWSER_RESOURCE_BYTES; compare(&bundle,9);
    assert(browser_resources_pack(&bundle,document,wire,sizeof(wire))==(int)sizeof(wire));
    unpack(wire,sizeof(wire));
    printf("BROWSER_RESOURCES_CPP_OK checks=%u\n",checks);
}
