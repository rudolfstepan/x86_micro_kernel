#include <reist_js_script.h>
#include <string.h>
#define SCHECK(x) do { if(!(x)) return __LINE__; } while(0)
static int js_script_vectors(int (*clock_fn)(void *,uint64_t *),void *opaque) {
    static char journal[REIST_JS_CONSOLE_BYTES],output[REIST_JS_RESULT_MAX];
    const char *args[]={"<eval>","'); throw 99; //","Gr\xc3\xbc\xc3\x9f"};
    for(unsigned mode=0;mode<11;++mode) {
        reist_js_config config={1,sizeof(config),32U*1024U*1024U,16384,
            REIST_JS_SOURCE_MAX,REIST_JS_RESULT_MAX,1024,0,42,opaque,clock_fn};
        reist_js_status status; reist_js_engine *engine=reist_js_create(&config,&status);
        SCHECK(engine && !status);
        uint64_t now; SCHECK(!clock_fn(opaque,&now)); size_t length=0;
        const char *absent="typeof print+','+typeof console+','+typeof scriptArgs";
        SCHECK(!reist_js_eval(engine,absent,strlen(absent),
            now+5000,output,sizeof(output),&length));
        SCHECK(!strcmp(output,"undefined,undefined,undefined"));
        reist_js_script_host host={1,sizeof(host),journal,sizeof(journal),0,0,0,0,0};
        SCHECK(!reist_js_script_attach(engine,&host,3,args));
        SCHECK(reist_js_script_attach(engine,&host,3,args)==REIST_JS_INVALID);
        const char *source=mode==0?"print(scriptArgs[1]);console.error(scriptArgs[2],42);reist.setExitCode(7);0":
            mode==1?"try{print('x'.repeat(61440))}catch(e){};42":
            mode==2?"for(let i=0;i<257;i++)print();0":
            mode==3?"try{print({toString(){print('nested');return 'outer'}})}catch(e){};0":
            mode==4?"print('before');throw Error('ordinary')":
            mode==5?"print({toString(){throw 3}})":
            mode==6?"reist.setExitCode(1.5)":
            mode==7?"reist.setExitCode('7')":
            mode==8?"print('x'.repeat(61431));0":
            mode==9?"console.log();Promise.resolve().then(()=>console.error('job'));0":
            "[typeof os,typeof std,typeof process,typeof fetch,typeof Date,typeof SharedArrayBuffer].join(',')";
        SCHECK(!clock_fn(opaque,&now));
        status=reist_js_eval(engine,source,strlen(source),now+5000,output,sizeof(output),&length);
        if(mode>=1 && mode<=3) {
            SCHECK(status==REIST_JS_LIMIT && host.failed);
            SCHECK(reist_js_eval(engine,"1",1,now+5000,output,sizeof(output),&length)==REIST_JS_CLOSED);
        } else if(mode>=4 && mode<=7) {
            SCHECK(status==REIST_JS_EXCEPTION && !host.failed);
            SCHECK(host.records==(mode==4?1:0));
        } else {
            SCHECK(!status && !host.failed);
            if(mode==0) {
                uint32_t h[2]; memcpy(h,journal,8);
                SCHECK(host.records==2 && host.exit_code==7 && h[0]==1);
                SCHECK(h[1]==strlen(args[1])+1 && !memcmp(journal+8,args[1],strlen(args[1])));
                memcpy(h,journal+8+h[1],8); SCHECK(h[0]==2 && h[1]==10);
            }
            if(mode==8) SCHECK(host.records==1 && host.used==REIST_JS_CONSOLE_BYTES);
            if(mode==9) SCHECK(host.records==2);
            if(mode==10) SCHECK(!strcmp(output,"undefined,undefined,undefined,undefined,undefined,undefined"));
        }
        reist_js_destroy(&engine); SCHECK(!engine);
    }
    return 0;
}
#undef SCHECK
