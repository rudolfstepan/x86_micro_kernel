/** @file html_document.c @brief Fixed-capacity semantic HTML subset parser. */
#include "reist/gui/html_document.h"

enum html_tag {
    HTML_TAG_UNKNOWN = 0,
    HTML_TAG_HTML, HTML_TAG_HEAD, HTML_TAG_BODY, HTML_TAG_TITLE,
    HTML_TAG_P, HTML_TAG_DIV, HTML_TAG_H1, HTML_TAG_H2, HTML_TAG_H3,
    HTML_TAG_BR, HTML_TAG_STRONG, HTML_TAG_B, HTML_TAG_EM, HTML_TAG_I,
    HTML_TAG_PRE, HTML_TAG_UL, HTML_TAG_OL, HTML_TAG_LI, HTML_TAG_A,
    HTML_TAG_SCRIPT, HTML_TAG_STYLE
};

typedef struct html_frame {
    uint16_t tag;
    uint16_t list_depth;
    uint32_t style;
    uint32_t link_index;
    uint8_t title;
    uint8_t suppress;
} html_frame_t;

typedef struct html_parser {
    reist_html_document_t *document;
    html_frame_t frames[REIST_HTML_NESTING_CAPACITY];
    uint32_t depth;
    uint32_t style;
    uint32_t link_index;
    uint16_t list_depth;
    uint8_t list_ordered[REIST_HTML_NESTING_CAPACITY];
    uint32_t list_ordinal[REIST_HTML_NESTING_CAPACITY];
    uint8_t title;
    uint8_t suppress;
    uint8_t pending_space;
} html_parser_t;

static void zero_bytes(void *target, size_t length) {
    uint8_t *bytes = (uint8_t *)target;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static uint8_t ascii_lower(uint8_t value) {
    return value >= 'A' && value <= 'Z' ? (uint8_t)(value + ('a' - 'A'))
                                       : value;
}

static int name_equal(const uint8_t *text, size_t length, const char *name) {
    size_t index = 0U;
    while (name[index] != '\0') {
        if (index >= length || ascii_lower(text[index]) != (uint8_t)name[index])
            return 0;
        ++index;
    }
    return index == length;
}

static uint16_t tag_from_name(const uint8_t *name, size_t length) {
    if (name_equal(name, length, "html")) return HTML_TAG_HTML;
    if (name_equal(name, length, "head")) return HTML_TAG_HEAD;
    if (name_equal(name, length, "body")) return HTML_TAG_BODY;
    if (name_equal(name, length, "title")) return HTML_TAG_TITLE;
    if (name_equal(name, length, "p")) return HTML_TAG_P;
    if (name_equal(name, length, "div")) return HTML_TAG_DIV;
    if (name_equal(name, length, "h1")) return HTML_TAG_H1;
    if (name_equal(name, length, "h2")) return HTML_TAG_H2;
    if (name_equal(name, length, "h3")) return HTML_TAG_H3;
    if (name_equal(name, length, "br")) return HTML_TAG_BR;
    if (name_equal(name, length, "strong")) return HTML_TAG_STRONG;
    if (name_equal(name, length, "b")) return HTML_TAG_B;
    if (name_equal(name, length, "em")) return HTML_TAG_EM;
    if (name_equal(name, length, "i")) return HTML_TAG_I;
    if (name_equal(name, length, "pre")) return HTML_TAG_PRE;
    if (name_equal(name, length, "ul")) return HTML_TAG_UL;
    if (name_equal(name, length, "ol")) return HTML_TAG_OL;
    if (name_equal(name, length, "li")) return HTML_TAG_LI;
    if (name_equal(name, length, "a")) return HTML_TAG_A;
    if (name_equal(name, length, "script")) return HTML_TAG_SCRIPT;
    if (name_equal(name, length, "style")) return HTML_TAG_STYLE;
    return HTML_TAG_UNKNOWN;
}

static int utf8_scalar(const uint8_t *input, size_t remaining,
                       size_t *consumed) {
    if (remaining == 0U || consumed == 0) return 0;
    uint8_t first = input[0U];
    if (first < 0x80U) { *consumed = 1U; return first != 0U; }
    uint32_t scalar = 0U;
    size_t count = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
        scalar = first & 0x1FU; count = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        scalar = first & 0x0FU; count = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        scalar = first & 0x07U; count = 4U;
    } else return 0;
    if (remaining < count) return 0;
    for (size_t index = 1U; index < count; ++index) {
        if ((input[index] & 0xC0U) != 0x80U) return 0;
        scalar = (scalar << 6U) | (input[index] & 0x3FU);
    }
    if ((count == 3U && ((first == 0xE0U && input[1U] < 0xA0U) ||
                         (first == 0xEDU && input[1U] >= 0xA0U))) ||
        (count == 4U && ((first == 0xF0U && input[1U] < 0x90U) ||
                         (first == 0xF4U && input[1U] >= 0x90U))) ||
        scalar > 0x10FFFFU) return 0;
    *consumed = count;
    return 1;
}

