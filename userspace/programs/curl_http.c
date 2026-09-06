/** @file curl_http.c @brief Bounded HTTP URL and response-head parser. */
#include "curl_http.h"

_Static_assert(sizeof(reist_curl_ipc_packet_t)==2048U,"private CURL packet ABI");
int reist_curl_ipc_accept(const reist_curl_ipc_packet_t *p,uint32_t length,
    uint32_t endpoint,uint8_t *output,uint32_t capacity,uint32_t *used,uint32_t *total) {
    if(!p || !output || !used || !total || !endpoint || length<=16U || length>sizeof(*p) ||
       p->magic!=REIST_CURL_IPC_MAGIC || p->endpoint!=endpoint || p->offset!=*used ||
       !p->total || p->total>capacity || p->total>REIST_CURL_IPC_BODY_LIMIT+REIST_CURL_HEADER_CAPACITY ||
       (*total && *total!=p->total) || *used>p->total || length-16U>p->total-*used) return -84;
    for(uint32_t i=0;i<length-16U;++i) output[*used+i]=p->bytes[i];
    *used+=length-16U; *total=p->total;
    return 0;
}

static uint8_t lower_ascii(uint8_t value) {
    return value >= 'A' && value <= 'Z' ? (uint8_t)(value + ('a' - 'A'))
                                        : value;
}

static int equal_ascii_case(const uint8_t *left, uint32_t length,
                            const char *right) {
    uint32_t index = 0U;
    while (right[index] != '\0') {
        if (index >= length || lower_ascii(left[index]) !=
                lower_ascii((uint8_t)right[index])) return 0;
        ++index;
    }
    return index == length;
}

int reist_curl_parse_http_url(const char *text, reist_curl_url_t *url) {
    static const char http_scheme[] = "http://";
    static const char https_scheme[] = "https://";
    if (text == 0 || url == 0) return -22;
    uint32_t index = 0U;
    while (http_scheme[index] != '\0' && lower_ascii((uint8_t)text[index]) == http_scheme[index])
        ++index;
    if (http_scheme[index] == '\0') {
        url->scheme = REIST_CURL_SCHEME_HTTP;
        url->port = 80U;
    } else {
        index = 0U;
        while (https_scheme[index] != '\0' &&
               lower_ascii((uint8_t)text[index]) == https_scheme[index]) ++index;
        if (https_scheme[index] != '\0') return -95;
        url->scheme = REIST_CURL_SCHEME_HTTPS;
        url->port = 443U;
    }
    uint32_t host_length = 0U, label_length = 0U;
    int previous_hyphen = 0;
    while (text[index] != '\0' && text[index] != '/' && text[index] != ':' && text[index] != '?') {
        uint8_t value = (uint8_t)text[index++];
        int alphanumeric = (value >= 'a' && value <= 'z') ||
                           (value >= 'A' && value <= 'Z') ||
                           (value >= '0' && value <= '9');
        if ((!alphanumeric && value != '-' && value != '.') ||
            host_length + 1U >= REIST_CURL_HOST_CAPACITY) return -22;
        if (value == '.') {
            if (label_length == 0U || label_length > 63U || previous_hyphen)
                return -22;
            label_length = 0U;
            previous_hyphen = 0;
        } else {
            if (label_length == 0U && value == '-') return -22;
            ++label_length;
            previous_hyphen = value == '-';
        }
        url->host[host_length++] = (char)value;
    }
    if (host_length == 0U || label_length == 0U || label_length > 63U ||
        previous_hyphen) return -22;
    url->host[host_length] = '\0';
    if (text[index] == ':') {
        ++index;
        uint32_t port = 0U, digits = 0U;
        while (text[index] >= '0' && text[index] <= '9') {
            if (digits >= 5U) return -22;
            uint32_t digit = (uint32_t)(text[index++] - '0');
            if (port > (65535U - digit) / 10U) return -22;
            port = port * 10U + digit;
            ++digits;
        }
        if (digits == 0U || port == 0U ||
            (text[index] != '\0' && text[index] != '/' && text[index] != '?')) return -22;
        url->port = (uint16_t)port;
    }
    uint32_t path_length = 0U;
    if (text[index] == '\0') {
        url->path[path_length++] = '/';
    } else {
        if (text[index] == '?') url->path[path_length++] = '/';
        while (text[index] != '\0') {
            uint8_t value = (uint8_t)text[index++];
            if (value <= 0x20U || value >= 0x7fU || value == '#' ||
                path_length + 1U >= REIST_CURL_PATH_CAPACITY) return -22;
            url->path[path_length++] = (char)value;
        }
    }
    url->path[path_length] = '\0';
    return 0;
}

int reist_curl_find_header_end(const uint8_t *data, uint32_t length,
                               uint32_t *body_offset) {
    if (data == 0 || body_offset == 0) return -22;
    uint32_t scan = length > REIST_CURL_HEADER_CAPACITY ? REIST_CURL_HEADER_CAPACITY : length;
    for (uint32_t index = 3U; index < scan; ++index) {
        if (data[index - 3U] == '\r' && data[index - 2U] == '\n' &&
            data[index - 1U] == '\r' && data[index] == '\n') {
            *body_offset = index + 1U;
            return 0;
        }
    }
    return length >= REIST_CURL_HEADER_CAPACITY ? -90 : -11;
}

