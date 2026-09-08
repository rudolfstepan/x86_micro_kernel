"""REIST LibCSS 0.9.2 private token/bytecode adapter v1.

Applied after the archive SHA256 check, before compilation. Exact contexts only;
no stylesheet rewriting, second lexer, selector matcher or network dependency.
The original parser/cascade handles every standard property and shorthand.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(data, before, after):
    before, after = before.encode(), after.encode()
    if data.count(before) != 1:
        raise ValueError("libcss layout v1: exact source context mismatch: "+before[:80].decode())
    return data.replace(before, after, 1)


def patch(relative, data):
    if relative == "src/select/bloom.h":
        data = replace_once(data, "bloom[index] |= (1 << bit);", "bloom[index] |= (1U << bit); /* REIST: defined bit 31 */")
        data = replace_once(data, "return (bloom[index] & (1 << bit));", "return (bloom[index] & (1U << bit));")
    elif relative == "src/lex/lex.c":
        data = replace_once(data, """\t\t\t/* CHAR is the only match here */
\t\t\t/* Remove the '-' we read above */
\t\t\tlexer->bytesReadForToken -= 1;
\t\t\tt->data.len -= 1;
\t\t\treturn emitToken(lexer, CSS_TOKEN_CHAR, token);""", """\t\t\t/* CSS Syntax 3: a double dash starts an identifier. */
\t\t\treturn emitToken(lexer, CSS_TOKEN_IDENT, token);""")
        data = replace_once(data, """\t\t\t/* Remove the '-' we read above */
\t\t\tlexer->bytesReadForToken -= 1;
\t\t\tt->data.len -= 1;
\t\t\tt->type = CSS_TOKEN_CHAR;""", """\t\t\t/* Preserve CDC above; otherwise continue the upstream name lexer. */
\t\t\tlexer->state = sIDENT;
\t\t\tlexer->substate = 0;
\t\t\treturn IdentOrFunction(lexer, token);""")
    elif relative == "src/parse/parse.c":
        data = replace_once(data, """\t\t/* Grammar ambiguity -- assume ';' or '}' mark end */
