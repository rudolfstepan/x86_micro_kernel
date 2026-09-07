#ifndef REIST_JS_VECTORS_H
#define REIST_JS_VECTORS_H
#include <reist_js.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <limits.h>
#ifndef JS_CASE_DETAIL
#define JS_CASE_DETAIL(index,status,output) ((void)0)
#endif
#define JC(expression) do { if(!(expression)) { line=__LINE__; goto failed; } } while(0)
static int js_vectors(int (*clock)(void *,uint64_t *),void *opaque) {
    int line=0; uint64_t now=0; size_t required=0;
    reist_js_engine *engine=NULL; reist_js_status status;
    char output[512]; char *large=NULL;
    reist_js_config config={REIST_JS_VERSION,sizeof(config),32U*1024U*1024U,16384,
        REIST_JS_SOURCE_MAX,REIST_JS_RESULT_MAX,1024,0,1234567,opaque,clock};
    const struct {const char *source,*expected;} cases[]={
        {"1+2*3","7"},
        {"let f=x=>y=>x+y; f(20)(22)","42"},
        {"class A { constructor(x){this.x=x} value(){return this.x} }; new A(42).value()","42"},
        {"JSON.stringify(JSON.parse('{\"a\":[1,true,null]}'))","{\"a\":[1,true,null]}"},
        {"'e\\u0301'.normalize('NFC') === '\\u00e9'","true"},
        {"/^(?<a>\\p{Letter}+)$/u.exec('Gr\\u00fc\\u00dfe').groups.a","Gr\xc3\xbc\xc3\x9f" "e"},
        {"(2n**100n).toString()","1267650600228229401496703205376"},
        {"new Map([['x',42]]).get('x') + new Set([1,1]).size","43"},
        {"new Proxy({x:41},{get:(t,k)=>t[k]+1}).x","42"},
        {"new Uint8ClampedArray([0.5,1.5,2.5,254.5,255.5,-1,NaN]).join(',')","0,2,2,254,255,0,0"},
        {"new Float64Array([1.25,-0])[1] === 0 && 1/new Float64Array([-0])[0] === -Infinity","true"},
        {"encodeURIComponent('a &+\\u00e9')","a%20%26%2B%C3%A9"},
        {"Math.sqrt(144)+Math.round(2.5)+Math.sin(0)","15"},
        {"let r=Math.random(); r>=0 && r<1","true"},
        {"typeof Date+','+typeof Atomics+','+typeof SharedArrayBuffer+','+typeof fetch+','+typeof document+','+typeof std",
         "undefined,undefined,undefined,undefined,undefined,undefined"},
        {"globalThis.answer=0; Promise.resolve(21).then(x=>answer=x*2); 'queued'","queued"},
        {"answer","42"},
        {"new WeakRef({x:1}) instanceof WeakRef","true"},
        {"try { throw new Error('caught') } catch(e) { e.message }","caught"}
    };
    JC(!clock(opaque,&now));
    engine=reist_js_create(&config,&status); JC(engine && status==REIST_JS_OK);
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        JC(!clock(opaque,&now));
        status=reist_js_eval(engine,cases[i].source,strlen(cases[i].source),now+10000,output,sizeof(output),&required);
        if(status || strcmp(output,cases[i].expected)) JS_CASE_DETAIL(i,status,output);
        JC(status==REIST_JS_OK); JC(!strcmp(output,cases[i].expected)); JC(required==strlen(output));
    }
    const char *errors[]={"let = ;","throw new Error('expected')","function recurse(){return recurse()} recurse()"};
    for(unsigned i=0;i<3;++i) {
        JC(!clock(opaque,&now));
        JC(reist_js_eval(engine,errors[i],strlen(errors[i]),now+10000,output,sizeof(output),&required)==REIST_JS_EXCEPTION);
        JC(!output[0]);
        JC(reist_js_eval(engine,"6*7",3,now+10000,output,sizeof(output),&required)==REIST_JS_OK);
        JC(!strcmp(output,"42"));
    }
    /* A cyclic object graph must be collectible in the same living runtime. */
    reist_js_stats before={REIST_JS_VERSION,sizeof(before),0,0,0,0},after=before;
    JC(reist_js_get_stats(engine,&before)==0);
    const char *cycles="(()=>{for(let i=0;i<2000;i++){let a={},b={};a.b=b;b.a=a;}return 42})()";
    JC(!clock(opaque,&now));
    JC(reist_js_eval(engine,cycles,strlen(cycles),now+10000,output,sizeof(output),&required)==REIST_JS_OK);
    JC(reist_js_collect(engine,now+10000)==REIST_JS_OK);
    JC(reist_js_get_stats(engine,&after)==0);
    JC(after.peak_bytes>before.live_bytes && after.live_bytes<before.live_bytes+64U*1024U);
    large=malloc(REIST_JS_SOURCE_MAX); JC(large);
    memset(large,' ',REIST_JS_SOURCE_MAX); memcpy(large+REIST_JS_SOURCE_MAX-4,"6*7;",4);
    JC(!clock(opaque,&now));
    JC(reist_js_eval(engine,large,REIST_JS_SOURCE_MAX,now+10000,output,sizeof(output),&required)==REIST_JS_OK);
    JC(!strcmp(output,"42")); free(large); large=NULL;
    const char badnul[]={'1',0,'2'};
    JC(reist_js_eval(engine,badnul,sizeof(badnul),now+10000,output,sizeof(output),&required)==REIST_JS_INVALID);
    reist_js_destroy(&engine); JC(!engine); reist_js_destroy(&engine);
    /* Different failure causes each fence evaluation; fresh create still works. */
    const char *failures[]={"new ArrayBuffer(64*1024*1024)","'x'.repeat(1024*1024)",
        "while(true){}","function again(){Promise.resolve().then(again)} again()"};
    const reist_js_status expected[]={REIST_JS_OOM,REIST_JS_LIMIT,REIST_JS_DEADLINE,REIST_JS_LIMIT};
    for(unsigned i=0;i<4;++i) {
        config.memory_limit=(i==0?2U:32U)*1024U*1024U; config.job_limit=(i==3?4:1024);
        engine=reist_js_create(&config,&status); JC(engine && !status);
        JC(!clock(opaque,&now));
        status=reist_js_eval(engine,failures[i],strlen(failures[i]),now+(i==2?20:10000),output,sizeof(output),&required);
        JC(status==expected[i]); JC(!output[0]);
        JC(reist_js_eval(engine,"1",1,now+10000,output,sizeof(output),&required)==REIST_JS_CLOSED);
        reist_js_destroy(&engine); JC(!engine);
    }
    config.memory_limit=32U*1024U*1024U; config.job_limit=1024;
    for(unsigned i=0;i<8;++i) {
        engine=reist_js_create(&config,&status); JC(engine && !status); JC(!clock(opaque,&now));
        JC(reist_js_eval(engine,"21*2",4,now+10000,output,sizeof(output),&required)==REIST_JS_OK);
        JC(!strcmp(output,"42")); reist_js_destroy(&engine);
    }
    /* ISO conversion dependency: four rounds, ties, inexact and invalid. */
    const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const long positive[]={2,2,3,2},negative[]={-2,-3,-2,-2};
    for(unsigned i=0;i<4;++i) {
        JC(!fesetround(rounds[i])); JC(!feclearexcept(FE_ALL_EXCEPT));
        JC(lrint(2.5)==positive[i] && lrint(-2.5)==negative[i]);
        JC(fetestexcept(FE_INEXACT));
    }
    JC(!fesetround(FE_TONEAREST)); JC(!feclearexcept(FE_ALL_EXCEPT));
    JC(lrint((double)LONG_MIN)==LONG_MIN); JC(!fetestexcept(FE_INVALID));
    (void)lrint(INFINITY); JC(fetestexcept(FE_INVALID));
    JC(!feclearexcept(FE_ALL_EXCEPT)); (void)lrint(NAN); JC(fetestexcept(FE_INVALID));
    JC(!feclearexcept(FE_ALL_EXCEPT));
failed:
    if(large) free(large);
    reist_js_destroy(&engine);
    (void)fesetround(FE_TONEAREST);
    return line;
}
#undef JC
#endif