static int parse_decimal(const uint8_t *data, uint32_t length,
                         uint32_t *value_out) {
    uint32_t value = 0U;
    if (length == 0U || value_out == 0) return -22;
    for (uint32_t index = 0U; index < length; ++index) {
        uint32_t digit = (uint32_t)(data[index] - '0');
        if (digit > 9U || value > (UINT32_MAX - digit) / 10U) return -22;
        value = value * 10U + digit;
    }
    *value_out = value;
    return 0;
}

static int field_token(uint8_t value) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9')) return 1;
    static const char extra[] = "!#$%&'*+-.^_`|~";
    for (uint32_t i = 0; extra[i]; ++i) if (value == (uint8_t)extra[i]) return 1;
    return 0;
}
static int copy_field(char *destination, uint32_t capacity,
                       const uint8_t *source, uint32_t length) {
    if (length >= capacity) return -90;
    for (uint32_t i = 0; i < length; ++i) destination[i] = (char)source[i];
    destination[length] = '\0'; return 0;
}

int reist_curl_parse_response_head(const uint8_t *data, uint32_t length,
                                   reist_curl_response_head_t *head) {
    if (data == 0 || head == 0 || length < 16U || length > REIST_CURL_HEADER_CAPACITY) return -22;
    if (!((data[0] == 'H' && data[1] == 'T' && data[2] == 'T' &&
           data[3] == 'P' && data[4] == '/' && data[5] == '1' &&
           data[6] == '.' && (data[7] == '0' || data[7] == '1')) &&
          data[8] == ' ' && data[9] >= '0' && data[9] <= '9' &&
          data[10] >= '0' && data[10] <= '9' && data[11] >= '0' &&
          data[11] <= '9' && data[12] == ' ')) return -84;
    *head = (reist_curl_response_head_t){0};
    head->status = (uint32_t)(data[9] - '0') * 100U +
        (uint32_t)(data[10] - '0') * 10U + (uint32_t)(data[11] - '0');
    if (head->status < 100U || head->status > 599U) return -84;
    uint32_t fields_seen = 0U;
    uint32_t line = 12U;
    while (line + 1U < length &&
           !(data[line] == '\r' && data[line + 1U] == '\n')) {
        if ((data[line] < 0x20U && data[line] != '\t') || data[line] == 0x7FU) return -84;
        ++line;
    }
    if (line + 1U >= length) return -84;
    line += 2U;
    while (line + 1U < length) {
        if (data[line] == '\r' && data[line + 1U] == '\n')
            return line + 2U == length &&
                !(head->content_length_present && (fields_seen & 1U)) ? 0 : -84;
        uint32_t end = line;
        while (end + 1U < length &&
               !(data[end] == '\r' && data[end + 1U] == '\n')) ++end;
        if (end + 1U >= length) return -84;
        uint32_t colon = line;
        while (colon < end && data[colon] != ':') ++colon;
        if (colon == line || colon == end) return -84;
        for (uint32_t i = line; i < colon; ++i) if (!field_token(data[i])) return -84;
        for (uint32_t i = colon + 1U; i < end; ++i)
            if ((data[i] < 0x20U && data[i] != '\t') || data[i] == 0x7FU) return -84;
        uint32_t value = colon + 1U;
        while (value < end && (data[value] == ' ' || data[value] == '\t'))
            ++value;
        uint32_t value_end = end;
        while (value_end > value &&
               (data[value_end - 1U] == ' ' || data[value_end - 1U] == '\t'))
            --value_end;
        if (equal_ascii_case(data + line, colon - line, "content-length")) {
            uint32_t parsed = 0U;
            if (parse_decimal(data + value, value_end - value, &parsed) != 0 ||
                (head->content_length_present &&
                 head->content_length != parsed)) return -84;
            head->content_length = parsed;
            head->content_length_present = 1U;
        } else if (equal_ascii_case(data + line, colon - line, "transfer-encoding")) {
            if (fields_seen & 1U) return -84;
            fields_seen |= 1U;
            head->chunked = equal_ascii_case(data + value, value_end - value, "chunked");
            head->transfer_encoding_unsupported = !head->chunked;
        } else {
            uint32_t flag = 0U, capacity = 0U;
            char *destination = 0;
            if (equal_ascii_case(data + line, colon - line, "location")) {
                flag = 2U; destination = head->location; capacity = sizeof(head->location);
            } else if (equal_ascii_case(data + line, colon - line, "content-type")) {
                flag = 4U; destination = head->content_type; capacity = sizeof(head->content_type);
            } else if (equal_ascii_case(data + line, colon - line, "content-encoding")) {
                flag = 8U; destination = head->content_encoding; capacity = sizeof(head->content_encoding);
            }
            if (flag) {
                if (fields_seen & flag) return -84;
                fields_seen |= flag;
                int copied = copy_field(destination, capacity, data + value, value_end - value);
                if (copied != 0) return copied;
            }
        }
        line = end + 2U;
    }
    return -84;
}