static int add_element(html_parser_t *parser, uint32_t kind,
                       uint32_t offset, uint32_t length) {
    reist_html_document_t *document = parser->document;
    if (document->element_count >= REIST_HTML_ELEMENT_CAPACITY)
        return REIST_HTML_CAPACITY;
    reist_html_element_t *element =
        &document->elements[document->element_count++];
    *element = (reist_html_element_t){
        kind, offset, length, parser->style, parser->link_index,
        parser->list_depth, 0U};
    return REIST_HTML_OK;
}

static int add_break(html_parser_t *parser, uint32_t kind) {
    parser->pending_space = 0U;
    if (parser->suppress || parser->title) return REIST_HTML_OK;
    reist_html_document_t *document = parser->document;
    if (document->element_count != 0U &&
        document->elements[document->element_count - 1U].kind == kind)
        return REIST_HTML_OK;
    return add_element(parser, kind, 0U, 0U);
}

static int append_bytes(html_parser_t *parser, const uint8_t *bytes,
                        size_t length) {
    if (parser->suppress || length == 0U) return REIST_HTML_OK;
    if (parser->title) {
        size_t used = 0U;
        while (used < REIST_HTML_TITLE_CAPACITY &&
               parser->document->title[used] != '\0') ++used;
        if (used + length >= REIST_HTML_TITLE_CAPACITY)
            return REIST_HTML_CAPACITY;
        for (size_t index = 0U; index < length; ++index)
            parser->document->title[used + index] = (char)bytes[index];
        parser->document->title[used + length] = '\0';
        return REIST_HTML_OK;
    }
    reist_html_document_t *document = parser->document;
    if (document->text_length + length > REIST_HTML_TEXT_CAPACITY)
        return REIST_HTML_CAPACITY;
    uint32_t offset = document->text_length;
    for (size_t index = 0U; index < length; ++index)
        document->text[document->text_length++] = (char)bytes[index];
    if (document->element_count != 0U) {
        reist_html_element_t *previous =
            &document->elements[document->element_count - 1U];
        if (previous->kind == REIST_HTML_ELEMENT_TEXT &&
            previous->style == parser->style &&
            previous->link_index == parser->link_index &&
            previous->list_depth == parser->list_depth &&
            previous->text_offset + previous->text_length == offset) {
            previous->text_length += (uint32_t)length;
            return REIST_HTML_OK;
        }
    }
    return add_element(parser, REIST_HTML_ELEMENT_TEXT, offset,
                       (uint32_t)length);
}

static int append_space(html_parser_t *parser) {
    static const uint8_t space = ' ';
    if (!parser->pending_space) return REIST_HTML_OK;
    parser->pending_space = 0U;
    return append_bytes(parser, &space, 1U);
}

static int append_character(html_parser_t *parser, const uint8_t *bytes,
                            size_t length, uint8_t whitespace) {
    if (!(parser->style & REIST_HTML_STYLE_PREFORMATTED) && whitespace) {
        parser->pending_space = 1U;
        return REIST_HTML_OK;
    }
    int status = append_space(parser);
    return status == 0 ? append_bytes(parser, bytes, length) : status;
}

