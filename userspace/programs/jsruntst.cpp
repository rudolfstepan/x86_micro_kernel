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
extern "C" int main() {
    if(reist_libc_init_process(8U*1024U*1024U)) return 71;
    Source source; JsSession a,b;
    char *output=static_cast<char *>(malloc(JS_SERVICE_RESULT));
    char *other=static_cast<char *>(malloc(JS_SERVICE_RESULT));
    int line=output && other?realms(a,b,source,output,other):__LINE__;
    if(!line) line=runner_cases(source);
    a.cancel(); b.cancel(); (void)settle(a); (void)settle(b);
    if(a.pid() || b.pid()) { x86os_puts("JS_RUNNER_TEST_FAIL cleanup\n"); x86os_exit(70); }
    release(source); free(output); free(other);
    if(reist_libc_reset() && !line) line=__LINE__;
    if(line) { x86os_puts("JS_RUNNER_TEST_FAIL line="); number((uint32_t)line); x86os_puts("\n"); return 1; }
    x86os_puts("JS_RUNNER_RUNTIME_OK\n"); return 0;
}
