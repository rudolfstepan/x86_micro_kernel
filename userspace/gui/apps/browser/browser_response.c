#include "browser_response.h"
#include "reist/gui/html_document.h"

static char lower(char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
static int equal(const char *text, size_t length, const char *expected) {
    size_t i = 0;
    while (i < length && expected[i] && lower(text[i]) == expected[i]) ++i;
    return i == length && !expected[i];
}
static size_t length_of(const char *text) {
    size_t length = 0; while (text[length]) ++length; return length;
}
static int secure(const char *url) {
    size_t i = 0; static const char prefix[] = "https://";
    while (prefix[i] && url[i] && lower(url[i]) == prefix[i]) ++i;
    return !prefix[i];
}
static int network(const char *url) {
    size_t i = 0; static const char prefix[] = "http://";
    while (prefix[i] && url[i] && lower(url[i]) == prefix[i]) ++i;
    return !prefix[i] || secure(url);
}
static int supported_type(const char *value, uint32_t image) {
    if (!value[0]) return 1; /* Legacy servers: decode/parse still validates. */
    size_t end = 0; while (value[end] && value[end] != ';') ++end;
    size_t mime_end = end;
    while (mime_end && (value[mime_end-1] == ' ' || value[mime_end-1] == '\t')) --mime_end;
    if (image) {
        static const char *const types[] = {"image/png", "image/jpeg", "image/gif", "image/bmp", "image/x-ms-bmp", "application/octet-stream"};
        for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); ++i)
            if (equal(value, mime_end, types[i])) return 1;
        return 0;
    }
    if (!equal(value, mime_end, "text/html")) return 0;
    uint32_t charset_seen = 0;
    while (value[end]) {
        ++end;
        while (value[end] == ' ' || value[end] == '\t') ++end;
        size_t key = end;
        while (value[end] && value[end] != '=' && value[end] != ';') ++end;
        size_t key_end = end;
        while (key_end > key && (value[key_end-1] == ' ' || value[key_end-1] == '\t')) --key_end;
        if (value[end] != '=') return 0;
        ++end; while (value[end] == ' ' || value[end] == '\t') ++end;
        uint32_t quoted = value[end] == '"';
        if (quoted) ++end;
        size_t start = end;
        while (value[end] && (quoted ? value[end] != '"' : value[end] != ';')) ++end;
        size_t parameter_end = end;
        if (quoted) {
            if (value[end] != '"') return 0;
            ++end; while (value[end] == ' ' || value[end] == '\t') ++end;
            if (value[end] && value[end] != ';') return 0;
        } else while (parameter_end > start &&
            (value[parameter_end-1] == ' ' || value[parameter_end-1] == '\t')) --parameter_end;
        if (equal(value + key, key_end - key, "charset")) {
            if (charset_seen++) return 0;
            if (!equal(value + start, parameter_end - start, "utf-8") &&
                !equal(value + start, parameter_end - start, "us-ascii")) return 0;
        }
    }
    return 1;
}

int browser_response_open(const uint8_t *bytes, size_t length, const char *url,
                           uint32_t image, browser_response_t *result) {
    if (!result) return -22;
    *result = (browser_response_t){0};
    if (!bytes || !url || length > UINT32_MAX) return -22;
    size_t url_length = 0;
    while (url_length < REIST_CURL_LOCATION_CAPACITY && url[url_length]) ++url_length;
    if (url_length == REIST_CURL_LOCATION_CAPACITY || !network(url)) return -22;
    uint32_t offset = 0;
    reist_curl_response_head_t head;
    for (uint32_t interim = 0; interim <= 4; ++interim) {
        uint32_t head_length = 0;
        int status = reist_curl_find_header_end(bytes + offset, (uint32_t)length - offset, &head_length);
        if (status != 0 || head_length > REIST_CURL_HEADER_CAPACITY - offset) return -84;
        status = reist_curl_parse_response_head(bytes + offset, head_length, &head);
        if (status != 0 || head.transfer_encoding_unsupported) return -84;
        offset += head_length;
        if (head.status >= 200) break;
        if (head.status == 101 || interim == 4 || head.content_length_present || head.chunked) return -84;
    }
    result->status = head.status; result->body_offset = offset;
    result->body_length = (uint32_t)length - offset;
    if (head.content_length_present && head.status != 304 &&
        head.content_length != result->body_length) return -84;
    if (head.status == 301 || head.status == 302 || head.status == 303 || head.status == 307 || head.status == 308) {
        if (!head.location[0] || reist_html_url_resolve(url, head.location,
            result->redirect, sizeof(result->redirect)) != 0 || !network(result->redirect) ||
            (secure(url) && !secure(result->redirect))) return -13;
        /* RFC 9110: an absent fragment inherits the original target fragment. */
        size_t location_length = length_of(head.location), i = 0;
        while (i < location_length && head.location[i] != '#') ++i;
        if (i == location_length) {
            size_t from = 0; while (url[from] && url[from] != '#') ++from;
            if (url[from]) {
                size_t used = length_of(result->redirect), suffix = length_of(url + from);
                if (used + suffix >= sizeof(result->redirect)) return -90;
                for (size_t n = 0; n <= suffix; ++n) result->redirect[used+n] = url[from+n];
            }
        }
        return 1;
    }
    if (head.status < 200 || head.status >= 300 || head.status == 204 || head.status == 205) return -5;
    if (head.status == 206) return -95; /* No Range request was made. */
    if ((head.content_encoding[0] && !equal(head.content_encoding, length_of(head.content_encoding), "identity")) ||
        !supported_type(head.content_type, image)) return -95;
    return 0;
}
