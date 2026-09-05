#ifndef REIST_CURL_HTTP_H
#define REIST_CURL_HTTP_H

#include <stdint.h>

#define REIST_CURL_HOST_CAPACITY 254U
#define REIST_CURL_PATH_CAPACITY 256U
#define REIST_CURL_HEADER_CAPACITY 8192U
#define REIST_CURL_LOCATION_CAPACITY 256U

typedef enum reist_curl_scheme {
    REIST_CURL_SCHEME_HTTP = 1,
    REIST_CURL_SCHEME_HTTPS = 2
} reist_curl_scheme_t;

typedef struct reist_curl_url {
    char host[REIST_CURL_HOST_CAPACITY];
    char path[REIST_CURL_PATH_CAPACITY];
    uint16_t port;
    uint16_t scheme;
} reist_curl_url_t;

typedef struct reist_curl_response_head {
    uint32_t status;
    uint32_t content_length;
    uint32_t content_length_present;
    uint32_t transfer_encoding_unsupported;
    uint32_t chunked;
    char location[REIST_CURL_LOCATION_CAPACITY];
    char content_type[128U];
    char content_encoding[32U];
} reist_curl_response_head_t;

int reist_curl_parse_http_url(const char *text, reist_curl_url_t *url);
int reist_curl_find_header_end(const uint8_t *data, uint32_t length,
                               uint32_t *body_offset);
int reist_curl_parse_response_head(const uint8_t *data, uint32_t length,
                                   reist_curl_response_head_t *head);

#endif
