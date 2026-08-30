/** @file curl_http.c @brief Bounded HTTP URL and response-head parser. */
#include "curl_http.h"

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
    static const char scheme[] = "http://";
    if (text == 0 || url == 0) return -22;
    uint32_t index = 0U;
    while (scheme[index] != '\0') {
        if (text[index] != scheme[index]) return -95;
        ++index;
    }
    uint32_t host_length = 0U;
    while (text[index] != '\0' && text[index] != '/' && text[index] != ':') {
        uint8_t value = (uint8_t)text[index++];
        if (value <= 0x20U || value >= 0x7fU || value == '[' || value == ']' ||
            host_length + 1U >= REIST_CURL_HOST_CAPACITY) return -22;
        url->host[host_length++] = (char)value;
    }
    if (host_length == 0U) return -22;
    url->host[host_length] = '\0';
    url->port = 80U;
    if (text[index] == ':') {
        ++index;
        uint32_t port = 0U, digits = 0U;
        while (text[index] >= '0' && text[index] <= '9') {
            uint32_t digit = (uint32_t)(text[index++] - '0');
            if (port > (65535U - digit) / 10U) return -22;
            port = port * 10U + digit;
            ++digits;
        }
        if (digits == 0U || port == 0U ||
            (text[index] != '\0' && text[index] != '/')) return -22;
        url->port = (uint16_t)port;
    }
    uint32_t path_length = 0U;
    if (text[index] == '\0') {
        url->path[path_length++] = '/';
    } else {
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
    for (uint32_t index = 3U; index < length; ++index) {
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

int reist_curl_parse_response_head(const uint8_t *data, uint32_t length,
                                   reist_curl_response_head_t *head) {
    if (data == 0 || head == 0 || length < 12U) return -22;
    if (!((data[0] == 'H' && data[1] == 'T' && data[2] == 'T' &&
           data[3] == 'P' && data[4] == '/' && data[5] == '1' &&
           data[6] == '.' && (data[7] == '0' || data[7] == '1')) &&
          data[8] == ' ' && data[9] >= '0' && data[9] <= '9' &&
          data[10] >= '0' && data[10] <= '9' && data[11] >= '0' &&
          data[11] <= '9')) return -84;
    *head = (reist_curl_response_head_t){
        (uint32_t)(data[9] - '0') * 100U +
        (uint32_t)(data[10] - '0') * 10U + (uint32_t)(data[11] - '0'),
        0U, 0U, 0U};
    uint32_t line = 12U;
    while (line + 1U < length &&
           !(data[line] == '\r' && data[line + 1U] == '\n')) ++line;
    if (line + 1U >= length) return -84;
    line += 2U;
    while (line + 1U < length) {
        if (data[line] == '\r' && data[line + 1U] == '\n') return 0;
        uint32_t end = line;
        while (end + 1U < length &&
               !(data[end] == '\r' && data[end + 1U] == '\n')) ++end;
        if (end + 1U >= length) return -84;
        uint32_t colon = line;
        while (colon < end && data[colon] != ':') ++colon;
        if (colon == line || colon == end) return -84;
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
        } else if (equal_ascii_case(
                       data + line, colon - line, "transfer-encoding") &&
                   !equal_ascii_case(data + value, value_end - value,
                                     "identity")) {
            head->transfer_encoding_unsupported = 1U;
        }
        line = end + 2U;
    }
    return -84;
}
