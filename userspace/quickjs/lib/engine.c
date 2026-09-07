/* Private, single-owner QuickJS embedding. No DOM, OS facade or ambient I/O. */
#include <reist_js.h>
#include <reist_js_port.h>
#include <quickjs.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

struct reist_js_engine {
    reist_js_config config;
    JSRuntime *runtime;
    JSContext *context;
    size_t used,peak,count;
    uint64_t last,deadline;
    reist_js_status poisoned;
    unsigned running,oom;
};
typedef union {
    max_align_t align;
    struct {size_t bytes; uint32_t magic; reist_js_engine *owner;} data;
} block_header;
#define BLOCK_MAGIC 0x4a534231U

static void *allocate(JSMallocState *state,size_t bytes) {
    reist_js_engine *engine=state->opaque;
    if (!bytes) return NULL;
    if (bytes>SIZE_MAX-sizeof(block_header) ||
        bytes+sizeof(block_header)>engine->config.memory_limit-engine->used) {
        engine->oom=1; return NULL;
    }
    block_header *block=malloc(bytes+sizeof(*block));
    if (!block) { engine->oom=1; return NULL; }
    block->data.bytes=bytes; block->data.magic=BLOCK_MAGIC; block->data.owner=engine;
    engine->used+=bytes+sizeof(*block); ++engine->count;
    if(engine->used>engine->peak) engine->peak=engine->used;
    state->malloc_size+=bytes+sizeof(*block); ++state->malloc_count;
    return block+1;
}
static block_header *header(const void *pointer) {
    block_header *block=(block_header *)pointer-1;
    if(block->data.magic!=BLOCK_MAGIC) abort();
    return block;
}
static size_t usable(const void *pointer) { return pointer?header(pointer)->data.bytes:0; }
static void release(JSMallocState *state,void *pointer) {
    if(!pointer) return;
    block_header *block=header(pointer); reist_js_engine *engine=state->opaque;
    size_t total=block->data.bytes+sizeof(*block);
    if(block->data.owner!=engine || total>engine->used || !engine->count ||
       total>state->malloc_size || !state->malloc_count) abort();
    engine->used-=total; --engine->count;
    state->malloc_size-=total; --state->malloc_count;
    block->data.magic=0; free(block);
}
static void *resize(JSMallocState *state,void *pointer,size_t bytes) {
    if(!pointer) return allocate(state,bytes);
    if(!bytes) { release(state,pointer); return NULL; }
    block_header *old=header(pointer); reist_js_engine *engine=state->opaque;
    if(old->data.owner!=engine) abort();
    size_t previous=old->data.bytes;
    if(bytes>SIZE_MAX-sizeof(*old) || (bytes>previous &&
        bytes-previous>engine->config.memory_limit-engine->used)) { engine->oom=1; return NULL; }
    block_header *replacement=realloc(old,bytes+sizeof(*old));
    if(!replacement) { engine->oom=1; return NULL; }
    replacement->data.bytes=bytes;
    engine->used=engine->used-previous+bytes;
    state->malloc_size=state->malloc_size-previous+bytes;
    if(engine->used>engine->peak) engine->peak=engine->used;
    return replacement+1;
}
static const JSMallocFunctions allocator={allocate,release,resize,usable};
uint64_t reist_js_seed(void *owner) { return ((reist_js_engine *)owner)->config.seed; }