static size_t encode_utf8(uint32_t scalar, uint8_t output[4U]) {
    if (scalar <= 0x7FU) { output[0U] = (uint8_t)scalar; return 1U; }
    if (scalar <= 0x7FFU) {
        output[0U] = (uint8_t)(0xC0U | (scalar >> 6U));
        output[1U] = (uint8_t)(0x80U | (scalar & 0x3FU)); return 2U;
    }
    if (scalar <= 0xFFFFU && (scalar < 0xD800U || scalar > 0xDFFFU)) {
        output[0U] = (uint8_t)(0xE0U | (scalar >> 12U));
        output[1U] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3FU));
        output[2U] = (uint8_t)(0x80U | (scalar & 0x3FU)); return 3U;
    }
    if (scalar <= 0x10FFFFU) {
        output[0U] = (uint8_t)(0xF0U | (scalar >> 18U));
        output[1U] = (uint8_t)(0x80U | ((scalar >> 12U) & 0x3FU));
        output[2U] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3FU));
        output[3U] = (uint8_t)(0x80U | (scalar & 0x3FU)); return 4U;
    }
    return 0U;
}

static int character_reference(const uint8_t *input, size_t length,
                               size_t *consumed, uint8_t output[4U],
                               size_t *output_length) {
    size_t end = 1U;
    while (end < length && end <= 12U && input[end] != ';') ++end;
    if (end >= length || input[end] != ';') return 0;
    uint32_t scalar = 0U;
    if (end > 2U && input[1U] == '#') {
        size_t index = 2U;
        uint32_t base = 10U;
        if (index < end && (input[index] == 'x' || input[index] == 'X')) {
            base = 16U; ++index;
        }
        if (index == end) return 0;
        for (; index < end; ++index) {
            uint32_t digit;
            if (input[index] >= '0' && input[index] <= '9')
                digit = input[index] - '0';
            else if (base == 16U && ascii_lower(input[index]) >= 'a' &&
                     ascii_lower(input[index]) <= 'f')
                digit = ascii_lower(input[index]) - 'a' + 10U;
            else return 0;
            if (scalar > (0x10FFFFU - digit) / base) return 0;
            scalar = scalar * base + digit;
        }
    } else if (name_equal(input + 1U, end - 1U, "amp")) scalar = '&';
    else if (name_equal(input + 1U, end - 1U, "lt")) scalar = '<';
    else if (name_equal(input + 1U, end - 1U, "gt")) scalar = '>';
    else if (name_equal(input + 1U, end - 1U, "quot")) scalar = '"';
    else if (name_equal(input + 1U, end - 1U, "apos")) scalar = '\'';
    else if (name_equal(input + 1U, end - 1U, "nbsp")) scalar = ' ';
    else return 0;
    *output_length = encode_utf8(scalar, output);
    if (*output_length == 0U || scalar == 0U) return 0;
    *consumed = end + 1U;
    return 1;
}

static int add_link(html_parser_t *parser, const uint8_t *value,
                    size_t length) {
    if (length == 0U || length >= REIST_HTML_HREF_CAPACITY ||
        parser->document->link_count >= REIST_HTML_LINK_CAPACITY)
        return REIST_HTML_CAPACITY;
    uint32_t index = parser->document->link_count++;
    for (size_t byte = 0U; byte < length; ++byte) {
        if (value[byte] < 0x20U || value[byte] == 0x7FU)
            return REIST_HTML_INVALID;
        parser->document->links[index].href[byte] = (char)value[byte];
    }
    parser->document->links[index].href[length] = '\0';
    parser->link_index = index;
    parser->style |= REIST_HTML_STYLE_LINK;
    return REIST_HTML_OK;
}

