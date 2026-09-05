#ifndef REIST_BROWSER_RESPONSE_H
#define REIST_BROWSER_RESPONSE_H
#include <stddef.h>
#include <stdint.h>
#include "../../../programs/curl_http.h"

#define BROWSER_REDIRECT_LIMIT 5U
#define BROWSER_REDIRECT_DEADLINE_MS 30000U
typedef struct browser_response {
    uint32_t status, body_offset, body_length;
    char redirect[REIST_CURL_LOCATION_CAPACITY];
} browser_response_t;
/* CURL --include contract: original headers followed by an already decoded
 * body. Return 1 for a validated redirect, 0 for content, negative on error. */
int browser_response_open(const uint8_t *bytes, size_t length, const char *url,
                           uint32_t image, browser_response_t *result);
#endif
