#include <reist_js_files.h>
#include <reist_js_script.h>
#include <string.h>
#define FCHECK(x) do {if(!(x))return __LINE__;}while(0)
typedef struct {unsigned calls,mode,closed;uint32_t position;} file_fixture;
static int file_callback(void *opaque,uint32_t slot,uint32_t lease,uint32_t op,uint32_t arg,
                         void *bytes,uint32_t *length,uint32_t *value) {
    file_fixture *f=opaque;++f->calls;*length=*value=0;
    if(slot!=1 || lease!=17)return -13;
    if(f->mode==7)return -5;
    if(f->mode==8)return -28;
    if(f->mode==9){*length=arg+1;return 0;}
    if(f->mode==10)return -84;
    if(f->mode==14){*length=1;return -5;}
    if(op==REIST_JS_FILE_CLOSE){++f->closed;return 0;}
    if(op==REIST_JS_FILE_SIZE){*value=6;return 0;}
    if(op==REIST_JS_FILE_SEEK){*value=f->position=arg;return 0;}
    if(f->mode==11){memset(bytes,'x',arg);*length=arg;return 0;}
    uint32_t n=f->position<6?6-f->position:0;if(n>arg)n=arg;
    if(n)memcpy(bytes,"A\xc3\xbc" "BCD"+f->position,n);f->position+=n;*length=n;return 0;
}
static int js_file_vectors(int (*clock_fn)(void *,uint64_t *),void *opaque) {
    static char console[REIST_JS_CONSOLE_BYTES],bytes[REIST_JS_FILE_CHUNK],output[1024];
    static const char *sources[]={
        "let a=new Uint8Array(reist.files[0].read(3));[...a].join(',')",
        "reist.files[0].readText(3)",
        "let f=reist.files[0];f.seek(3);f.size()+':'+f.readText(3)+':'+f.read(1).byteLength",
        "let f=reist.files[0],n=0;for(let o of [{},Object.create(f),new Proxy(f,{})])try{f.read.call(o,1)}catch(e){n++}n",
        "let n=0;try{reist.files[0].read({valueOf(){n++;return 1}})}catch(e){}n",
        "let f=reist.files[0];f.close();f.close();try{f.read(1)}catch(e){}'closed'",
        "[typeof reist.files[0].write,typeof reist.open,typeof os,typeof process,JSON.stringify(reist.files[0])].join(',')",
        "try{reist.files[0].read(1)}catch(e){}42",
        "try{reist.files[0].read(1)}catch(e){}42",
        "try{reist.files[0].read(1)}catch(e){}42",
        "try{reist.files[0].read(1)}catch(e){}42",
        "let a=new Uint8Array(reist.files[0].read(131072));a.length+':'+a[0]+':'+a[131071]",
        "let n=0;for(let v of [0,-1,1.5,NaN,Infinity,131073,'1'])try{reist.files[0].read(v)}catch(e){n++}n",
        "let f=reist.files[0];f.seek(4294967295);f.read(2).byteLength",
        "try{reist.files[0].read(1)}catch(e){}42"};
    static const char *wanted[]={"65,195,188","A\xc3\xbc","6:BCD:0","3","0","closed",
        "undefined,undefined,undefined,undefined,{}","42","","","","131072:120:120","7","0",""};
    for(unsigned mode=0;mode<sizeof(sources)/sizeof(sources[0]);++mode) {
        reist_js_config config={1,sizeof(config),32U*1024U*1024U,16384,
            REIST_JS_SOURCE_MAX,REIST_JS_RESULT_MAX,1024,0,42,opaque,clock_fn};
        reist_js_status status;reist_js_engine *engine=reist_js_create(&config,&status);FCHECK(engine && !status);
        uint64_t now;size_t needed;FCHECK(!clock_fn(opaque,&now));
        FCHECK(!reist_js_eval(engine,"typeof reist",12,now+5000,output,sizeof(output),&needed));FCHECK(!strcmp(output,"undefined"));
        reist_js_script_host script={1,sizeof(script),console,sizeof(console),0,0,0,0,0};
        const char *args[]={"<eval>"};FCHECK(!reist_js_script_attach(engine,&script,1,args));
        file_fixture fixture={0,mode,0,0};
        reist_js_files_host host={0};host.version=1;host.struct_size=sizeof(host);host.count=1;
        host.context=&fixture;host.call=file_callback;host.bytes=bytes;
        host.files[0].slot=1;host.files[0].lease=17;host.files[0].rights=7;
        FCHECK(!reist_js_files_attach(engine,&host));FCHECK(reist_js_files_attach(engine,&host)==REIST_JS_INVALID);
        status=reist_js_eval(engine,sources[mode],strlen(sources[mode]),now+5000,output,sizeof(output),&needed);
        if(mode==8)FCHECK(status==REIST_JS_LIMIT && host.failed);
        else if(mode==9 || mode==10 || mode==14)FCHECK(status==REIST_JS_INVALID && host.failed);
        else {FCHECK(!status && !host.failed);FCHECK(!strcmp(output,wanted[mode]));}
        if(mode==3 || mode==4 || mode==6 || mode==12)FCHECK(!fixture.calls);
        if(mode==5)FCHECK(fixture.closed==1 && fixture.calls==1);
        reist_js_destroy(&engine);FCHECK(!engine);
    }
    return 0;
}
