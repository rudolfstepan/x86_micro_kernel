/* Real Hubbub tree callbacks. All tree storage belongs to one worker request;
 * retired slots are never recycled, so a stale pointer cannot alias a new node.
 * The complete pool is released with the worker address space. */
#include "html_engine.h"
#include <hubbub/parser.h>
#include <reist/libc.h>
#include <string.h>
#include <strings.h>

#define NODES 2048U
#define ATTRS 4096U
#define STRINGS (256U*1024U)
#define DEPTH 128U
#define WORK 262144U
static struct {
    node nodes[NODES];
    attribute attributes[ATTRS];
    char strings[STRINGS];
    uint32_t count, attribute_count, used, work, failed, quirks;
    uint8_t projection[REIST_HTML_INPUT_CAPACITY];
    uint32_t projected;
} tree;
static _Alignas(max_align_t) uint8_t arena[REIST_LIBC_HEAP_LIMIT];

static hubbub_error step(void) {
    if (tree.failed || ++tree.work > WORK) { tree.failed=1; return HUBBUB_NOMEM; }
    return HUBBUB_OK;
}
static node *checked(void *value) {
    uintptr_t address=(uintptr_t)value, base=(uintptr_t)tree.nodes;
    if (address<base || address-base >= tree.count*sizeof(node) ||
        (address-base)%sizeof(node)) { tree.failed=1; return NULL; }
    return value;
}
static char *copy(const uint8_t *text, size_t length) {
    if (length>=STRINGS-tree.used) { tree.failed=1; return NULL; }
    char *out=tree.strings+tree.used;
    memcpy(out,text,length); out[length]=0;
    tree.used+=(uint32_t)length+1; return out;
}
static node *create(uint32_t kind) {
    if (step()!=HUBBUB_OK || tree.count==NODES) { tree.failed=1; return NULL; }
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
    if (!n || step()!=HUBBUB_OK || count>ATTRS-tree.attribute_count) return HUBBUB_NOMEM;
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
        if (tree.attribute_count==ATTRS || step()!=HUBBUB_OK) { tree.failed=1; return HUBBUB_NOMEM; }
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
    /* Hubbub reports meta declarations even when the decoder is already fixed.
     * Confirming UTF-8 is not a request to restart/change that decoder. */
    if (name && strlen(name)==5U && !strncasecmp(name,"UTF-8",5U)) return step();
    /* Other declarations require an unsupported charset/restart adapter. */
    return HUBBUB_ENCODINGCHANGE;
}
static hubbub_error script(void *ctx, void *value) {
    (void)ctx; (void)value; return step(); /* Scripts are inert. */
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

int browser_html5_tree(const uint8_t *input, size_t length, node **result) {
    if (!input || !result || !length || length>REIST_HTML_INPUT_CAPACITY) return -22;
    *result=NULL;
    memset(&tree,0,sizeof(tree));
    if (reist_libc_init(arena,sizeof(arena))) return -12;
    node *root=create(9);
    hubbub_parser *parser=NULL;
    hubbub_tree_handler handler={comment_node,doctype_node,element_node,text_node,
        ref_node,unref_node,append,insert,remove_child,clone_node,reparent,
        get_parent,has_children,form_associate,add_attributes,quirks,encoding,script,NULL};
    hubbub_parser_optparams option={.tree_handler=&handler};
    hubbub_error error=hubbub_parser_create("UTF-8",true,&parser);
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_TREE_HANDLER,&option);
    option.document_node=root;
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_DOCUMENT_NODE,&option);
    option.enable_scripting=false;
    if (error==HUBBUB_OK) error=hubbub_parser_setopt(parser,HUBBUB_PARSER_ENABLE_SCRIPTING,&option);
    for (size_t offset=0; error==HUBBUB_OK && !tree.failed && offset<length; offset+=256) {
        size_t amount=length-offset; if (amount>256) amount=256;
        error=hubbub_parser_parse_chunk(parser,input+offset,amount);
    }
    if (error==HUBBUB_OK && !tree.failed) error=hubbub_parser_completed(parser);
    if (parser) hubbub_parser_destroy(parser);
    if (error==HUBBUB_ENCODINGCHANGE) return -84;
    if (error!=HUBBUB_OK || tree.failed) return -28;
    *result=root;
    return 0;
}
int browser_html5_parse(const uint8_t *input, size_t length, reist_html_document_t *document) {
    if (!document) return -22;
    node *root;
    int result=browser_html5_tree(input,length,&root);
    if (result) return result;
    if (project(root,0)) return -28;
    return reist_html_document_parse(tree.projection,tree.projected,document);
}
