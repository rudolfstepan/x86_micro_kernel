#ifndef REIST_CURL_HTTP_H
#define REIST_CURL_HTTP_H

#include <stdint.h>

#define REIST_CURL_HOST_CAPACITY 254U
#define REIST_CURL_PATH_CAPACITY 8193U
#define REIST_CURL_HEADER_CAPACITY 16384U
#define REIST_CURL_LOCATION_CAPACITY 8193U
#define REIST_CURL_IPC_MAGIC 0x314C5243U /* private CRL1, not a socket ABI */
#define REIST_CURL_IPC_BODY_LIMIT (1024U*1024U)
#define REIST_CURL_IPC_DATA 2032U
typedef struct reist_curl_ipc_packet {
    uint32_t magic, endpoint, offset, total;
    uint8_t bytes[REIST_CURL_IPC_DATA];
} reist_curl_ipc_packet_t;
int reist_curl_ipc_accept(const reist_curl_ipc_packet_t *,uint32_t length,
    uint32_t endpoint,uint8_t *output,uint32_t capacity,uint32_t *used,uint32_t *total);

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
