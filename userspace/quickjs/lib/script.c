/* Optional native console binding: memory only, no OS symbols or I/O. */
#include "engine_internal.h"
#include <reist_js_script.h>
#include <string.h>

static JSValue quota(JSContext *ctx,reist_js_script_host *host) {
    host->failed=1;
    reist_js_engine *engine=JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
    engine->poisoned=REIST_JS_LIMIT;
    return JS_ThrowRangeError(ctx,"console capacity exceeded");
}
static JSValue emit(JSContext *ctx,JSValueConst this_value,int argc,
                    JSValueConst *argv,int stream) {
    (void)this_value;
    reist_js_script_host *host=JS_GetContextOpaque(ctx);
    if(!host) return JS_ThrowInternalError(ctx,"no script host");
    if(host->failed || host->emitting || host->records>=REIST_JS_CONSOLE_RECORDS ||
       host->used>host->capacity || host->capacity-host->used<9) return quota(ctx,host);
    host->emitting=1;
    uint32_t start=host->used,offset=start+8;
    /* A throwing conversion leaves this record unpublished. Previous complete
     * records remain valid only for an ordinary exception, never quota failure. */
    for(int i=0;i<argc;++i) {
        size_t length=0;
        const char *text=JS_ToCStringLen(ctx,&length,argv[i]);
        if(!text) { host->emitting=0; return JS_EXCEPTION; }
        uint32_t separator=i!=0;
        if(length>=host->capacity-offset || separator>host->capacity-offset-length-1) {
            JS_FreeCString(ctx,text); return quota(ctx,host);
        }
        if(separator) host->bytes[offset++]=' ';
        memcpy(host->bytes+offset,text,length); offset+=(uint32_t)length;
        JS_FreeCString(ctx,text);
    }
    host->bytes[offset++]='\n';
    uint32_t channel=(uint32_t)stream,length=offset-start-8;
    memcpy(host->bytes+start,&channel,4); memcpy(host->bytes+start+4,&length,4);
    host->used=offset; ++host->records; host->emitting=0;
    return JS_UNDEFINED;
}
static JSValue exit_code(JSContext *ctx,JSValueConst this_value,int argc,JSValueConst *argv) {
    (void)this_value;
    reist_js_script_host *host=JS_GetContextOpaque(ctx);
    double code;
    if(!host || argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(ctx,&code,argv[0]) ||
       !(code>=0 && code<=125) || code!=(double)(uint32_t)code)
        return JS_ThrowRangeError(ctx,"exit code must be an integer 0..125");
    host->exit_code=(uint32_t)code;
    return JS_UNDEFINED;
}
reist_js_status reist_js_script_attach(reist_js_engine *engine,reist_js_script_host *host,
                                      unsigned argc,const char *const *argv) {
    if(!engine || !host || engine->running || engine->poisoned ||
       JS_GetContextOpaque(engine->context) || !argv || !argc || argc>16 ||
       host->version!=1 || host->struct_size!=sizeof(*host) || !host->bytes ||
       host->capacity<9 || host->capacity>REIST_JS_CONSOLE_BYTES ||
       host->used || host->records || host->exit_code || host->failed || host->emitting) return REIST_JS_INVALID;
    size_t bytes=0;
    for(unsigned i=0;i<argc;++i) {
        if(!argv[i]) return REIST_JS_INVALID;
        size_t n=0; while(n<4096 && argv[i][n]) ++n;
        if(n==4096 || n+1>4096-bytes) return REIST_JS_INVALID;
        bytes+=n+1;
    }
    JS_UpdateStackTop(engine->runtime);
    JSContext *ctx=engine->context;
    JSValue args=JS_NewArray(ctx),console=JS_NewObject(ctx),reist=JS_NewObject(ctx);
    JSValue global=JS_GetGlobalObject(ctx);
    int failed=JS_IsException(args) || JS_IsException(console) || JS_IsException(reist);
    for(unsigned i=0;!failed && i<argc;++i)
        failed=JS_SetPropertyUint32(ctx,args,i,JS_NewString(ctx,argv[i]))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,console,"log",
        JS_NewCFunctionMagic(ctx,emit,"log",0,JS_CFUNC_generic_magic,1))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,console,"error",
        JS_NewCFunctionMagic(ctx,emit,"error",0,JS_CFUNC_generic_magic,2))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,reist,"setExitCode",
        JS_NewCFunction(ctx,exit_code,"setExitCode",1))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,global,"scriptArgs",JS_DupValue(ctx,args))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,global,"console",JS_DupValue(ctx,console))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,global,"reist",JS_DupValue(ctx,reist))<0;
    if(!failed) failed=JS_SetPropertyStr(ctx,global,"print",
        JS_NewCFunctionMagic(ctx,emit,"print",0,JS_CFUNC_generic_magic,1))<0;
    JS_FreeValue(ctx,args); JS_FreeValue(ctx,console); JS_FreeValue(ctx,reist); JS_FreeValue(ctx,global);
    if(failed || engine->oom) {
        engine->poisoned=engine->oom?REIST_JS_OOM:REIST_JS_EXCEPTION;
        return engine->poisoned;
    }
    JS_SetContextOpaque(ctx,host);
    return REIST_JS_OK;
}
