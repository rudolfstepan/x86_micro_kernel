/* Real worker/IPC consumer; never evaluates JavaScript in the parent. */
#include "js_session.hpp"
#include <string.h>
using reist::browser::JsSession;
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static int settle(JsSession &s) {
    uint64_t start=0,now=0;
    if(x86os_monotonic_ms(&start)) return -1;
    for(unsigned i=0;i<20000 && s.busy();++i) {
        uint32_t before=s.progress(); s.poll();
        if(x86os_monotonic_ms(&now) || now<start || now-start>6500) return -1;
        if(s.busy()) {
            if(s.progress()==before) { if(x86os_sleep_ms(1)) return -1; }
            else if(x86os_yield()) return -1;
        }
    }
    return s.busy() || s.state()==JsSession::State::stranded ? -1 : 0;
}
static void record(const char *mode,int pid,uint32_t generation,int status) {
    x86os_puts("JS_SERVICE_REAP mode="); x86os_puts(mode);
    x86os_puts(" status="); x86os_print_number(status);
    x86os_puts(" pid="); x86os_print_number(pid);
    x86os_puts(" generation="); x86os_print_number((int)generation); x86os_puts("\n");
}
static int evaluate(JsSession &s,const char *code,char *output,const char *expected) {
    CHECK(!s.evaluate(code,(uint32_t)strlen(code),output,JS_SERVICE_RESULT));
    CHECK(!settle(s)); CHECK(s.ready() && !s.engine_status() && s.result());
    CHECK(!strcmp(s.result(),expected)); CHECK(s.result_length()==strlen(expected)); return 0;
}
static const char *hold="globalThis.counter=40; globalThis.add=()=>++counter; globalThis.held=new ArrayBuffer(8*1024*1024); 'seeded'";
static int exercise(JsSession &s,char *input,char *output) {
    // HELLO is accepted only after the native raw-syscall denial fixture.
    CHECK(!s.start(1,42,4)); CHECK(!settle(s)); CHECK(s.ready());
    int pid=s.pid(); uint32_t generation=s.generation();
    CHECK(!evaluate(s,hold,output,"seeded"));
    CHECK(!evaluate(s,"add()+1",output,"42"));
    x86os_puts("JS_SERVICE_DOMAIN_OK\n");
    CHECK(!evaluate(s,"Promise.resolve().then(()=>counter=43); 'queued'",output,"queued"));
    CHECK(!evaluate(s,"counter",output,"43"));
    CHECK(!s.evaluate("let = ;",7,output,JS_SERVICE_RESULT)); CHECK(!settle(s));
    CHECK(s.ready() && s.engine_status()==1 && !s.result());
    CHECK(!evaluate(s,"counter",output,"43"));
    memset(input,' ',JS_SERVICE_SOURCE); memcpy(input+JS_SERVICE_SOURCE-7,"counter",7);
    CHECK(!s.evaluate(input,JS_SERVICE_SOURCE,output,JS_SERVICE_RESULT)); CHECK(!settle(s));
    CHECK(s.result() && !strcmp(s.result(),"43"));
    CHECK(!s.evaluate("'x'.repeat(60000)",17,output,JS_SERVICE_RESULT)); CHECK(!settle(s));
    CHECK(s.result() && s.result_length()==60000);
    for(unsigned i=0;i<60000;++i) CHECK(s.result()[i]=='x');
    CHECK(!s.health()); CHECK(!settle(s)); CHECK(s.stats());
    uint32_t before=s.stats()[3]; CHECK(before>=8U*1024U*1024U);
    CHECK(!evaluate(s,"held=null; (()=>{let a={},b={};a.b=b;b.a=a})(); 0",output,"0"));
    CHECK(!s.health(true)); CHECK(!settle(s)); CHECK(s.stats() && s.stats()[3]<before);
    CHECK(!s.shutdown()); CHECK(!settle(s));
    CHECK(s.state()==JsSession::State::closed && s.exit_status()==0);
    record("normal",pid,generation,s.exit_status());
    for(uint32_t mode=1;mode<=4;++mode) {
        CHECK(!s.start(mode+1,42,mode==4?0:mode)); CHECK(!settle(s)); CHECK(s.ready());
        pid=s.pid(); generation=s.generation();
        CHECK(!evaluate(s,hold,output,"seeded"));
        CHECK(!s.evaluate("'reply'",7,output,JS_SERVICE_RESULT));
        if(mode==2) {
            CHECK(!settle(s));
            CHECK(s.ready() && s.result() && !strcmp(s.result(),"native-hang"));
            x86os_puts("JS_SERVICE_HANG_CONFIRMED\n");
            CHECK(!s.evaluate("'reply'",7,output,JS_SERVICE_RESULT,200));
        }
        if(mode==4) s.cancel();
        CHECK(!settle(s)); CHECK(s.state()==JsSession::State::failed && !s.result());
        if(mode==2) CHECK(s.error()==-110);
        if(mode==3) CHECK(s.error()==-84);
        CHECK(mode==1 ? s.exit_status()==142 : mode==2 ? s.exit_status()==143 :
              (s.exit_status()==143 || s.exit_status()==74));
        record(mode==1?"fault":mode==2?"hang":mode==3?"stale":"cancel",pid,generation,s.exit_status());
    }
    // Recovery stays on the same document epoch but has a fresh empty realm.
    CHECK(!s.start(5,99)); CHECK(!settle(s)); CHECK(s.ready());
    pid=s.pid(); generation=s.generation();
    CHECK(!evaluate(s,"typeof counter",output,"undefined"));
    CHECK(!evaluate(s,"21*2",output,"42"));
    CHECK(!s.shutdown()); CHECK(!settle(s)); CHECK(s.state()==JsSession::State::closed && !s.exit_status());
    record("fresh",pid,generation,s.exit_status());
    return 0;
}
static void decimal(char out[11],uint32_t n) {
    char reverse[10]; unsigned count=0;
    do { reverse[count++]=(char)('0'+n%10); n/=10; } while(n);
    for(unsigned i=0;i<count;++i) out[i]=reverse[count-i-1]; out[count]=0;
}
static int orphan_helper(const char *argument) {
    uint32_t endpoint=0;
    for(unsigned i=0;argument[i];++i) {
        if(i>=10 || argument[i]<'0' || argument[i]>'9' || endpoint>(UINT32_MAX-(uint32_t)(argument[i]-'0'))/10) return 64;
        endpoint=endpoint*10+(uint32_t)(argument[i]-'0');
    }
    if(!endpoint) return 64;
    JsSession s;
    if(s.start(1,42) || settle(s) || !s.ready()) x86os_exit(91);
    uint32_t identity[]={(uint32_t)s.pid(),s.generation()};
    x86os_ipc_message_t message={X86OS_IPC_MESSAGE_VERSION,sizeof(message),sizeof(identity),{0}};
    memcpy(message.payload,identity,sizeof(identity));
    if(x86os_ipc_send_timeout(endpoint,&message,1000)) x86os_exit(92);
    // Deliberate abrupt owner exit: OS revokes its handles; worker must detect
    // parent/channel loss and release its own persistent realm, without C++ cleanup.
    x86os_exit(37);
}
static int orphan() {
    x86os_ipc_handle_t endpoint=0; CHECK(!x86os_ipc_create(&endpoint));
    char number[11]; decimal(number,endpoint);
    const char *args[]={"/usr/bin/jsipctst.prg","--orphan",number};
    int pid=x86os_spawnv(args[0],3,args); CHECK(pid>0);
    x86os_process_identity_t owner; CHECK(!x86os_process_identity_of(pid,&owner));
    CHECK(!x86os_ipc_delegate(endpoint,pid,X86OS_IPC_RIGHT_SEND));
    x86os_ipc_message_t message={X86OS_IPC_MESSAGE_VERSION,sizeof(message),0,{0}};
    CHECK(!x86os_ipc_receive_timeout(endpoint,&message,5000));
    CHECK(message.version==X86OS_IPC_MESSAGE_VERSION && message.struct_size==sizeof(message) && message.length==8);
    uint32_t worker[2]; memcpy(worker,message.payload,sizeof(worker)); CHECK(worker[0] && worker[1]);
    CHECK(!x86os_ipc_close(endpoint));
    uint64_t start=0,now=0; CHECK(!x86os_monotonic_ms(&start));
    int reaped=0,absent=0,retired=0,witness[2]={0,0},witness_reaped=0;
    for(unsigned n=0;n<5000;++n) {
        int found_worker=0;
        for(unsigned i=0;i<32;++i) {
            x86os_process_info_t info; int rc=x86os_process_info(i,&info); CHECK(rc>=0); if(!rc) break;
            if(info.pid==(int)worker[0]) {
                found_worker=1;
                if(info.state==X86OS_PROCESS_ZOMBIE) {
                    CHECK(info.parent_pid==0 && info.exit_status==74);
                    x86os_process_identity_t identity;
                    CHECK(x86os_process_identity_of(info.pid,&identity)==-3);
                    retired=1;
                }
            }
            if(!reaped && info.pid==pid && info.state==X86OS_PROCESS_ZOMBIE) {
                CHECK(info.parent_pid==x86os_getpid());
                int status=-1; CHECK(x86os_wait(pid,&status)==pid && status==37); reaped=1;
                // Process table compaction can change this iteration's indices.
                found_worker=1; break;
            }
            bool changed=false;
            for(unsigned k=0;k<2;++k) {
                if(witness[k] && !(witness_reaped&(1<<k)) && info.pid==witness[k] &&
                   info.state==X86OS_PROCESS_ZOMBIE) {
                    // Own, unreaped children pin these PIDs even when they
                    // exit before a separate live-identity query can run.
                    CHECK(info.parent_pid==x86os_getpid());
                    int status=-1; CHECK(x86os_wait(witness[k],&status)==witness[k] && status==37);
                    witness_reaped|=1<<k; changed=true; break;
                }
            }
            if(changed) { found_worker=1; break; }
        }
        absent=!found_worker;
        if(reaped && retired && !witness[0]) {
            // Orphan exit metadata remains observable until slot reuse. The
            // first allocation can reuse the helper slot; keep its PID pinned
            // while a second ordinary allocation proves the worker slot free.
            const char *args[]={"/usr/bin/jsipctst.prg","--retire"};
            for(unsigned k=0;k<2;++k) {
                witness[k]=x86os_spawnv(args[0],2,args);
                CHECK(witness[k]>0 && witness[k]!=pid && witness[k]!=(int)worker[0]);
            }
            CHECK(witness[0]!=witness[1]);
            x86os_puts("JS_SERVICE_ORPHAN_RETIRED status=74 parent=0\n");
            absent=0;
        }
        if(reaped && absent && (!witness[0] || witness_reaped==3)) break;
        CHECK(!x86os_monotonic_ms(&now) && now>=start && now-start<5000);
        CHECK(!x86os_sleep_ms(1));
    }
    CHECK(reaped && absent && (!witness[0] || witness_reaped==3));
    x86os_puts("JS_SERVICE_ORPHAN_OK\n"); return 0;
}
extern "C" int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--orphan")) return orphan_helper(argv[2]);
    if(argc==2 && !strcmp(argv[1],"--retire")) return 37;
    if(argc!=1) return 64;
    JsSession session;
    char *input=static_cast<char *>(x86os_malloc(JS_SERVICE_SOURCE));
    char *output=static_cast<char *>(x86os_malloc(JS_SERVICE_RESULT));
    int line=input && output ? exercise(session,input,output) : 1000;
    if(!line) line=orphan();
    if(line) {
        session.cancel(); (void)settle(session);
        x86os_puts("JS_SERVICE_TEST_FAIL line="); x86os_print_number(line);
        x86os_puts(" error="); x86os_print_number(session.error());
        x86os_puts(" state="); x86os_print_number((int)session.state());
        x86os_puts(" status="); x86os_print_number(session.exit_status()); x86os_puts("\n");
    }
    x86os_free(input); x86os_free(output);
    if(line) x86os_exit(1); // Failed recovery is terminal; no fake destructor cleanup.
    x86os_puts("JS_SERVICE_RUNTIME_OK\n"); return 0;
}