static int parse_href(html_parser_t *parser, const uint8_t *input,
                      size_t start, size_t end) {
    size_t cursor = start;
    while (cursor < end) {
        while (cursor < end && (input[cursor] == ' ' || input[cursor] == '\t' ||
               input[cursor] == '\r' || input[cursor] == '\n' ||
               input[cursor] == '/')) ++cursor;
        size_t name = cursor;
        while (cursor < end && input[cursor] != '=' && input[cursor] != ' ' &&
               input[cursor] != '\t' && input[cursor] != '\r' &&
               input[cursor] != '\n') ++cursor;
        size_t name_length = cursor - name;
        while (cursor < end && (input[cursor] == ' ' || input[cursor] == '\t' ||
               input[cursor] == '\r' || input[cursor] == '\n')) ++cursor;
        if (cursor >= end || input[cursor] != '=') {
            while (cursor < end && input[cursor] != ' ') ++cursor;
            continue;
        }
        ++cursor;
        while (cursor < end && (input[cursor] == ' ' || input[cursor] == '\t' ||
               input[cursor] == '\r' || input[cursor] == '\n')) ++cursor;
        uint8_t quote = cursor < end && (input[cursor] == '"' ||
            input[cursor] == '\'') ? input[cursor++] : 0U;
        size_t value = cursor;
        while (cursor < end && ((quote != 0U && input[cursor] != quote) ||
               (quote == 0U && input[cursor] != ' ' && input[cursor] != '\t' &&
                input[cursor] != '\r' && input[cursor] != '\n'))) ++cursor;
        size_t value_length = cursor - value;
        if (quote != 0U && cursor < end) ++cursor;
        if (name_equal(input + name, name_length, "href"))
            return add_link(parser, input + value, value_length);
    }
    return REIST_HTML_OK;
}

static int start_tag(html_parser_t *parser, uint16_t tag,
                     const uint8_t *input, size_t attributes_start,
                     size_t attributes_end, uint8_t self_closing) {
    if (tag == HTML_TAG_BR)
        return add_break(parser, REIST_HTML_ELEMENT_LINE_BREAK);
    /* Unknown and therefore unsupported elements are transparent.  In
     * particular, HTML void elements such as meta and img must not consume a
     * nesting frame merely because this subset does not render them. */
    if (tag == HTML_TAG_UNKNOWN) return REIST_HTML_OK;
    if (parser->depth >= REIST_HTML_NESTING_CAPACITY)
        return REIST_HTML_CAPACITY;
    parser->frames[parser->depth++] = (html_frame_t){
        tag, parser->list_depth, parser->style, parser->link_index,
        parser->title, parser->suppress};
    int status = REIST_HTML_OK;
    if (tag == HTML_TAG_SCRIPT || tag == HTML_TAG_STYLE) parser->suppress = 1U;
    else if (tag == HTML_TAG_TITLE) parser->title = 1U;
    else if (tag == HTML_TAG_H1 || tag == HTML_TAG_H2 || tag == HTML_TAG_H3) {
        status = add_break(parser, REIST_HTML_ELEMENT_PARAGRAPH_BREAK);
        parser->style |= tag == HTML_TAG_H1 ? REIST_HTML_STYLE_HEADING_1
            : tag == HTML_TAG_H2 ? REIST_HTML_STYLE_HEADING_2
                                 : REIST_HTML_STYLE_HEADING_3;
    } else if (tag == HTML_TAG_P || tag == HTML_TAG_DIV)
        status = add_break(parser, REIST_HTML_ELEMENT_PARAGRAPH_BREAK);
    else if (tag == HTML_TAG_STRONG || tag == HTML_TAG_B)
        parser->style |= REIST_HTML_STYLE_BOLD;
    else if (tag == HTML_TAG_EM || tag == HTML_TAG_I)
        parser->style |= REIST_HTML_STYLE_ITALIC;
    else if (tag == HTML_TAG_PRE) {
        status = add_break(parser, REIST_HTML_ELEMENT_PARAGRAPH_BREAK);
        parser->style |= REIST_HTML_STYLE_PREFORMATTED;
    } else if (tag == HTML_TAG_UL || tag == HTML_TAG_OL) {
        if (parser->list_depth >= REIST_HTML_NESTING_CAPACITY)
            return REIST_HTML_CAPACITY;
        parser->list_ordered[parser->list_depth] = tag == HTML_TAG_OL;
        parser->list_ordinal[parser->list_depth] = 0U;
        ++parser->list_depth;
    } else if (tag == HTML_TAG_LI) {
        status = add_break(parser, REIST_HTML_ELEMENT_PARAGRAPH_BREAK);
        if (status == 0) {
            status = add_element(parser, REIST_HTML_ELEMENT_LIST_MARKER,
                                 0U, 0U);
            if (status == 0 && parser->list_depth != 0U &&
                parser->list_ordered[parser->list_depth - 1U]) {
                uint32_t *ordinal =
                    &parser->list_ordinal[parser->list_depth - 1U];
                if (*ordinal == UINT32_MAX) return REIST_HTML_CAPACITY;
                parser->document->elements[
                    parser->document->element_count - 1U].text_length =
                        ++*ordinal;
            }
        }
    } else if (tag == HTML_TAG_A)
        status = parse_href(parser, input, attributes_start, attributes_end);
    if (self_closing && parser->depth != 0U) {
        html_frame_t frame = parser->frames[--parser->depth];
        parser->style = frame.style; parser->link_index = frame.link_index;
        parser->list_depth = frame.list_depth; parser->title = frame.title;
        parser->suppress = frame.suppress;
    }
    return status;
}

