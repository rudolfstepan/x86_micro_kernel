/* Guest proof using the production runner/worker, never a second engine. */
#include "runner.hpp"
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
using namespace reist::script;
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static void number(uint32_t n) { x86os_print_number((int)n); }
static int eval(JsSession &s,char *output,const char *code,const char *expected) {
    CHECK(!s.evaluate(code,(uint32_t)strlen(code),output,JS_SERVICE_RESULT));
    CHECK(!settle(s) && s.result() && !strcmp(s.result(),expected)); return 0;
}
static int retire(JsSession &s,const char *mode) {
    int pid=s.pid(); uint32_t generation=s.generation();
    if(s.ready()) CHECK(!s.shutdown());
    else s.cancel();
    CHECK(!settle(s) && !s.pid());
    x86os_puts("JS_RUNNER_REAP mode="); x86os_puts(mode);
    x86os_puts(" pid="); number((uint32_t)pid); x86os_puts(" generation="); number(generation);
    x86os_puts(" status="); number((uint32_t)s.exit_status()); x86os_puts("\n");
    return 0;
}
static int realms(JsSession &a,JsSession &b,Source &source,char *output,char *other) {
    const char *args[]={"js","-e","globalThis.local=42;print(scriptArgs[1]);reist.setExitCode(7)","argument as data"};
    CHECK(!prepare(4,args,source));
    CHECK(!a.start(1,42,4) && !b.start(1,99));
    CHECK(!settle(a) && !settle(b) && a.ready() && b.ready());
    // Generations are scoped to process slots, not globally unique numbers.
    CHECK(a.pid()!=b.pid() && a.generation() && b.generation());
    CHECK(!a.script(source.packet,source.length,output,JS_SERVICE_RESULT));
    CHECK(!eval(b,other,"globalThis.other=9;typeof local+','+typeof print+','+typeof console+','+typeof scriptArgs",
        "undefined,undefined,undefined,undefined"));
    CHECK(!settle(a) && a.script_result() && !js_script_reply_valid(output,a.script_result_length()));
    js_script_reply reply; memcpy(&reply,output,sizeof(reply)); CHECK(reply.exit_code==7 && reply.records==1);
    CHECK(a.script(source.packet,source.length,output,JS_SERVICE_RESULT)==-22);
    CHECK(!retire(a,"script") && !eval(b,other,"other","9"));
    CHECK(!retire(b,"browser"));
    release(source);
    CHECK(!a.start(2,17) && !settle(a) && a.ready());
    CHECK(!eval(a,output,"typeof local+','+typeof console","undefined,undefined"));
    CHECK(!retire(a,"fresh"));
    x86os_puts("JS_RUNNER_REALMS_OK\n");
    // A completed language deadline is not the only protection: owner also
    // fences/reaps at the same absolute transport deadline if no reply arrives.
    const char *hang[]={"js","-e","for(;;){}"};
    CHECK(!prepare(3,hang,source));
    CHECK(!a.start(3,42) && !settle(a) && a.ready());
    int pid=a.pid(); uint32_t generation=a.generation();
    CHECK(!a.script(source.packet,source.length,output,JS_SERVICE_RESULT,200));
    CHECK(!settle(a) && !a.pid() && !a.script_result());
    CHECK(a.error()==-110 || a.engine_status()==3);
    CHECK(a.exit_status()==0 || a.exit_status()==74 || a.exit_status()==143);
    x86os_puts("JS_RUNNER_TIMEOUT_OK pid="); number((uint32_t)pid);
    x86os_puts(" generation="); number(generation); x86os_puts("\n");
    CHECK(!a.start(4,42) && !settle(a) && a.ready());
    CHECK(!a.script(source.packet,source.length,output,JS_SERVICE_RESULT));
    a.poll(); a.cancel(); CHECK(!settle(a) && a.error()==-125 && !a.pid());
    CHECK(a.exit_status()==74 || a.exit_status()==143);
    release(source); x86os_puts("JS_RUNNER_CANCEL_OK\n");
    return 0;
}
static int runner_cases(Source &source) {
    const char *code[]={
        "print('JS_RUNNER_STDOUT_OK');console.error('JS_RUNNER_STDERR_OK');reist.setExitCode(7)",
        "throw Error('expected')",
        "let = ;",
        "try{print('x'.repeat(61440))}catch(e){};0",
        "if(scriptArgs[1]!==\"');throw 99;//\")throw 4;print('JS_RUNNER_ARGV_OK')",
        "print([typeof os,typeof std,typeof process,typeof fetch].join(','))"};
    const int wanted[]={7,1,1,71,0,0};
    for(unsigned i=0;i<6;++i) {
        const char *args[]={"js","-e",code[i],"');throw 99;//"};
        CHECK(!prepare(4,args,source));
        int actual=execute(source,false); release(source);
        CHECK(actual==wanted[i]);
        x86os_puts("JS_RUNNER_CASE index="); number(i); x86os_puts(" status="); number((uint32_t)actual); x86os_puts("\n");
    }
    const char *file[]={"js","/htdocs/hello.js","guest","42"};
    CHECK(!prepare(4,file,source)); CHECK(execute(source,false)==0); release(source);
    const char *missing[]={"js","/htdocs/missing.js"};
    CHECK(prepare(2,missing,source)==66 && !source.packet);
    // Full1MiB source through the production binary protocol, not just EVAL.
    char *large=static_cast<char *>(malloc(JS_SERVICE_SOURCE+1)); CHECK(large);
    memset(large,' ',JS_SERVICE_SOURCE); memcpy(large,"print(42);",10); large[JS_SERVICE_SOURCE]=0;
    const char *args[]={"js","-e",large}; int admitted=prepare(3,args,source); free(large);
    CHECK(!admitted); CHECK(execute(source,false)==0); release(source);
    x86os_puts("JS_RUNNER_SOURCE_OK\n"); return 0;
}
static int file_cases(Source &source) {
    static const char *codes[]={
        "let f=reist.files[0];if(f.size()<100||f.readText(14)!=='// JS2 example')throw 1;"
        "f.seek(0);if(new Uint8Array(f.read(2)).join(',')!=='47,47')throw 2;"
        "f.seek(f.size());if(f.read(1).byteLength)throw 3;f.close();f.close();let closed=false;"
        "try{f.read(1)}catch(e){closed=true}if(!closed||typeof f.write!=='undefined'||typeof reist.open!=='undefined')throw 4",
        "reist.files[0].read(1);throw Error('expected')",
        "try{for(let i=0;i<257;i++)reist.files[0].seek(0)}catch(e){};42",
        "reist.files[0].read(1);while(true){}",
        "for(let f of reist.files){if(f.size()<100||f.readText(2)!=='//')throw 5;f.close()}"
    };
    const int expected[]={0,1,71,124,0};
    for(unsigned i=0;i<5;++i) {
        const char *args[]={"js","--read","/htdocs/hello.js","-e",codes[i]};
        const char *four[]={"js","--read","/htdocs/hello.js","--read","/htdocs/hello.js",
            "--read","/htdocs/hello.js","--read","/htdocs/hello.js","-e",codes[i]};
        CHECK(!prepare(i==4?11:5,i==4?four:args,source));
        int actual=execute(source,false);release(source);CHECK(actual==expected[i]);
        x86os_puts("JS_FILES_CASE index=");number(i);x86os_puts(" status=");number(actual);x86os_puts("\n");
    }
    // Cancel while the worker is awaiting a real host call, then close/reuse all slots.
    const char *args[]={"js","-e","reist.files[0].read(1)"};CHECK(!prepare(3,args,source));
    FileBroker broker;char paths[4][192]{};
    for(unsigned i=0;i<4;++i)memcpy(paths[i],"/htdocs/hello.js",sizeof("/htdocs/hello.js"));
    CHECK(!broker.admit(paths,4));
    char *packet=(char *)malloc(source.length+80),*output=(char *)malloc(JS_SERVICE_RESULT);
    CHECK(packet && output);memcpy(packet,&broker.manifest(),80);memcpy(packet+80,source.packet,source.length);
    JsSession session;int line=0;
    if(session.start(1,42) || settle(session) || !session.ready() ||
       session.script_capabilities(packet,source.length+80,output,JS_SERVICE_RESULT))line=__LINE__;
    for(unsigned i=0;!line && i<10000 && session.busy() && !session.host_request();++i) {
        session.poll();x86os_sleep_ms(1);
    }
    if(!session.host_request())line=__LINE__;
    session.cancel();if(settle(session) || session.pid())x86os_exit(70);
    if(session.exit_status()!=74 && session.exit_status()!=143)line=__LINE__;
    if(broker.close())x86os_exit(70);
    free(packet);free(output);release(source);CHECK(!line);
    CHECK(!broker.admit(paths,4));CHECK(!broker.close());
    x86os_puts("JS_FILES_CANCEL_REUSE_OK\n");
    return 0;
}
extern "C" int main() {
    if(reist_libc_init_process(8U*1024U*1024U)) return 71;
    Source source; JsSession a,b;
    char *output=static_cast<char *>(malloc(JS_SERVICE_RESULT));
    char *other=static_cast<char *>(malloc(JS_SERVICE_RESULT));
    int line=output && other?realms(a,b,source,output,other):__LINE__;
    if(!line) line=runner_cases(source);
    if(!line) line=file_cases(source);
    a.cancel(); b.cancel(); (void)settle(a); (void)settle(b);
    if(a.pid() || b.pid()) { x86os_puts("JS_RUNNER_TEST_FAIL cleanup\n"); x86os_exit(70); }
    release(source); free(output); free(other);
    if(reist_libc_reset() && !line) line=__LINE__;
    if(line) { x86os_puts("JS_RUNNER_TEST_FAIL line="); number((uint32_t)line); x86os_puts("\n"); return 1; }
    x86os_puts("JS_RUNNER_RUNTIME_OK\n"); return 0;
}
