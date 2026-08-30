#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "userspace/programs/curl_http.h"

int main(void) {
    reist_curl_url_t url;
    assert(reist_curl_parse_http_url("http://example.com", &url) == 0);
    assert(strcmp(url.host, "example.com") == 0);
    assert(strcmp(url.path, "/") == 0 && url.port == 80U);
    assert(reist_curl_parse_http_url(
        "http://10.0.2.2:8080/a?b=1", &url) == 0);
    assert(strcmp(url.host, "10.0.2.2") == 0);
    assert(strcmp(url.path, "/a?b=1") == 0 && url.port == 8080U);
    assert(reist_curl_parse_http_url("https://example.com/", &url) == -95);
    assert(reist_curl_parse_http_url("http://:80/", &url) < 0);
    assert(reist_curl_parse_http_url("http://example.com:0/", &url) < 0);
    assert(reist_curl_parse_http_url("http://example.com/a#fragment", &url) < 0);

    static const uint8_t response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
    uint32_t body = 0U;
    assert(reist_curl_find_header_end(
        response, sizeof(response) - 1U, &body) == 0);
    assert(body == sizeof(response) - 1U - 5U);
    reist_curl_response_head_t head;
    assert(reist_curl_parse_response_head(response, body, &head) == 0);
    assert(head.status == 200U && head.content_length_present == 1U &&
           head.content_length == 5U &&
           head.transfer_encoding_unsupported == 0U);

    static const uint8_t chunked[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    assert(reist_curl_find_header_end(
        chunked, sizeof(chunked) - 1U, &body) == 0);
    assert(reist_curl_parse_response_head(chunked, body, &head) == 0);
    assert(head.transfer_encoding_unsupported == 1U);

    static const uint8_t conflicting[] =
        "HTTP/1.0 200 OK\r\nContent-Length: 4\r\nContent-Length: 5\r\n\r\n";
    assert(reist_curl_find_header_end(
        conflicting, sizeof(conflicting) - 1U, &body) == 0);
    assert(reist_curl_parse_response_head(conflicting, body, &head) < 0);
    return 0;
}