static int end_tag(html_parser_t *parser, uint16_t tag) {
    uint32_t found = parser->depth;
    while (found != 0U && parser->frames[found - 1U].tag != tag) --found;
    if (found == 0U) return REIST_HTML_OK;
    uint8_t block = tag == HTML_TAG_P || tag == HTML_TAG_DIV ||
        tag == HTML_TAG_H1 || tag == HTML_TAG_H2 || tag == HTML_TAG_H3 ||
        tag == HTML_TAG_PRE || tag == HTML_TAG_LI;
    html_frame_t frame = parser->frames[found - 1U];
    parser->depth = found - 1U;
    parser->style = frame.style; parser->link_index = frame.link_index;
    parser->list_depth = frame.list_depth; parser->title = frame.title;
    parser->suppress = frame.suppress;
    return block ? add_break(parser, REIST_HTML_ELEMENT_PARAGRAPH_BREAK)
                 : REIST_HTML_OK;
}

static int parse_tag(html_parser_t *parser, const uint8_t *input,
                     size_t length, size_t *cursor) {
    size_t start = *cursor;
    if (start + 3U < length && input[start + 1U] == '!' &&
        input[start + 2U] == '-' && input[start + 3U] == '-') {
        size_t end = start + 4U;
        while (end + 2U < length && !(input[end] == '-' &&
               input[end + 1U] == '-' && input[end + 2U] == '>')) ++end;
        if (end + 2U >= length) return REIST_HTML_INVALID;
        *cursor = end + 3U; return REIST_HTML_OK;
    }
    size_t end = start + 1U;
    uint8_t quote = 0U;
    while (end < length) {
        if (quote == 0U && (input[end] == '"' || input[end] == '\''))
            quote = input[end];
        else if (quote != 0U && input[end] == quote) quote = 0U;
        else if (quote == 0U && input[end] == '>') break;
        ++end;
    }
    if (end >= length || quote != 0U) return REIST_HTML_INVALID;
    size_t name = start + 1U;
    uint8_t closing = name < end && input[name] == '/';
    if (closing) ++name;
    while (name < end && (input[name] == ' ' || input[name] == '\t' ||
           input[name] == '\r' || input[name] == '\n')) ++name;
    if (name < end && input[name] == '!') { *cursor = end + 1U; return 0; }
    size_t name_end = name;
    while (name_end < end && ((ascii_lower(input[name_end]) >= 'a' &&
           ascii_lower(input[name_end]) <= 'z') ||
           (input[name_end] >= '0' && input[name_end] <= '9'))) ++name_end;
    if (name_end == name) return REIST_HTML_INVALID;
    uint16_t tag = tag_from_name(input + name, name_end - name);
    size_t tail = end;
    while (tail > name_end && (input[tail - 1U] == ' ' ||
           input[tail - 1U] == '\t' || input[tail - 1U] == '\r' ||
           input[tail - 1U] == '\n')) --tail;
    uint8_t self_closing = tail > name_end && input[tail - 1U] == '/';
    int status = closing ? end_tag(parser, tag)
        : start_tag(parser, tag, input, name_end, end, self_closing);
    *cursor = end + 1U;
    return status;
}

