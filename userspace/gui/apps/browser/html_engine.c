/* Real Hubbub tree callbacks. All tree storage belongs to one worker request;
 * retired slots are never recycled, so a stale pointer cannot alias a new node.
 * The complete pool is released with the worker address space. */
#include "html_engine.h"
#include "script_protocol.h"
#include <hubbub/parser.h>
#include <parserutils/charset/utf16.h>
#include <parserutils/charset/utf8.h>
#include <reist/libc.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#define NODES 2048U
#define ATTRS 4096U
#define STRINGS (256U*1024U)
#define DEPTH 128U
#define WORK 262144U
static node legacy_nodes[NODES];
static attribute legacy_attributes[ATTRS];
static char legacy_strings[STRINGS];
static browser_html_script_hook script_hook;
static void *script_context;
void browser_html_script_hook_set(browser_html_script_hook hook,void *context) {
    script_hook=hook; script_context=context;
}
int browser_html_script_enabled(void) { return script_hook!=NULL; }
static struct {
    node *nodes;
    attribute *attributes;
    char *strings;
    uint8_t *converted;
    uint32_t node_limit,attribute_limit,string_limit,work_limit;
    uint32_t extended,encoding,encoding_fixed,encoding_requested;
    uint32_t count, attribute_count, used, work, failed, quirks;
    uint8_t projection[REIST_HTML_INPUT_CAPACITY];
    uint32_t projected;
} tree;

