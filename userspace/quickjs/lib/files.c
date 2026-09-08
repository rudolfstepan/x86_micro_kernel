/* Opt-in native file objects: only an explicitly injected callback can act. */
#include "engine_internal.h"
#include <reist_js_files.h>
static JSClassID file_class;
static JSValue poison(JSContext *ctx,reist_js_files_host *host,int error) {
    host->failed=1;
    reist_js_engine *engine=JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
    engine->poisoned=error==-28?REIST_JS_LIMIT:error==-110?REIST_JS_DEADLINE:REIST_JS_INVALID;
    return JS_ThrowInternalError(ctx,"file host unavailable");
}
static JSValue file_call(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int method) {
    reist_js_file *file=JS_GetOpaque2(ctx,self,file_class);
    if(!file)return JS_EXCEPTION;
    reist_js_files_host *host=file->owner;
    if(!host || host->failed || host->busy) return host?poison(ctx,host,-84):JS_ThrowTypeError(ctx,"invalid file");
    uint32_t argument=0,operation=(uint32_t)method;
    if(method==5)operation=REIST_JS_FILE_READ;
    if(operation==REIST_JS_FILE_READ || operation==REIST_JS_FILE_SEEK) {
        double number;
        if(argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(ctx,&number,argv[0]) ||
           !(number>=0 && number<=4294967295.0) || number!=(double)(uint32_t)number ||
           (operation==REIST_JS_FILE_READ && (number<1 || number>REIST_JS_FILE_CHUNK)))
            return JS_ThrowRangeError(ctx,"invalid byte count or offset");
        argument=(uint32_t)number;
    } else if(argc) return JS_ThrowTypeError(ctx,"no arguments expected");
    if(file->closed) {
        if(operation==REIST_JS_FILE_CLOSE)return JS_UNDEFINED;
        return JS_ThrowTypeError(ctx,"file is closed");
    }
    host->busy=1;uint32_t length=0,value=0;
    int error=host->call(host->context,file->slot,file->lease,operation,argument,host->bytes,&length,&value);
    host->busy=0;
    if(error==-28 || error==-84 || error==-110) return poison(ctx,host,error);
    if(error>0 || error< -4095 || (error && (length || value)) || length>REIST_JS_FILE_CHUNK ||
       (operation==REIST_JS_FILE_READ && (length>argument || value)) ||
       (operation!=REIST_JS_FILE_READ && length) ||
       (!error && operation==REIST_JS_FILE_SEEK && value!=argument) ||
       (operation==REIST_JS_FILE_CLOSE && value)) return poison(ctx,host,-84);
    if(error)return JS_ThrowInternalError(ctx,"file access error %d",-error);
    if(operation==REIST_JS_FILE_CLOSE){file->closed=1;return JS_UNDEFINED;}
    if(operation==REIST_JS_FILE_READ)
        return method==5?JS_NewStringLen(ctx,host->bytes,length):JS_NewArrayBufferCopy(ctx,(uint8_t *)host->bytes,length);
    return JS_NewUint32(ctx,value);
}
reist_js_status reist_js_files_attach(reist_js_engine *engine,reist_js_files_host *host) {
    if(!engine || !host || engine->running || engine->poisoned || !JS_GetContextOpaque(engine->context) ||
       host->version!=1 || host->struct_size!=sizeof(*host) || !host->count || host->count>4 ||
       host->busy || host->failed || !host->call || !host->bytes)return REIST_JS_INVALID;
    for(unsigned i=0;i<4;++i) {
        reist_js_file *f=&host->files[i];
        if(f->owner || f->closed || (i<host->count?(f->slot!=i+1 || !f->lease || f->rights!=7):
           (f->slot || f->lease || f->rights)))return REIST_JS_INVALID;
    }
    JS_UpdateStackTop(engine->runtime);
    if(!file_class)JS_NewClassID(&file_class);
    JSClassDef definition={0};definition.class_name="REISTReadFile";
    JSContext *ctx=engine->context;
    int failed=JS_IsRegisteredClass(engine->runtime,file_class) || JS_NewClass(engine->runtime,file_class,&definition)<0;
    JSValue global=JS_GetGlobalObject(ctx),reist=JS_GetPropertyStr(ctx,global,"reist"),array=JS_NewArray(ctx);
    if(!JS_IsObject(reist) || JS_IsException(array))failed=1;
    static const char *names[]={"read","seek","size","close","readText"};
    for(unsigned i=0;!failed && i<host->count;++i) {
        JSValue object=JS_NewObjectClass(ctx,file_class);
        if(JS_IsException(object)){failed=1;break;}
        host->files[i].owner=host;JS_SetOpaque(object,&host->files[i]);
        for(unsigned method=1;!failed && method<=5;++method)
            failed=JS_DefinePropertyValueStr(ctx,object,names[method-1],
                JS_NewCFunctionMagic(ctx,file_call,names[method-1],method==1 || method==2 || method==5,
                    JS_CFUNC_generic_magic,(int)method),0)<0;
        if(!failed)failed=JS_SetPropertyUint32(ctx,array,i,JS_DupValue(ctx,object))<0;
        JS_FreeValue(ctx,object);
    }
    if(!failed)failed=JS_DefinePropertyValueStr(ctx,reist,"files",JS_DupValue(ctx,array),0)<0;
    JS_FreeValue(ctx,array);JS_FreeValue(ctx,reist);JS_FreeValue(ctx,global);
    if(failed || engine->oom){engine->poisoned=engine->oom?REIST_JS_OOM:REIST_JS_INVALID;return engine->poisoned;}
    return REIST_JS_OK;
}