static int raw_end_tag_at(const html_parser_t *parser, const uint8_t *input,
                          size_t length, size_t cursor) {
    if (!parser->suppress || cursor + 3U >= length || input[cursor] != '<' ||
        input[cursor + 1U] != '/') return 0;
    size_t name = cursor + 2U;
    size_t name_end = name;
    while (name_end < length && ascii_lower(input[name_end]) >= 'a' &&
           ascii_lower(input[name_end]) <= 'z') ++name_end;
    uint16_t tag = tag_from_name(input + name, name_end - name);
    if (tag != HTML_TAG_SCRIPT && tag != HTML_TAG_STYLE) return 0;
    if (parser->depth == 0U ||
        parser->frames[parser->depth - 1U].tag != tag) return 0;
    while (name_end < length && (input[name_end] == ' ' ||
           input[name_end] == '\t' || input[name_end] == '\r' ||
           input[name_end] == '\n')) ++name_end;
    return name_end < length && input[name_end] == '>';
}

int reist_html_document_parse(const uint8_t *input, size_t length,
                              reist_html_document_t *document) {
    if (document == 0) return REIST_HTML_INVALID;
    zero_bytes(document, sizeof(*document));
    if (input == 0 || length == 0U || length > REIST_HTML_INPUT_CAPACITY)
        return REIST_HTML_INVALID;
    html_parser_t parser;
    zero_bytes(&parser, sizeof(parser));
    parser.document = document;
    parser.link_index = UINT32_MAX;
    size_t cursor = 0U;
    int status = REIST_HTML_OK;
    while (cursor < length && status == REIST_HTML_OK) {
        if (parser.suppress && !raw_end_tag_at(
                &parser, input, length, cursor)) {
            size_t ignored = 0U;
            if (!utf8_scalar(input + cursor, length - cursor, &ignored)) {
                status = REIST_HTML_ENCODING; break;
            }
            cursor += ignored;
            continue;
        }
        if (input[cursor] == '<') {
            status = parse_tag(&parser, input, length, &cursor);
            continue;
        }
        uint8_t decoded[4U];
        size_t consumed = 0U, decoded_length = 0U;
        if (input[cursor] == '&' && character_reference(
                input + cursor, length - cursor, &consumed,
                decoded, &decoded_length)) {
            status = append_character(&parser, decoded, decoded_length,
                                      decoded_length == 1U && decoded[0U] == ' ');
            cursor += consumed; continue;
        }
        if (!utf8_scalar(input + cursor, length - cursor, &consumed)) {
            status = REIST_HTML_ENCODING; break;
        }
        uint8_t whitespace = consumed == 1U &&
            (input[cursor] == ' ' || input[cursor] == '\t' ||
             input[cursor] == '\r' || input[cursor] == '\n' ||
             input[cursor] == '\f');
        status = append_character(&parser, input + cursor, consumed, whitespace);
        cursor += consumed;
    }
    if (status == REIST_HTML_OK && parser.suppress)
        status = REIST_HTML_INVALID;
    if (status != REIST_HTML_OK) zero_bytes(document, sizeof(*document));
    return status;
}

static size_t text_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int prefix_equal(const char *text, const char *prefix) {
    size_t index = 0U;
    while (prefix[index] != '\0') {
        if (ascii_lower((uint8_t)text[index]) != (uint8_t)prefix[index])
            return 0;
        ++index;
    }
    return 1;
}

static int url_copy(char *output, size_t capacity, size_t *used,
                    const char *text, size_t length) {
    if (*used + length >= capacity) return REIST_HTML_CAPACITY;
    for (size_t index = 0U; index < length; ++index) {
        uint8_t byte = (uint8_t)text[index];
        if (byte < 0x20U || byte == 0x7FU) return REIST_HTML_INVALID;
        output[(*used)++] = text[index];
    }
    output[*used] = '\0';
    return REIST_HTML_OK;
}