static hubbub_error step(void) {
    if (tree.failed || ++tree.work > tree.work_limit) { tree.failed=1; return HUBBUB_NOMEM; }
    return HUBBUB_OK;
}
static node *checked(void *value) {
    uintptr_t address=(uintptr_t)value, base=(uintptr_t)tree.nodes;
    if (address<base || address-base >= tree.count*sizeof(node) ||
        (address-base)%sizeof(node)) { tree.failed=1; return NULL; }
    return value;
}
static char *copy(const uint8_t *text, size_t length) {
    if (length>=tree.string_limit-tree.used) { tree.failed=1; return NULL; }
    char *out=tree.strings+tree.used;
    memcpy(out,text,length); out[length]=0;
    tree.used+=(uint32_t)length+1; return out;
}
static node *create(uint32_t kind) {
    if (step()!=HUBBUB_OK || tree.count==tree.node_limit) { tree.failed=1; return NULL; }
    node *n=&tree.nodes[tree.count++];
    *n=(node){.kind=kind,.refs=1}; return n;
}
static hubbub_error text_node(void *ctx, const hubbub_string *data, void **out) {
    (void)ctx; *out=NULL;
    node *n=create(3);
    if (!n || !(n->text=copy(data->ptr,data->len))) return HUBBUB_NOMEM;
    n->length=data->len; *out=n; return HUBBUB_OK;
}
static hubbub_error comment_node(void *ctx, const hubbub_string *data, void **out) {
    (void)ctx; (void)data; *out=create(8); return *out ? HUBBUB_OK : HUBBUB_NOMEM;
}
static hubbub_error doctype_node(void *ctx, const hubbub_doctype *data, void **out) {
    (void)ctx; (void)data; *out=create(10); return *out ? HUBBUB_OK : HUBBUB_NOMEM;
}
static hubbub_error add_attributes(void *ctx, void *value, const hubbub_attribute *attrs, uint32_t count) {
    (void)ctx;
    node *n=checked(value);
    if (!n || step()!=HUBBUB_OK || count>tree.attribute_limit-tree.attribute_count) return HUBBUB_NOMEM;
    for (uint32_t i=0; i<count; ++i) {
        attribute *existing=n->attributes;
        for (; existing; existing=existing->next) {
            if (step()!=HUBBUB_OK) return HUBBUB_NOMEM;
            if (strlen(existing->name)==attrs[i].name.len &&
                !memcmp(existing->name,attrs[i].name.ptr,attrs[i].name.len)) break;
        }
        if (existing) continue;
        attribute *a=&tree.attributes[tree.attribute_count++];
        a->name=copy(attrs[i].name.ptr,attrs[i].name.len);
        a->value=copy(attrs[i].value.ptr,attrs[i].value.len);
        if (!a->name || !a->value) return HUBBUB_NOMEM;
        a->next=n->attributes; n->attributes=a;
    }
    return HUBBUB_OK;
}
static hubbub_error element_node(void *ctx, const hubbub_tag *tag, void **out) {
    *out=NULL; node *n=create(1);
    if (!n || !(n->name=copy(tag->name.ptr,tag->name.len))) return HUBBUB_NOMEM;
    n->ns=tag->ns;
    hubbub_error error=add_attributes(ctx,n,tag->attributes,tag->n_attributes);
    if (error==HUBBUB_OK) *out=n;
    return error;
}
static hubbub_error ref_node(void *ctx, void *value) {
    (void)ctx; node *n=checked(value);
    if (!n || step()!=HUBBUB_OK || n->refs==UINT32_MAX) return HUBBUB_NOMEM;
    ++n->refs; return HUBBUB_OK;
}
static hubbub_error unref_node(void *ctx, void *value) {
    (void)ctx; node *n=checked(value);
    if (!n || !n->refs) { tree.failed=1; return HUBBUB_BADPARM; }
    --n->refs; return HUBBUB_OK;
}
static void detach(node *n) {
    if (!n->parent) return;
    if (n->previous) n->previous->next=n->next; else n->parent->first=n->next;
    if (n->next) n->next->previous=n->previous; else n->parent->last=n->previous;
    n->parent=n->previous=n->next=NULL;
}
static hubbub_error insert(void *ctx, void *parent, void *child, void *before, void **out) {
    *out=NULL;
    node *p=checked(parent), *n=checked(child), *b=before ? checked(before) : NULL;
    if (!p || !n || (before && (!b || b->parent!=p)) || n==b) return HUBBUB_BADPARM;
    uint32_t depth=0;
    for (node *a=p; a; a=a->parent) {
        if (step()!=HUBBUB_OK || ++depth>DEPTH || a==n) { tree.failed=1; return HUBBUB_NOMEM; }
    }
    if (ref_node(ctx,n)!=HUBBUB_OK) return HUBBUB_NOMEM;
    detach(n); n->parent=p; n->next=b;
    n->previous=b ? b->previous : p->last;
    if (n->previous) n->previous->next=n; else p->first=n;
    if (b) b->previous=n; else p->last=n;
    *out=n; return HUBBUB_OK;
}
static hubbub_error append(void *ctx, void *parent, void *child, void **out) {
    return insert(ctx,parent,child,NULL,out);
}
static hubbub_error remove_child(void *ctx, void *parent, void *child, void **out) {
    *out=NULL; node *p=checked(parent), *n=checked(child);
    if (!p || !n || n->parent!=p) return HUBBUB_BADPARM;
    if (ref_node(ctx,n)!=HUBBUB_OK) return HUBBUB_NOMEM;
    detach(n); *out=n; return HUBBUB_OK;
}
static hubbub_error clone_at(node *source, node **out, bool deep, uint32_t depth) {
    *out=NULL;
    if (depth>=DEPTH) { tree.failed=1; return HUBBUB_NOMEM; }
    node *n=create(source->kind);
    if (!n) return HUBBUB_NOMEM;
    n->name=source->name; n->text=source->text; n->length=source->length; n->ns=source->ns;
    for (attribute *a=source->attributes; a; a=a->next) {
        if (tree.attribute_count==tree.attribute_limit || step()!=HUBBUB_OK) { tree.failed=1; return HUBBUB_NOMEM; }
        attribute *b=&tree.attributes[tree.attribute_count++];
        *b=*a; b->next=n->attributes; n->attributes=b;
    }
    if (deep) for (node *child=source->first; child; child=child->next) {
        node *cloned; void *added;
        if (clone_at(child,&cloned,true,depth+1)!=HUBBUB_OK ||
            append(NULL,n,cloned,&added)!=HUBBUB_OK) return HUBBUB_NOMEM;
        unref_node(NULL,added); unref_node(NULL,cloned);
    }
    *out=n; return HUBBUB_OK;
}
static hubbub_error clone_node(void *ctx, void *value, bool deep, void **out) {
    (void)ctx; node *n=checked(value), *copy_node=NULL;
    hubbub_error error=n ? clone_at(n,&copy_node,deep,0) : HUBBUB_BADPARM;
    *out=copy_node; return error;
}
static hubbub_error reparent(void *ctx, void *value, void *parent) {
    node *n=checked(value), *p=checked(parent);
    if (!n || !p || n==p) return HUBBUB_BADPARM;
    while (n->first) {
        void *out;
        if (append(ctx,p,n->first,&out)!=HUBBUB_OK) return HUBBUB_NOMEM;
        unref_node(ctx,out);
    }
    return HUBBUB_OK;
}
static hubbub_error get_parent(void *ctx, void *value, bool element_only, void **out) {
    node *n=checked(value); *out=NULL;
    if (!n || step()!=HUBBUB_OK) return HUBBUB_NOMEM;
    if (n->parent && (!element_only || n->parent->kind==1)) {
        if (ref_node(ctx,n->parent)!=HUBBUB_OK) return HUBBUB_NOMEM;
        *out=n->parent;
    }
    return HUBBUB_OK;
}
static hubbub_error has_children(void *ctx, void *value, bool *out) {
    (void)ctx; node *n=checked(value);
    if (!n || step()!=HUBBUB_OK) return HUBBUB_NOMEM;
    *out=n->first!=NULL; return HUBBUB_OK;
}
static hubbub_error form_associate(void *ctx, void *form, void *value) {
    (void)ctx; node *n=checked(value), *f=checked(form);
    if (!n || !f || step()!=HUBBUB_OK) return HUBBUB_NOMEM;
    n->form=f; return HUBBUB_OK;
}
static hubbub_error quirks(void *ctx, hubbub_quirks_mode mode) {
    (void)ctx; tree.quirks=(uint32_t)mode; return step();
}
static hubbub_error encoding(void *ctx, const char *name) {
    (void)ctx;
    if(tree.extended) {
        if(tree.encoding_fixed) return step(); /* transport/BOM wins over meta */
        uint32_t selected=browser_encoding_label(name,name ? strlen(name) : 0);
        /* HTML meta UTF-16 labels are interpreted as UTF-8. */
        if(selected==BROWSER_ENCODING_UTF16LE || selected==BROWSER_ENCODING_UTF16BE) selected=BROWSER_ENCODING_UTF8;
        if(selected==tree.encoding) { tree.encoding_fixed=1; return step(); }
        tree.encoding_requested=selected;
        return HUBBUB_ENCODINGCHANGE;
    }
    /* Hubbub reports meta declarations even when the decoder is already fixed.
     * Confirming UTF-8 is not a request to restart/change that decoder. */
    if (name && strlen(name)==5U && !strncasecmp(name,"UTF-8",5U)) return step();
    /* Other declarations require an unsupported charset/restart adapter. */
    return HUBBUB_ENCODINGCHANGE;
}
static int equal_ascii(const char *a,const char *b) {
    size_t n=strlen(b); return strlen(a)==n && !strncasecmp(a,b,n);
}
static hubbub_error script(void *ctx, void *value) {
    (void)ctx; node *n=checked(value);
    if(!n || step()!=HUBBUB_OK) return HUBBUB_NOMEM;
    if(!script_hook || !tree.extended || n->ns!=HUBBUB_NS_HTML) return HUBBUB_OK;
    for(node *p=n->parent;p;p=p->parent)
        if(p->kind==1 && (p->ns!=HUBBUB_NS_HTML || !strcmp(p->name,"template"))) return HUBBUB_OK;
    const char *type=NULL,*language=NULL;
    for(attribute *a=n->attributes;a;a=a->next) {
        if(!strcmp(a->name,"src")) return HUBBUB_OK;
        if(!strcmp(a->name,"type")) type=a->value;
        if(!strcmp(a->name,"language")) language=a->value;
    }
    if(type && *type && !equal_ascii(type,"text/javascript") && !equal_ascii(type,"application/javascript") &&
       !equal_ascii(type,"text/ecmascript") && !equal_ascii(type,"application/ecmascript") &&
       !equal_ascii(type,"application/x-javascript") && !equal_ascii(type,"text/jscript")) return HUBBUB_OK;
    if(!type && language && *language && !equal_ascii(language,"javascript") && !equal_ascii(language,"ecmascript")) return HUBBUB_OK;
    /* Unsupported enforcing meta policies conservatively disable scripting. */
    for(uint32_t i=0;i<tree.count;++i) {
        node *meta=&tree.nodes[i];
        if(meta->kind!=1 || meta->ns!=HUBBUB_NS_HTML || strcmp(meta->name,"meta")) continue;
        for(attribute *a=meta->attributes;a;a=a->next)
            if(!strcmp(a->name,"http-equiv") && equal_ascii(a->value,"content-security-policy")) return HUBBUB_OK;
    }
    /* An executing script makes the current character encoding certain. */
    tree.encoding_fixed=1;
    if(script_hook(script_context,n)) { tree.failed=1; return HUBBUB_NOMEM; }
    return HUBBUB_OK;
}
static int emit(const char *text, size_t length) {
    if (length>sizeof(tree.projection)-tree.projected) return -28;
    memcpy(tree.projection+tree.projected,text,length); tree.projected+=(uint32_t)length; return 0;
}
static int escaped(const char *text, size_t length) {
    for (size_t i=0; i<length; ++i) {
        const char *replacement=text[i]=='&' ? "&amp;" : text[i]=='<' ? "&lt;" : text[i]=='>' ? "&gt;" : text[i]=='"' ? "&quot;" : NULL;
        if (emit(replacement ? replacement : text+i,replacement ? strlen(replacement) : 1)) return -28;
    }
    return 0;
}
static int named(const char *name, const char *list) {
    size_t n=strlen(name);
    while (*list) {
        const char *end=strchr(list,' '); size_t length=end ? (size_t)(end-list) : strlen(list);
        if (n==length && !memcmp(name,list,n)) return 1;
        if (!end) break;
        list=end+1;
    }
    return 0;
}
static int project(node *n, uint32_t depth) {
    if (depth>=DEPTH || step()!=HUBBUB_OK) return -28;
    if (n->kind==3) return escaped(n->text,n->length);
    if (n->kind!=1 && n->kind!=9) return 0;
    if (n->kind==1 && (n->ns!=HUBBUB_NS_HTML || named(n->name,"script style template"))) return 0;
    const char *tag=NULL;
    if (n->kind==1 && named(n->name,"html head body title p div h1 h2 h3 h4 h5 h6 br strong b em i pre ul ol li a img section article header footer main nav table tr")) tag=n->name;
    if (tag) {
        if (emit("<",1) || emit(tag,strlen(tag))) return -28;
        for (attribute *a=n->attributes; a; a=a->next) {
            if (!named(a->name,"id name href src alt width height")) continue;
            if (emit(" ",1) || emit(a->name,strlen(a->name)) || emit("=\"",2) ||
                escaped(a->value,strlen(a->value)) || emit("\"",1)) return -28;
        }
        if (emit(">",1)) return -28;
    }
    for (node *child=n->first; child; child=child->next)
        if (project(child,depth+1)) return -28;
    if (tag && !named(tag,"br img"))
        if (emit("</",2) || emit(tag,strlen(tag)) || emit(">",1)) return -28;
    return 0;
}