static int interrupted(JSRuntime *runtime,void *owner) {
    (void)runtime; reist_js_engine *engine=owner; uint64_t now=0;
    if(engine->poisoned) return 1;
    if(engine->config.monotonic_ms(engine->config.clock_context,&now) ||
       now<engine->last || now>=engine->deadline) {
        engine->poisoned=REIST_JS_DEADLINE; return 1;
    }
    engine->last=now;
    if(engine->oom) { engine->poisoned=REIST_JS_OOM; return 1; }
    return 0;
}
static reist_js_status begin(reist_js_engine *engine,uint64_t deadline) {
    if(!engine || engine->running) return REIST_JS_INVALID;
    if(engine->poisoned) return REIST_JS_CLOSED;
    engine->deadline=deadline;
    if(interrupted(engine->runtime,engine)) return engine->poisoned;
    JS_UpdateStackTop(engine->runtime);
    engine->running=1; return REIST_JS_OK;
}
static reist_js_status finish(reist_js_engine *engine,reist_js_status status) {
    (void)interrupted(engine->runtime,engine);
    if(engine->oom && !engine->poisoned) engine->poisoned=REIST_JS_OOM;
    if(status==REIST_JS_LIMIT && !engine->poisoned) engine->poisoned=status;
    engine->running=0;
    return engine->poisoned?engine->poisoned:status;
}
void reist_js_destroy(reist_js_engine **owner) {
    if(!owner || !*owner) return;
    reist_js_engine *engine=*owner; *owner=NULL;
    if(engine->running) abort();
    if(engine->context) JS_FreeContext(engine->context);
    if(engine->runtime) JS_FreeRuntime(engine->runtime);
    if(engine->used || engine->count) abort();
    free(engine);
}
reist_js_engine *reist_js_create(const reist_js_config *config,reist_js_status *status) {
    if(!status) return NULL;
    *status=REIST_JS_INVALID;
    if(!config || config->version!=REIST_JS_VERSION || config->struct_size!=sizeof(*config) ||
       config->reserved || !config->seed || !config->monotonic_ms ||
       config->memory_limit<1024U*1024U || config->memory_limit>128U*1024U*1024U ||
       config->stack_limit<4096 || config->stack_limit>16384 ||
       !config->source_limit || config->source_limit>REIST_JS_SOURCE_MAX ||
       !config->result_limit || config->result_limit>REIST_JS_RESULT_MAX ||
       !config->job_limit || config->job_limit>1024 || fegetround()!=FE_TONEAREST) return NULL;
    reist_js_engine *engine=calloc(1,sizeof(*engine));
    if(!engine) { *status=REIST_JS_OOM; return NULL; }
    engine->config=*config;
    engine->runtime=JS_NewRuntime2(&allocator,engine);
    if(!engine->runtime) goto failed;
    JS_SetRuntimeOpaque(engine->runtime,engine);
    JS_SetMemoryLimit(engine->runtime,config->memory_limit);
    JS_SetMaxStackSize(engine->runtime,config->stack_limit);
    engine->context=JS_NewContextRaw(engine->runtime);
    if(!engine->context) goto failed;
    JSContext *ctx=engine->context;
    if(JS_AddIntrinsicBaseObjects(ctx) || JS_AddIntrinsicEval(ctx) ||
       JS_AddIntrinsicStringNormalize(ctx) || JS_AddIntrinsicRegExp(ctx) ||
       JS_AddIntrinsicJSON(ctx) || JS_AddIntrinsicProxy(ctx) || JS_AddIntrinsicMapSet(ctx) ||
       JS_AddIntrinsicTypedArrays(ctx) || JS_AddIntrinsicPromise(ctx) || JS_AddIntrinsicWeakRef(ctx)) goto failed;
    JSValue global=JS_GetGlobalObject(ctx);
    JSAtom atom=JS_NewAtom(ctx,"SharedArrayBuffer");
    int removed=atom?JS_DeleteProperty(ctx,global,atom,0):-1;
    JS_FreeAtom(ctx,atom); JS_FreeValue(ctx,global);
    if(removed<0 || engine->oom) goto failed;
    JS_SetInterruptHandler(engine->runtime,interrupted,engine);
    *status=REIST_JS_OK; return engine;
failed:
    *status=engine->oom?REIST_JS_OOM:REIST_JS_EXCEPTION;
    reist_js_destroy(&engine); return NULL;
}
reist_js_status reist_js_eval(reist_js_engine *engine,const char *source,size_t length,
    uint64_t deadline,char *output,size_t capacity,size_t *required) {
    if(!engine || !source || !output || !capacity || !required || engine->running ||
       capacity>REIST_JS_RESULT_MAX) return REIST_JS_INVALID;
    *required=0; output[0]=0;
    if(engine->poisoned) return REIST_JS_CLOSED;
    if(length>engine->config.source_limit) { engine->poisoned=REIST_JS_LIMIT; return REIST_JS_LIMIT; }
    if(memchr(source,0,length)) return REIST_JS_INVALID;
    reist_js_status status=begin(engine,deadline);
    if(status) return status;
    JSContext *ctx=engine->context;
    char *copy=js_malloc(ctx,length+1);
    JSValue value=JS_UNDEFINED;
    const char *text=NULL;
    if(!copy) { status=REIST_JS_OOM; goto done; }
    memcpy(copy,source,length); copy[length]=0;
    value=JS_Eval(ctx,copy,length,"<reist>",JS_EVAL_TYPE_GLOBAL);
    js_free(ctx,copy);
    if(JS_IsException(value)) { status=REIST_JS_EXCEPTION; goto exception; }
    for(uint32_t jobs=0;JS_IsJobPending(engine->runtime);++jobs) {
        if(interrupted(engine->runtime,engine)) goto done;
        if(jobs==engine->config.job_limit) { status=REIST_JS_LIMIT; goto done; }
        JSContext *job_context=NULL;
        if(JS_ExecutePendingJob(engine->runtime,&job_context)<0) {
            JSValue exception=JS_GetException(job_context); JS_FreeValue(job_context,exception);
            status=REIST_JS_EXCEPTION; goto done;
        }
    }
    text=JS_ToCStringLen(ctx,required,value);
    if(!text) { status=REIST_JS_EXCEPTION; goto exception; }
    if(*required>=capacity || *required>=engine->config.result_limit) { status=REIST_JS_LIMIT; goto done; }
    if(!interrupted(engine->runtime,engine)) memcpy(output,text,*required+1);
    goto done;
exception:
    { JSValue exception=JS_GetException(ctx);
      JS_FreeValue(ctx,exception); }
done:
    if(text) JS_FreeCString(ctx,text);
    JS_FreeValue(ctx,value);
    status=finish(engine,status);
    if(status) output[0]=0;
    return status;
}
reist_js_status reist_js_collect(reist_js_engine *engine,uint64_t deadline) {
    reist_js_status status=begin(engine,deadline);
    if(status) return status;
    JS_RunGC(engine->runtime); return finish(engine,REIST_JS_OK);
}
int reist_js_get_stats(const reist_js_engine *engine,reist_js_stats *stats) {
    if(!engine || !stats || stats->version!=REIST_JS_VERSION || stats->struct_size!=sizeof(*stats)) return -1;
    stats->live_allocations=(uint32_t)engine->count; stats->live_bytes=(uint32_t)engine->used;
    stats->peak_bytes=(uint32_t)engine->peak; stats->poisoned=(uint32_t)engine->poisoned;
    return 0;
}
