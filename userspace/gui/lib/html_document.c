/** @file html_document.c @brief Fixed-capacity semantic HTML subset parser. */
#include "reist/gui/html_document.h"

enum html_tag {
    HTML_TAG_UNKNOWN = 0,
    HTML_TAG_HTML, HTML_TAG_HEAD, HTML_TAG_BODY, HTML_TAG_TITLE,
    HTML_TAG_P, HTML_TAG_DIV, HTML_TAG_H1, HTML_TAG_H2, HTML_TAG_H3,
    HTML_TAG_BR, HTML_TAG_STRONG, HTML_TAG_B, HTML_TAG_EM, HTML_TAG_I,
    HTML_TAG_PRE, HTML_TAG_UL, HTML_TAG_OL, HTML_TAG_LI, HTML_TAG_A,
    HTML_TAG_SCRIPT, HTML_TAG_STYLE, HTML_TAG_IMG
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
    uint8_t title_truncated;
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
    if (name_equal(name, length, "img")) return HTML_TAG_IMG;
    if (name_equal(name, length, "section") || name_equal(name, length, "article") ||
        name_equal(name, length, "header") || name_equal(name, length, "footer") ||
        name_equal(name, length, "main") || name_equal(name, length, "nav") ||
        name_equal(name, length, "table") || name_equal(name, length, "tr"))
        return HTML_TAG_DIV;
    if (name_equal(name, length, "h4") || name_equal(name, length, "h5") ||
        name_equal(name, length, "h6")) return HTML_TAG_H3;
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
        if (parser->title_truncated) return REIST_HTML_OK;
        size_t used = 0U;
        while (used < REIST_HTML_TITLE_CAPACITY &&
               parser->document->title[used] != '\0') ++used;
        if (used + length >= REIST_HTML_TITLE_CAPACITY) {
            /* Optional display metadata: retain a valid UTF-8 prefix, not a
             * failed document. Input validation continues after truncation. */
            parser->title_truncated = 1U;
            return REIST_HTML_OK;
        }
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

static int attribute(const uint8_t *input, size_t start, size_t end,
                      const char *wanted, char *output, size_t capacity) {
    output[0] = '\0';
    size_t cursor = start;
    while (cursor < end) {
        while (cursor < end && (input[cursor] <= ' ' || input[cursor] == '/')) ++cursor;
        size_t name = cursor;
        while (cursor < end && input[cursor] != '=' && input[cursor] > ' ' &&
               input[cursor] != '/') ++cursor;
        size_t name_length = cursor - name;
        while (cursor < end && input[cursor] <= ' ') ++cursor;
        if (cursor >= end) break;
        if (input[cursor] != '=') { if (cursor == name) ++cursor; continue; }
        ++cursor;
        while (cursor < end && input[cursor] <= ' ') ++cursor;
        uint8_t quote = cursor < end && (input[cursor] == '"' || input[cursor] == '\'')
            ? input[cursor++] : 0U;
        size_t value = cursor;
        while (cursor < end && (quote ? input[cursor] != quote : input[cursor] > ' '))
            ++cursor;
        size_t value_end = cursor;
        if (quote && cursor < end) ++cursor;
        if (!name_equal(input + name, name_length, wanted)) continue;
        size_t used = 0U;
        while (value < value_end) {
            uint8_t decoded[4U];
            size_t consumed = 0U, amount = 0U;
            const uint8_t *bytes = input + value;
            if (input[value] == '&' && character_reference(input + value,
                    value_end - value, &consumed, decoded, &amount)) bytes = decoded;
            else {
                if (!utf8_scalar(input + value, value_end - value, &consumed))
                    return REIST_HTML_ENCODING;
                amount = consumed;
            }
            if (used + amount >= capacity) return REIST_HTML_CAPACITY;
            for (size_t i = 0; i < amount; ++i) {
                if (bytes[i] < 0x20U || bytes[i] == 0x7FU) return REIST_HTML_INVALID;
                output[used++] = (char)bytes[i];
            }
            value += consumed;
        }
        output[used] = '\0';
        return 1;
    }
    return 0;
}

static int parse_href(html_parser_t *parser, const uint8_t *input,
                      size_t start, size_t end) {
    char href[REIST_HTML_HREF_CAPACITY];
    int found = attribute(input, start, end, "href", href, sizeof(href));
    if (found < 0) return found;
    if (!found) return 0;
    /* Empty href is a valid current-document reference. */
    if (href[0] == '\0') { href[0] = '#'; href[1] = '\0'; }
    size_t length = 0U;
    while (href[length]) ++length;
    return add_link(parser, (const uint8_t *)href, length);
}

static uint16_t image_dimension(const uint8_t *input, size_t start, size_t end,
                                const char *name) {
    char value[16];
    if (attribute(input, start, end, name, value, sizeof(value)) <= 0) return 0U;
    uint32_t number = 0U;
    for (size_t i = 0; value[i]; ++i) {
        if (value[i] < '0' || value[i] > '9' || number > 1024U) return 0U;
        number = number * 10U + (uint32_t)(value[i] - '0');
    }
    return number <= 1024U ? (uint16_t)number : 0U;
}

static int parse_anchor(html_parser_t *parser, uint16_t tag, const uint8_t *input,
                         size_t start, size_t end) {
    if (parser->suppress || parser->title) return 0;
    char name[128U];
    int found = attribute(input, start, end, "id", name, sizeof(name));
    if (found == 0 && tag == HTML_TAG_A)
        found = attribute(input, start, end, "name", name, sizeof(name));
    if (found < 0) return found;
    if (!found || name[0] == '\0') return 0;
    reist_html_document_t *document = parser->document;
    if (document->anchor_count >= REIST_HTML_ANCHOR_CAPACITY) return REIST_HTML_CAPACITY;
    uint32_t index = document->anchor_count++;
    for (size_t i = 0; i < sizeof(name); ++i) {
        document->anchors[index].name[i] = name[i];
        if (!name[i]) break;
    }
    return add_element(parser, REIST_HTML_ELEMENT_ANCHOR, index, 0U);
}

static int parse_image(html_parser_t *parser, const uint8_t *input,
                        size_t start, size_t end) {
    if (parser->suppress || parser->title) return 0;
    reist_html_document_t *document = parser->document;
    if (document->image_count >= REIST_HTML_IMAGE_CAPACITY) return 0;
    uint32_t index = document->image_count;
    reist_html_image_t *image = &document->images[index];
    int found = attribute(input, start, end, "src", image->source, sizeof(image->source));
    if (found < 0) return found;
    int alt = attribute(input, start, end, "alt", image->alt, sizeof(image->alt));
    if (alt < 0) return alt;
    image->width = image_dimension(input, start, end, "width");
    image->height = image_dimension(input, start, end, "height");
    ++document->image_count;
    return add_element(parser, REIST_HTML_ELEMENT_IMAGE, index, 0U);
}

static int start_tag(html_parser_t *parser, uint16_t tag,
                     const uint8_t *input, size_t attributes_start,
                     size_t attributes_end, uint8_t self_closing) {
    int anchor_status = parse_anchor(parser, tag, input, attributes_start, attributes_end);
    if (anchor_status != 0) return anchor_status;
    if (tag == HTML_TAG_IMG)
        return parse_image(parser, input, attributes_start, attributes_end);
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

/* RFC 3986 sections 3 and 5, restricted to HTTP(S) and local absolute paths.
 * Work and scratch storage are bounded by REIST_HTML_HREF_CAPACITY. */
typedef struct uri_parts {
    size_t scheme_end, authority_start, authority_end;
    size_t path_start, path_end, query_start, fragment_start, length;
    uint8_t network, has_authority, has_query, has_fragment;
} uri_parts_t;

static int split_uri(const char *text, uri_parts_t *parts,size_t limit) {
    zero_bytes(parts, sizeof(*parts));
    size_t length = text_length(text, limit);
    if (length >= limit) return REIST_HTML_CAPACITY;
    parts->length = length;
    for (size_t i = 0; i < length; ++i)
        if ((uint8_t)text[i] <= 0x20U || (uint8_t)text[i] == 0x7FU ||
            text[i] == '\\') return REIST_HTML_INVALID;
    size_t cursor = 0U;
    for (size_t i = 0; i < length && text[i] != '/' &&
         text[i] != '?' && text[i] != '#'; ++i) {
        if (text[i] != ':') continue;
        if (!prefix_equal(text, "http://") && !prefix_equal(text, "https://"))
            return REIST_HTML_INVALID;
        parts->network = 1U;
        parts->scheme_end = i + 1U;
        cursor = i + 1U;
        break;
    }
    if (cursor + 1U < length && text[cursor] == '/' && text[cursor + 1U] == '/') {
        parts->has_authority = 1U;
        cursor += 2U;
        parts->authority_start = cursor;
        while (cursor < length && text[cursor] != '/' &&
               text[cursor] != '?' && text[cursor] != '#') {
            if (text[cursor] == '@') return REIST_HTML_INVALID;
            ++cursor;
        }
        parts->authority_end = cursor;
        if (parts->authority_start == cursor) return REIST_HTML_INVALID;
    }
    if (parts->network && !parts->has_authority) return REIST_HTML_INVALID;
    parts->path_start = cursor;
    while (cursor < length && text[cursor] != '?' && text[cursor] != '#') ++cursor;
    parts->path_end = cursor;
    if (cursor < length && text[cursor] == '?') {
        parts->has_query = 1U;
        parts->query_start = ++cursor;
        while (cursor < length && text[cursor] != '#') ++cursor;
    }
    parts->fragment_start = cursor;
    if (cursor < length && text[cursor] == '#') {
        parts->has_fragment = 1U;
        parts->fragment_start = cursor + 1U;
    }
    return 0;
}

static size_t query_end(const uri_parts_t *p) {
    return p->has_fragment ? p->fragment_start - 1U : p->length;
}

static int normalized_path(const char *path, size_t length,
                            char *output, size_t capacity, size_t *used,uint16_t *marks) {
    /* RFC dot segments affect only the path, never query/fragment bytes.
     * Preserve empty segments (double slashes) and terminal directory slash. */
    size_t count = 0U, cursor = 0U;
    if (length == 0U) return 0;
    if (path[0] != '/') return REIST_HTML_INVALID;
    if (url_copy(output, capacity, used, "/", 1U) != 0) return REIST_HTML_CAPACITY;
    cursor = 1U;
    while (cursor <= length) {
        size_t end = cursor;
        while (end < length && path[end] != '/') ++end;
        size_t n = end - cursor;
        if (n == 1U && path[cursor] == '.') {
            /* Dropped; the separator already present preserves a trailing /. */
        } else if (n == 2U && path[cursor] == '.' && path[cursor + 1U] == '.') {
            if (count != 0U) { *used = marks[--count]; output[*used] = '\0'; }
        } else {
            marks[count++] = (uint16_t)*used;
            int status = url_copy(output, capacity, used, path + cursor, n);
            if (status != 0) return status;
            if (end < length && url_copy(output, capacity, used, "/", 1U) != 0)
                return REIST_HTML_CAPACITY;
        }
        if (end == length) break;
        cursor = end + 1U;
    }
    return 0;
}

static int resolve_work(const char *base,const char *reference,char *output,size_t capacity,
    size_t limit,char *candidate,char *path,uint16_t *marks) {
    if (base == 0 || reference == 0 || output == 0 || capacity < 2U)
        return REIST_HTML_INVALID;
    uri_parts_t b, r;
    int status = split_uri(base, &b,limit);
    if (status == 0) status = split_uri(reference, &r,limit);
    if (status != 0 || b.length == 0U ||
        (!b.network && (b.has_authority || base[0] != '/'))) {
        output[0] = '\0'; return status != 0 ? status : REIST_HTML_INVALID;
    }
    candidate[0]=path[0]=0;
    size_t used = 0U, path_used = 0U;
    uint8_t network = r.network || b.network;
    if (r.has_authority && !network) { output[0] = '\0'; return REIST_HTML_INVALID; }
    if (network) {
        const char *scheme = r.network ? reference : base;
        size_t scheme_length = r.network ? r.scheme_end : b.scheme_end;
        const char *canonical = scheme_length == 6U ? "https:" : "http:";
        (void)scheme;
        status = url_copy(candidate, limit, &used, canonical, scheme_length);
        if (status == 0) status = url_copy(candidate, limit, &used, "//", 2U);
        const uri_parts_t *authority = r.has_authority ? &r : &b;
        const char *source = r.has_authority ? reference : base;
        if (status == 0)
            status = url_copy(candidate, limit, &used,
                source + authority->authority_start,
                authority->authority_end - authority->authority_start);
    }
    const char *query_source = reference;
    const uri_parts_t *query = &r;
    if (r.path_start == r.path_end && !r.has_authority && !r.network) {
        status = status == 0 ? url_copy(path, limit, &path_used,
            base + b.path_start, b.path_end - b.path_start) : status;
        if (!r.has_query) { query_source = base; query = &b; }
    } else if (r.has_authority || reference[r.path_start] == '/') {
        if (status == 0) status = url_copy(path, limit, &path_used,
            reference + r.path_start, r.path_end - r.path_start);
    } else {
        size_t directory_end = b.path_end;
        while (directory_end > b.path_start && base[directory_end - 1U] != '/')
            --directory_end;
        if (directory_end == b.path_start && network)
            status = status == 0 ? url_copy(path, limit, &path_used, "/", 1U) : status;
        else if (status == 0) status = url_copy(path, limit, &path_used,
            base + b.path_start, directory_end - b.path_start);
        if (status == 0) status = url_copy(path, limit, &path_used,
            reference + r.path_start, r.path_end - r.path_start);
    }
    if (status == 0) status = normalized_path(path, path_used, candidate, limit, &used,marks);
    if (status == 0 && query->has_query) {
        status = url_copy(candidate, limit, &used, "?", 1U);
        if (status == 0) status = url_copy(candidate, limit, &used,
            query_source + query->query_start, query_end(query) - query->query_start);
    }
    if (status == 0 && r.has_fragment) {
        status = url_copy(candidate, limit, &used, "#", 1U);
        if (status == 0) status = url_copy(candidate, limit, &used,
            reference + r.fragment_start, r.length - r.fragment_start);
    }
    output[0] = '\0';
    size_t published = 0U;
    if (status == 0) status = url_copy(output, capacity, &published, candidate, used);
    if (status != 0) output[0] = '\0';
    return status;
}

int reist_html_url_resolve(const char *base,const char *reference,char *out,size_t capacity) {
    char candidate[REIST_HTML_HREF_CAPACITY],path[REIST_HTML_HREF_CAPACITY];
    uint16_t marks[REIST_HTML_HREF_CAPACITY];
    return resolve_work(base,reference,out,capacity,REIST_HTML_HREF_CAPACITY,candidate,path,marks);
}
int reist_html_url_resolve_wide(const char *base,const char *reference,char *out,
    size_t capacity,reist_html_url_workspace_t *w) {
    if(!w) return REIST_HTML_INVALID;
    return resolve_work(base,reference,out,capacity,REIST_HTML_URL_CAPACITY,w->candidate,w->path,w->marks);
}

int reist_html_navigation_normalize(const char *input, char *output,
                                    size_t capacity) {
    if (input == 0 || output == 0 || capacity < 2U) return REIST_HTML_INVALID;
    size_t length = text_length(input, REIST_HTML_HREF_CAPACITY);
    if (length == 0U || length >= REIST_HTML_HREF_CAPACITY) {
        output[0] = '\0'; return REIST_HTML_INVALID;
    }
    char target[REIST_HTML_HREF_CAPACITY] = {0};
    size_t used = 0U;
    int status = 0;
    if (input[0] != '/' && input[0] != '#' &&
        !prefix_equal(input, "http://") && !prefix_equal(input, "https://")) {
        /* Schemes are rejected before adding a default; host:port is allowed. */
        size_t colon = 0U;
        while (colon < length && input[colon] != ':' && input[colon] != '/') ++colon;
        if (colon < length && input[colon] == ':') {
            size_t digit = colon + 1U;
            while (digit < length && input[digit] >= '0' && input[digit] <= '9') ++digit;
            if (digit == colon + 1U || (digit < length && input[digit] != '/'))
                status = REIST_HTML_INVALID;
        }
        if (status == 0) status = url_copy(target, sizeof(target), &used, "https://", 8U);
    }
    if (status == 0) status = url_copy(target, sizeof(target), &used, input, length);
    if (status != 0) { output[0] = '\0'; return status; }
    if (input[0] == '#') {
        uri_parts_t parts;
        status = split_uri(input, &parts,REIST_HTML_HREF_CAPACITY);
        used = 0U; output[0] = '\0';
        return status == 0 ? url_copy(output, capacity, &used, input, length) : status;
    }
    return reist_html_url_resolve(input[0] == '/' && input[1] != '/' ? "/" :
                                  "https://invalid.test/", target, output, capacity);
}