static int decode_utf16(const uint8_t *input,size_t length,uint32_t big,size_t *decoded) {
    size_t capacity=length*2U+4U;
    tree.converted=malloc(capacity); if(!tree.converted) return -12;
    uint8_t *out=tree.converted; size_t left=capacity;
    for(size_t i=0;i<length;) {
        uint16_t units[2]={0,0}; size_t available=length-i>=4 ? 4 : length-i>=2 ? 2 : 0;
        for(size_t j=0;j<available;j+=2)
            units[j/2]=(uint16_t)(big ? ((uint16_t)input[i+j]<<8)|input[i+j+1] : input[i+j]|((uint16_t)input[i+j+1]<<8));
        uint32_t scalar=0xfffd; size_t consumed=available ? 2 : 1;
        if(available && parserutils_charset_utf16_to_ucs4((const uint8_t *)units,available,&scalar,&consumed)!=PARSERUTILS_OK)
            { scalar=0xfffd; consumed=2; }
        if(parserutils_charset_utf8_from_ucs4(scalar,&out,&left)!=PARSERUTILS_OK) return -84;
        i+=consumed;
    }
    *decoded=(size_t)(out-tree.converted); return 0;
}
static int parse_tree(const uint8_t *input,size_t length,node **result,uint32_t private_heap,
                      uint32_t extended,uint32_t selected) {
    if (!input || !result || !length || length>(extended ? BROWSER_DOCUMENT_INPUT_CAPACITY : REIST_HTML_INPUT_CAPACITY) ||
        selected>BROWSER_ENCODING_UTF16BE) return -22;
    *result=NULL;
    memset(&tree,0,sizeof(tree));
    /* Both profiles use the existing demand-backed private provider. Legacy
     * admission still has its original 4-MiB heap budget; do not map/reap an
     * unused 4-MiB BSS arena in every extended worker generation. */
    if (reist_libc_init_process(private_heap ? 32U*1024U*1024U : REIST_LIBC_HEAP_LIMIT)) return -12;
    tree.node_limit=extended ? 8192 : NODES; tree.attribute_limit=extended ? 16384 : ATTRS;
    tree.string_limit=extended ? 4U*1024U*1024U : STRINGS; tree.work_limit=extended ? 1048576 : WORK;
    tree.extended=extended;
    tree.nodes=extended ? malloc(tree.node_limit*sizeof(node)) : legacy_nodes;
    tree.attributes=extended ? malloc(tree.attribute_limit*sizeof(attribute)) : legacy_attributes;
    tree.strings=extended ? malloc(tree.string_limit) : legacy_strings;
    if(!tree.nodes || !tree.attributes || !tree.strings) return -12;
    tree.extended=extended; tree.encoding_fixed=selected!=BROWSER_ENCODING_AUTO;
    if(extended && length>=3 && input[0]==0xef && input[1]==0xbb && input[2]==0xbf)
        { selected=BROWSER_ENCODING_UTF8; tree.encoding_fixed=1; }
    else if(extended && length>=2 && input[0]==0xff && input[1]==0xfe)
        { selected=BROWSER_ENCODING_UTF16LE; tree.encoding_fixed=1; }
    else if(extended && length>=2 && input[0]==0xfe && input[1]==0xff)
        { selected=BROWSER_ENCODING_UTF16BE; tree.encoding_fixed=1; }
    tree.encoding=selected ? selected : BROWSER_ENCODING_UTF8;
    /* Pinned ParserUtils supplies a native-endian UTF-16 scalar decoder, not
     * UTF-16LE/BE named codecs. Adapt byte order before its existing UTF-8
     * encoder, preserving BOM/transport authority and HTML replacement rules. */
    if(tree.encoding==BROWSER_ENCODING_UTF16LE || tree.encoding==BROWSER_ENCODING_UTF16BE) {
        size_t decoded=0;
        int rc=decode_utf16(input,length,tree.encoding==BROWSER_ENCODING_UTF16BE,&decoded);
        if(rc) return rc;
        input=tree.converted; length=decoded; tree.encoding=BROWSER_ENCODING_UTF8;
    }
    for(unsigned attempt=0;attempt<(extended ? 2U : 1U);++attempt) {
    tree.count=tree.attribute_count=tree.used=tree.work=tree.failed=tree.quirks=0;
    tree.encoding_requested=0;
    node *root=create(9);
    hubbub_parser *parser=NULL;
    hubbub_tree_handler handler={comment_node,doctype_node,element_node,text_node,
        ref_node,unref_node,append,insert,remove_child,clone_node,reparent,
        get_parent,has_children,form_associate,add_attributes,quirks,encoding,script,NULL};
    hubbub_parser_optparams option={.tree_handler=&handler};
    const char *charset=tree.encoding==BROWSER_ENCODING_WINDOWS1252 ? "Windows-1252" :
        tree.encoding==BROWSER_ENCODING_UTF16LE ? "UTF-16LE" : tree.encoding==BROWSER_ENCODING_UTF16BE ? "UTF-16BE" : "UTF-8";
    hubbub_error error=hubbub_parser_create(charset,true,&parser);
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_TREE_HANDLER,&option);
    option.document_node=root;
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_DOCUMENT_NODE,&option);
    option.enable_scripting=extended && script_hook;
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_ENABLE_SCRIPTING,&option);
    size_t chunk=extended ? 4096 : 256;
    for (size_t offset=0; error==HUBBUB_OK && !tree.failed && offset<length; offset+=chunk) {
        size_t amount=length-offset; if (amount>chunk) amount=chunk;
        error=hubbub_parser_parse_chunk(parser,input+offset,amount);
    }
    if (error==HUBBUB_OK && !tree.failed) error=hubbub_parser_completed(parser);
    if (parser) hubbub_parser_destroy(parser);
    if (error==HUBBUB_ENCODINGCHANGE) {
        if(!extended || attempt || !tree.encoding_requested || tree.encoding_requested>BROWSER_ENCODING_UTF16BE) return -84;
        tree.encoding=tree.encoding_requested; tree.encoding_fixed=1; continue;
    }
    if (error!=HUBBUB_OK || tree.failed) return -28;
    *result=root;
    return 0;
    }
    return -84;
}
int browser_html5_document_tree(const uint8_t *input,size_t length,uint32_t encoding,node **root) {
    int rc=parse_tree(input,length,root,1,1,encoding);
    if(rc) browser_html5_document_release();
    return rc;
}
void browser_html5_document_release(void) {
    if(!tree.extended) return;
    free(tree.nodes); free(tree.attributes); free(tree.strings); free(tree.converted);
    tree.nodes=NULL; tree.attributes=NULL; tree.strings=NULL; tree.extended=0;
}
int browser_html5_tree_with_heap(const uint8_t *input,size_t length,node **root,uint32_t private_heap) {
    return parse_tree(input,length,root,private_heap,0,BROWSER_ENCODING_UTF8);
}
int browser_html5_tree(const uint8_t *input,size_t length,node **result) {
    return browser_html5_tree_with_heap(input,length,result,0);
}
int browser_html5_parse(const uint8_t *input, size_t length, reist_html_document_t *document) {
    if (!document) return -22;
    node *root;
    int result=browser_html5_tree(input,length,&root);
    if (result) return result;
    if (project(root,0)) return -28;
    return reist_html_document_parse(tree.projection,tree.projected,document);
}