int reist_html_navigation_normalize(const char *input, char *output,
                                    size_t capacity) {
    if (input == 0 || output == 0 || capacity < 2U)
        return REIST_HTML_INVALID;
    output[0U] = '\0';
    size_t length = text_length(input, REIST_HTML_HREF_CAPACITY);
    if (length == 0U || length >= REIST_HTML_HREF_CAPACITY)
        return REIST_HTML_INVALID;
    for (size_t index = 0U; index < length; ++index) {
        uint8_t byte = (uint8_t)input[index];
        if (byte <= 0x20U || byte == 0x7FU) return REIST_HTML_INVALID;
    }
    size_t used = 0U;
    size_t scheme = prefix_equal(input, "https://") ? 8U
                  : prefix_equal(input, "http://") ? 7U : 0U;
    if (scheme != 0U) {
        size_t authority_end = scheme;
        while (authority_end < length && input[authority_end] != '/' &&
               input[authority_end] != '#') ++authority_end;
        if (authority_end == scheme) return REIST_HTML_INVALID;
        const char *canonical = scheme == 8U ? "https://" : "http://";
        int status = url_copy(output, capacity, &used, canonical, scheme);
        return status == REIST_HTML_OK
            ? url_copy(output, capacity, &used, input + scheme,
                       length - scheme) : status;
    }
    if (input[0U] == '/' || input[0U] == '#')
        return url_copy(output, capacity, &used, input, length);
    for (size_t index = 0U; index < length; ++index)
        if (input[index] == ':') return REIST_HTML_INVALID;
    int status = url_copy(output, capacity, &used, "https://", 8U);
    return status == REIST_HTML_OK
        ? url_copy(output, capacity, &used, input, length) : status;
}

int reist_html_url_resolve(const char *base, const char *reference,
                           char *output, size_t capacity) {
    if (base == 0 || reference == 0 || output == 0 || capacity < 2U)
        return REIST_HTML_INVALID;
    output[0U] = '\0';
    size_t base_length = text_length(base, REIST_HTML_HREF_CAPACITY);
    size_t reference_length = text_length(
        reference, REIST_HTML_HREF_CAPACITY);
    if (base_length == 0U || base_length >= REIST_HTML_HREF_CAPACITY ||
        reference_length == 0U ||
        reference_length >= REIST_HTML_HREF_CAPACITY)
        return REIST_HTML_INVALID;
    size_t used = 0U;
    if (prefix_equal(reference, "http://") ||
        prefix_equal(reference, "https://"))
        return url_copy(output, capacity, &used, reference, reference_length);
    for (size_t index = 0U; index < reference_length; ++index)
        if (reference[index] == ':') return REIST_HTML_INVALID;

    size_t fragmentless = 0U;
    while (fragmentless < base_length && base[fragmentless] != '#')
        ++fragmentless;
    if (reference[0U] == '#') {
        int status = url_copy(output, capacity, &used, base, fragmentless);
        return status == 0 ? url_copy(output, capacity, &used, reference,
                                      reference_length) : status;
    }

    uint8_t network = prefix_equal(base, "http://") ||
                      prefix_equal(base, "https://");
    if (reference[0U] == '/') {
        if (network) {
            size_t authority = prefix_equal(base, "https://") ? 8U : 7U;
            while (authority < fragmentless && base[authority] != '/')
                ++authority;
            int status = url_copy(output, capacity, &used, base, authority);
            return status == 0 ? url_copy(output, capacity, &used, reference,
                                          reference_length) : status;
        }
        return url_copy(output, capacity, &used, reference, reference_length);
    }

    size_t directory = fragmentless;
    while (directory != 0U && base[directory - 1U] != '/') --directory;
    if (directory == 0U || (!network && base[0U] != '/'))
        return REIST_HTML_INVALID;
    int status = url_copy(output, capacity, &used, base, directory);
    return status == 0 ? url_copy(output, capacity, &used, reference,
                                  reference_length) : status;
}