\t\tif (token->type == CSS_TOKEN_CHAR &&""", """\t\t/* CSS Variables 1: empty custom properties are valid token lists.
\t\t * Leave ordinary value1 grammar and token pushback unchanged. */
\t\tconst css_token *property = parserutils_vector_peek(parser->tokens, 0);
\t\tif (property && property->type == CSS_TOKEN_IDENT &&
\t\t\tlwc_string_length(property->idata) > 2 &&
\t\t\tlwc_string_data(property->idata)[0] == '-' &&
\t\t\tlwc_string_data(property->idata)[1] == '-' &&
\t\t\t(token->type == CSS_TOKEN_EOF || (token->type == CSS_TOKEN_CHAR &&
\t\t\t lwc_string_length(token->idata) == 1 &&
\t\t\t (lwc_string_data(token->idata)[0] == ';' || lwc_string_data(token->idata)[0] == '}'))))
\t\t\treturn done(parser);
\t\t/* Grammar ambiguity -- assume ';' or '}' mark end */
\t\tif (token->type == CSS_TOKEN_CHAR &&""")
    elif relative == "src/parse/language.c":
        data = b'#include "reist_layout_private.h"\n'+data
        data = replace_once(data, "\t/* Find property index */", """\t/* REIST v1: retain actual tokens at their bytecode cascade position. */
\tint captured = css_reist_capture(c, property, vector, ctx, rule);
\tif (captured >= 0) return (css_error)captured;
\t/* Find property index */""")
    elif relative == "src/select/select.c":
        data = b'#include "reist_layout_private.h"\n'+data
        data = replace_once(data, """\tif (state->node_data != NULL) {
\t\tcss__destroy_node_data(state->node_data);
\t}""", """\tif (state->node_data != NULL) {
\t\t/* Until css__set_node_data succeeds, bloom is borrowed from the
\t\t * parent (or static root storage), not owned by this selection. */
\t\tstate->node_data->bloom = NULL;
\t\tcss__destroy_node_data(state->node_data);
\t}""")
        data = replace_once(data,
            "error = css_select_style__get_sharable_node_data(node, &state, &share);",
            "share = NULL;\n\terror = browser_css_values_active() ? CSS_OK :\n\t\tcss_select_style__get_sharable_node_data(node, &state, &share);")
        data = replace_once(data, "\t\top = getOpcode(opv);\n\n\t\terror = prop_dispatch[op].cascade(opv, &s, state);", """\t\top = getOpcode(opv);
\t\tif (op == CSS_REIST_TOKEN_OP) {
\t\t\tif (!s.used) return CSS_INVALID;
\t\t\tuint32_t id = *s.bytecode;
\t\t\tadvance_bytecode(&s, sizeof(css_code_t));
\t\t\tvoid *replacement = NULL;
\t\t\tint rc = browser_css_values_apply(id, state->current_origin,
\t\t\t\tstate->current_specificity, isImportant(opv),
\t\t\t\tstate->current_pseudo, s.sheet, &replacement);
\t\t\tif (rc) return CSS_NOMEM;
\t\t\tif (replacement) {
\t\t\t\terror = cascade_style(replacement, state);
\t\t\t\tcss__stylesheet_style_destroy(replacement);
\t\t\t\tif (error != CSS_OK) return error;
\t\t\t}
\t\t\tcontinue;
\t\t}
\t\terror = prop_dispatch[op].cascade(opv, &s, state);""")
    elif relative == "src/parse/important.c":
        data = replace_once(data, "\t\t\tswitch (op) {", "\t\t\tswitch (op) {\n\t\t\tcase 1023: offset++; break; /* REIST v1 token declaration id */")
    return data


def install(root):
    (root/"include/libcss/reist_layout.h").write_bytes(
        (ROOT/"userspace/gui/apps/browser/css_values.hpp").read_bytes())
    (root/"src/reist_layout_private.h").write_text(PRIVATE, encoding="utf-8")
    (root/"src/reist_layout.c").write_text(SOURCE, encoding="utf-8")


PRIVATE = r'''/* Private exact-pinned LibCSS adapter v1. */
#ifndef CSS_REIST_LAYOUT_PRIVATE_H
#define CSS_REIST_LAYOUT_PRIVATE_H
#include <libcss/reist_layout.h>
#include "parse/language.h"
#include "stylesheet.h"
#define CSS_REIST_TOKEN_OP 1023
int css_reist_capture(css_language *,const css_token *,const parserutils_vector *,int32_t *,css_rule *);
#endif
'''

SOURCE = r'''/* REIST layout adapter v1; upstream MIT license remains in COPYING. */
#include <string.h>
#include "reist_layout_private.h"
#include "parse/properties/properties.h"
#include "parse/important.h"
#include "parse/properties/utils.h"
_Static_assert(BC_IDENT == (int)CSS_TOKEN_IDENT && BC_EOF == (int)CSS_TOKEN_EOF &&
    BC_SPACE == (int)CSS_TOKEN_S && BC_DIMENSION == (int)CSS_TOKEN_DIMENSION, "pinned token ABI");
int css_reist_capture(css_language *c,const css_token *property,const parserutils_vector *v,int32_t *ctx,css_rule *rule) {
    if(!browser_css_values_active()) return -1;
    browser_css_token tokens[512]; uint32_t n=0;
    int32_t at=*ctx; const css_token *t;
    while((t=parserutils_vector_iterate(v,&at))) {
        if(n==512) return CSS_NOMEM;
        tokens[n++]=(browser_css_token){t->type,t->idata};
    }
    uint32_t id=0,important=0;
    int rc=browser_css_values_capture(lwc_string_data(property->idata),lwc_string_length(property->idata),tokens,n,&id,&important);
    if(!rc) return -1;
    if(rc<0) return rc==-84 ? CSS_INVALID : CSS_NOMEM;
    css_style *style=NULL;
    css_error error=css__stylesheet_style_create(c->sheet,&style);
    if(error!=CSS_OK) return error;
    error=css__stylesheet_style_appendOPV(style,CSS_REIST_TOKEN_OP,important ? FLAG_IMPORTANT : 0,0);
    if(error==CSS_OK) error=css__stylesheet_style_append(style,id);
    if(error==CSS_OK) error=css__stylesheet_rule_append_style(c->sheet,rule,style);
    if(error!=CSS_OK) css__stylesheet_style_destroy(style);
    *ctx=at; return error;
}
int css_reist_parse_value(void *opaque,const char *name,size_t length,const browser_css_token *tokens,uint32_t n,uint32_t important,void **out) {
    css_stylesheet *sheet=opaque;
    css_language language={0}; language.sheet=sheet; language.strings=sheet->propstrings;
    lwc_string *property=NULL;
    if(lwc_intern_string(name,length,&property)!=lwc_error_ok) return -28;
    int index;
    for(index=FIRST_PROP;index<=LAST_PROP;++index) {
        bool match=false;
        if(lwc_string_caseless_isequal(property,language.strings[index],&match)==lwc_error_ok && match) break;
    }
    lwc_string_unref(property);
    if(index>LAST_PROP) return -84;
    parserutils_vector *vector=NULL;
    if(parserutils_vector_create(sizeof(css_token),16,&vector)!=PARSERUTILS_OK) return -28;
    int result=0;
    for(uint32_t i=0;i<n;++i) {
        css_token t={0}; t.type=tokens[i].type; t.idata=tokens[i].string;
        if(t.idata) { t.data.data=(uint8_t *)lwc_string_data(t.idata); t.data.len=lwc_string_length(t.idata); }
        if(parserutils_vector_append(vector,&t)!=PARSERUTILS_OK) { result=-28; break; }
    }
    css_style *style=NULL;
    if(!result && css__stylesheet_style_create(sheet,&style)!=CSS_OK) result=-28;
    if(!result) {
        int32_t at=0;
        css_error rc=property_handlers[index-FIRST_PROP](&language,vector,&at,style);
        consumeWhitespace(vector,&at);
        if(rc!=CSS_OK || parserutils_vector_peek(vector,at)) result=rc==CSS_NOMEM ? -28 : -84;
        if(!result && important) css__make_style_important(style);
    }
    parserutils_vector_destroy(vector);
    if(result) { if(style) css__stylesheet_style_destroy(style); }
    else *out=style;
    return result;
}
int css_reist_parse_color(void *opaque,const browser_css_token *tokens,uint32_t n,uint32_t *used,uint32_t *color) {
    css_stylesheet *sheet=opaque; css_language language={0}; language.sheet=sheet; language.strings=sheet->propstrings;
    parserutils_vector *vector=NULL;
    if(n>512 || parserutils_vector_create(sizeof(css_token),16,&vector)!=PARSERUTILS_OK) return -28;
    for(uint32_t i=0;i<n;++i) {
        css_token t={0}; t.type=tokens[i].type; t.idata=tokens[i].string;
        if(t.idata) { t.data.data=(uint8_t *)lwc_string_data(t.idata); t.data.len=lwc_string_length(t.idata); }
        if(parserutils_vector_append(vector,&t)!=PARSERUTILS_OK) { parserutils_vector_destroy(vector); return -28; }
    }
    int32_t at=0; uint16_t value=0;
    css_error rc=css__parse_colour_specifier(&language,vector,&at,&value,color);
    parserutils_vector_destroy(vector);
    if(rc!=CSS_OK) return rc==CSS_NOMEM ? -28 : -84;
    *used=(uint32_t)at; return 0;
}
'''