typedef struct script_writer { char *data; uint32_t at,capacity; } script_writer;
static int script_bytes(script_writer *w,const char *data,size_t size) {
    if(size>w->capacity-w->at) return -28;
    memcpy(w->data+w->at,data,size); w->at+=(uint32_t)size; return 0;
}
static int script_number(script_writer *w,uint32_t value) {
    char reversed[10],out[10]; uint32_t n=0;
    do { reversed[n++]=(char)('0'+value%10); value/=10; } while(value);
    for(uint32_t i=0;i<n;++i) out[i]=reversed[n-i-1];
    return script_bytes(w,out,n);
}
static int script_string(script_writer *w,const char *data,size_t size) {
    static const char hex[]="0123456789abcdef";
    if(script_bytes(w,"\"",1)) return -28;
    for(size_t i=0;i<size;++i) {
        uint8_t c=(uint8_t)data[i];
        if(c<32) {
            char escaped[]={'\\','u','0','0',hex[c>>4],hex[c&15]};
            if(script_bytes(w,escaped,sizeof(escaped))) return -28;
        } else {
            if((c=='"' || c=='\\') && script_bytes(w,"\\",1)) return -28;
            if(script_bytes(w,data+i,1)) return -28;
        }
    }
    return script_bytes(w,"\"",1);
}
static uint32_t script_id(node *n) { return n ? (uint32_t)(n-tree.nodes)+1 : 0; }
int browser_html_script_snapshot(node *script_node,const char *url,char *output,uint32_t capacity,
    uint32_t *snapshot_size,uint32_t *source_size) {
    if(!tree.extended || !script_node || !url || !output || !snapshot_size || !source_size ||
       capacity<BROWSER_SCRIPT_SNAPSHOT) return -22;
    script_writer w={output,0,BROWSER_SCRIPT_SNAPSHOT};
    if(script_bytes(&w,"__reistDOM.sync(",16) || script_string(&w,url,strlen(url)) || script_bytes(&w,",[",2)) return -28;
    for(uint32_t i=0;i<tree.count;++i) {
        node *n=&tree.nodes[i]; const char *id="";
        for(attribute *a=n->attributes;a;a=a->next) if(!strcmp(a->name,"id")) { id=a->value; break; }
        if((i && script_bytes(&w,",",1)) || script_bytes(&w,"[",1)) return -28;
        uint32_t fields[]={n->kind,n->ns,script_id(n->parent),script_id(n->first),script_id(n->next)};
        for(unsigned j=0;j<5;++j) if(script_number(&w,fields[j]) || script_bytes(&w,",",1)) return -28;
        if(script_string(&w,n->name?n->name:"",n->name?strlen(n->name):0) || script_bytes(&w,",",1) ||
           script_string(&w,id,strlen(id)) || script_bytes(&w,",",1) ||
           script_string(&w,n->text?n->text:"",n->text?n->length:0) || script_bytes(&w,"]",1)) return -28;
    }
    if(script_bytes(&w,"]);",3)) return -28;
    *snapshot_size=w.at; w.capacity=capacity;
    for(node *n=script_node->first;n;n=n->next) {
        if(n->kind!=3 || script_bytes(&w,n->text,n->length)) return -28;
    }
    *source_size=w.at-*snapshot_size;
    return *source_size<=BROWSER_SCRIPT_SOURCE ? 0 : -28;
}
static node *script_find(node *root,const char *name) {
    node *n=root; uint32_t visited=0;
    while(n && ++visited<=tree.node_limit) {
        if(n->kind==1 && n->ns==HUBBUB_NS_HTML && !strcmp(n->name,name)) return n;
        if(n->first) { n=n->first; continue; }
        while(n!=root && !n->next) n=n->parent;
        if(n==root) break;
        n=n->next;
    }
    return NULL;
}
int browser_html_script_apply(const char *journal,uint32_t size) {
    browser_script_mutation_t items[BROWSER_SCRIPT_MUTATIONS]; uint32_t count=0,bytes=0;
    if(!tree.extended || browser_script_journal(journal,size,items,&count,&bytes)) return -84;
    /* Reserve all cumulative work, strings and retired-node slots before the
     * first mutation. Detached nodes are never recycled into stale JS IDs. */
    if(count>(tree.node_limit-tree.count)/3 || bytes+count*20>tree.string_limit-tree.used ||
       (count && (tree.count*2+12)>(tree.work_limit-tree.work)/count)) return -28;
    node *head=NULL;
    for(uint32_t i=0;i<count;++i) {
        if(items[i].node) {
            if(items[i].node>tree.count || tree.nodes[items[i].node-1].kind!=1) return -84;
        } else {
            head=script_find(&tree.nodes[0],"head"); if(!head) return -84;
        }
    }
    tree.work+=count*tree.count*2;
    for(uint32_t i=0;i<count;++i) {
        browser_script_mutation_t *item=&items[i];
        node *target=item->node ? &tree.nodes[item->node-1] : script_find(&tree.nodes[0],"title");
        if(!target) {
            target=create(1); if(!target) return -28;
            target->name=copy((const uint8_t *)"title",5); target->ns=HUBBUB_NS_HTML;
            void *added; if(!target->name || append(NULL,head,target,&added)!=HUBBUB_OK) return -28;
        }
        node *replacement=NULL;
        if(item->length) {
            replacement=create(3); if(!replacement) return -28;
            replacement->text=tree.strings+tree.used; replacement->length=item->length;
            if(browser_script_unhex(journal+item->offset,item->length,replacement->text)) return -84;
            replacement->text[item->length]=0; tree.used+=item->length+1;
        }
        while(target->first) detach(target->first);
        if(replacement) { void *added; if(append(NULL,target,replacement,&added)!=HUBBUB_OK) return -28; }
    }
    return 0;
}
