/* Actual service dispatch is inserted by the host test. Only the SDK boundary
 * is mocked. This fixture does not claim kernel/IPC layout compatibility. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define X86OS_REIST_ARP_RESOLUTION_VERSION 1U
#define X86OS_REIST_ARP_BINDING_VERSION 1U
#define X86OS_REIST_REPORT_NETWORK_DEGRADED 1U
#define X86OS_REIST_NETWORK_DEGRADED_SEMANTIC 2U
#define X86OS_REIST_REPORT_NETWORK_HEADER 3U
typedef struct { uint32_t length; uint8_t payload[128]; } x86os_ipc_message_t;
typedef struct { uint32_t version,struct_size,request_id,target_ip; } x86os_reist_arp_resolution_t;
typedef struct { uint32_t version,struct_size,probe_id,ip; uint8_t mac[6]; } x86os_reist_arp_binding_t;
static uint32_t pending_network_probe_id, pending_network_request;
static uint32_t current_request, current_probe, sends, bindings, reports, calls;
static int send_error, bind_error, report_error;
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL line=%u %s\n",__LINE__,#x); } } while(0)
static int x86os_reist_send_arp_request(const x86os_reist_arp_resolution_t* r) {
    ++calls;
    if (send_error) return send_error;
    if (r->request_id != current_request) return -13;
    current_request=0; ++sends; return 0;
}
static int x86os_reist_commit_arp_binding(const x86os_reist_arp_binding_t* b) {
    ++calls;
    if (bind_error) return bind_error;
    if (b->probe_id != current_probe) return -13;
    current_probe=0; ++bindings; return 0;
}
static int x86os_reist_report(uint32_t kind, uint32_t detail) {
    (void)kind; (void)detail; ++reports; return report_error;
}
static uint32_t message_probe_id(const x86os_ipc_message_t* r) {
    return (uint32_t)r->payload[60] | (uint32_t)r->payload[61]<<8 |
        (uint32_t)r->payload[62]<<16 | (uint32_t)r->payload[63]<<24;
}
/* PRODUCTION_HELPERS */
static int dispatch(x86os_ipc_message_t request) {
    const char* network="classified ARP";
    for (unsigned once=0;once<1;++once) {
        /* PRODUCTION_DISPATCH */
    }
    return 0;
}
static void put_id(uint8_t* p,uint32_t id) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(id>>(8*i));
}
static x86os_ipc_message_t request(uint32_t id) {
    x86os_ipc_message_t r={.length=26};
    r.payload[3]='A'; r.payload[4]=10; r.payload[7]=2;
    r.payload[8]=10; r.payload[11]=15; r.payload[12]=2;
    put_id(r.payload+18,id); put_id(r.payload+22,id);
    return r;
}
static x86os_ipc_message_t reply(uint32_t id) {
    x86os_ipc_message_t r={.length=64}; r.payload[3]='R';
    r.payload[16]=8; r.payload[17]=6; put_id(r.payload+60,id); return r;
}
static void reset(void) {
    pending_network_probe_id=pending_network_request=0;
    current_request=current_probe=1; sends=bindings=reports=calls=0;
    send_error=bind_error=report_error=0;
}
int main(void) {
    reset(); CHECK(!dispatch(request(1))); CHECK(sends==1 && pending_network_probe_id==1);
    /* The real kernel expires its250ms authority without clearing Ring3's
     * correlation. A newly authorized request must not exit16. */
    current_request=current_probe=2;
    CHECK(!dispatch(request(2))); CHECK(sends==2 && pending_network_probe_id==2);
    CHECK(!dispatch(reply(1))); CHECK(!bindings && pending_network_probe_id==2);
    CHECK(!dispatch(request(1))); CHECK(sends==2 && pending_network_probe_id==2);
    CHECK(!dispatch(request(2))); CHECK(sends==2 && pending_network_probe_id==2);
    CHECK(!dispatch(reply(2))); CHECK(bindings==1 && pending_network_probe_id==0);
    CHECK(!dispatch(reply(2))); CHECK(bindings==1);
    reset(); pending_network_request=99; pending_network_probe_id=1;
    current_request=current_probe=2;
    CHECK(!dispatch(request(2))); CHECK(!pending_network_request && pending_network_probe_id==2);
    const int denied[]={-11,-13,-5,-110};
    for(unsigned i=0;i<sizeof(denied)/sizeof(denied[0]);++i) {
        reset(); pending_network_probe_id=42; send_error=denied[i];
        CHECK(!dispatch(request(1))); CHECK(!sends && calls==1 && pending_network_probe_id==42);
        send_error=0; CHECK(!dispatch(request(1))); CHECK(sends==1 && pending_network_probe_id==1);
        pending_network_request=91; bind_error=denied[i];
        CHECK(!dispatch(reply(1))); CHECK(!bindings && !pending_network_probe_id && !pending_network_request);
        bind_error=0; current_request=current_probe=2;
        CHECK(!dispatch(request(2))); CHECK(!dispatch(reply(2))); CHECK(bindings==1);
    }
    reset(); send_error=-2000; CHECK(dispatch(request(1))==17 && !sends);
    reset(); pending_network_probe_id=1; bind_error=-2000;
    CHECK(dispatch(reply(1))==13 && !bindings && pending_network_probe_id==1);
    reset(); pending_network_probe_id=2; report_error=-2000;
    CHECK(dispatch(reply(1))==14 && !bindings && pending_network_probe_id==2);
    for(unsigned mode=0;mode<7;++mode) {
        reset(); x86os_ipc_message_t bad=request(1);
        if(mode==0)bad.length=25;
        if(mode==1)memset(bad.payload+4,0,4);
        if(mode==2)memcpy(bad.payload+4,bad.payload+8,4);
        if(mode==3)memset(bad.payload+18,0,4);
        if(mode==4)memset(bad.payload+22,0,4);
        if(mode==5)memset(bad.payload+12,0,6);
        if(mode==6)bad.payload[12]=3;
        CHECK(dispatch(bad)==16); CHECK(!calls && !sends && !pending_network_probe_id);
    }
    printf("NETWORK_ARP_LIFECYCLE checks=%u failures=%u\n",checks,failures);
    return failures ? 1 : 0;
}
