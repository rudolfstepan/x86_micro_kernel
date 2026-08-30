/**
 * @file curl.c
 * @brief Bounded curl-compatible HTTP/1.x download client.
 *
 * This first profile intentionally supports cleartext http:// only. It uses
 * the public Ring-3 DNS, TCP and VFS ABIs, fixed buffers, a total transfer
 * deadline and a caller-selected bounded byte limit. HTTPS is rejected until
 * a validated TLS service exists; no security property is implied by HTTP.
 */
#include "x86os.h"
#include "curl_http.h"

#define CURL_REQUEST_CAPACITY X86OS_TCP_MAX_SEGMENT
#define CURL_DEFAULT_MAX_BYTES (1024U * 1024U)
#define CURL_HARD_MAX_BYTES (16U * 1024U * 1024U)
#define CURL_DNS_TIMEOUT_MS 3000U
#define CURL_CONNECT_TIMEOUT_MS 5000U
#define CURL_IO_TIMEOUT_MS 3000U
#define CURL_TRANSFER_DEADLINE_MS 30000U
#define CURL_OUTPUT_PATH_CAPACITY 256U

typedef struct curl_options {
    const char *url;
    const char *output;
    uint32_t maximum_bytes;
} curl_options_t;

static uint32_t bounded_length(const char *text, uint32_t capacity) {
    uint32_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int text_equal(const char *left, const char *right) {
    uint32_t index = 0U;
    if (left == 0 || right == 0) return 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int parse_positive(const char *text, uint32_t maximum,
                          uint32_t *result) {
    uint32_t value = 0U, digits = 0U;
    if (text == 0 || result == 0) return -1;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (value > (maximum - digit) / 10U) return -1;
        value = value * 10U + digit;
        ++digits;
    }
    if (*text != '\0' || digits == 0U || value == 0U || value > maximum)
        return -1;
    *result = value;
    return 0;
}

static int parse_options(int argc, char **argv, curl_options_t *options) {
    if (argc < 2 || argv == 0 || options == 0) return -1;
    *options = (curl_options_t){0, 0, CURL_DEFAULT_MAX_BYTES};
    for (int index = 1; index < argc; ++index) {
        if (text_equal(argv[index], "-o")) {
            if (++index >= argc || options->output != 0) return -1;
            options->output = argv[index];
        } else if (text_equal(argv[index], "--max-bytes")) {
            if (++index >= argc || parse_positive(
                    argv[index], CURL_HARD_MAX_BYTES,
                    &options->maximum_bytes) != 0) return -1;
        } else if (argv[index][0] == '-') {
            return -1;
        } else if (options->url != 0) {
            return -1;
        } else {
            options->url = argv[index];
        }
    }
    return options->url == 0 ? -1 : 0;
}

static int append_text(char *buffer, uint32_t *used, uint32_t capacity,
                       const char *text) {
    if (buffer == 0 || used == 0 || text == 0) return -1;
    for (uint32_t index = 0U; text[index] != '\0'; ++index) {
        if (*used + 1U >= capacity) return -1;
        buffer[(*used)++] = text[index];
    }
    buffer[*used] = '\0';
    return 0;
}

static int append_unsigned(char *buffer, uint32_t *used, uint32_t capacity,
                           uint32_t value) {
    char digits[10]; uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) {
        char one[2] = {digits[--count], '\0'};
        if (append_text(buffer, used, capacity, one) != 0) return -1;
    }
    return 0;
}

static int build_request(const reist_curl_url_t *url, char *request,
                         uint32_t *request_length) {
    uint32_t used = 0U;
    if (url == 0 || request == 0 || request_length == 0 ||
        append_text(request, &used, CURL_REQUEST_CAPACITY, "GET ") != 0 ||
        append_text(request, &used, CURL_REQUEST_CAPACITY, url->path) != 0 ||
        append_text(request, &used, CURL_REQUEST_CAPACITY,
                    " HTTP/1.0\r\nHost: ") != 0 ||
        append_text(request, &used, CURL_REQUEST_CAPACITY, url->host) != 0)
        return -1;
    if (url->port != 80U &&
        (append_text(request, &used, CURL_REQUEST_CAPACITY, ":") != 0 ||
         append_unsigned(request, &used, CURL_REQUEST_CAPACITY,
                         url->port) != 0)) return -1;
    if (append_text(request, &used, CURL_REQUEST_CAPACITY,
            "\r\nUser-Agent: REIST-curl/1\r\nAccept: */*\r\n"
            "Connection: close\r\n\r\n") != 0) return -1;
    *request_length = used;
    return 0;
}

static int parse_ipv4(const char *text, uint32_t *result) {
    uint32_t address = 0U;
    if (text == 0 || result == 0) return -1;
    for (uint32_t part = 0U; part < 4U; ++part) {
        uint32_t value = 0U, digits = 0U;
        while (*text >= '0' && *text <= '9') {
            value = value * 10U + (uint32_t)(*text++ - '0');
            if (++digits > 3U || value > 255U) return -1;
        }
        if (digits == 0U || (part < 3U && *text++ != '.') ||
            (part == 3U && *text != '\0')) return -1;
        address = (address << 8U) | value;
    }
    *result = address;
    return 0;
}

static int send_all(x86os_tcp_socket_t socket, const uint8_t *data,
                    uint32_t length) {
    uint32_t sent = 0U;
    while (sent < length) {
        uint32_t amount = length - sent;
        if (amount > X86OS_TCP_MAX_SEGMENT) amount = X86OS_TCP_MAX_SEGMENT;
        x86os_tcp_io_t io = {
            X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, amount,
            CURL_IO_TIMEOUT_MS};
        int result = x86os_tcp_send(&io, data + sent);
        if (result != (int)amount) return result < 0 ? result : -5;
        sent += amount;
    }
    return 0;
}

static int write_all(int descriptor, const uint8_t *data, uint32_t length) {
    uint32_t written = 0U;
    while (written < length) {
        int result = x86os_write(
            descriptor, data + written, (size_t)(length - written));
        if (result <= 0) return result < 0 ? result : -5;
        written += (uint32_t)result;
    }
    return 0;
}

static int make_temporary_path(const char *output, char *temporary) {
    static const char suffix[] = ".curl-part";
    uint32_t length = bounded_length(output, CURL_OUTPUT_PATH_CAPACITY);
    if (length == 0U || length >= CURL_OUTPUT_PATH_CAPACITY) return -1;
    uint32_t suffix_length = sizeof(suffix) - 1U;
    if (length + suffix_length + 1U > CURL_OUTPUT_PATH_CAPACITY) return -1;
    for (uint32_t index = 0U; index < length; ++index)
        temporary[index] = output[index];
    for (uint32_t index = 0U; index <= suffix_length; ++index)
        temporary[length + index] = suffix[index];
    return 0;
}

static int receive_body(x86os_tcp_socket_t socket, int output,
                        uint32_t maximum_bytes) {
    uint8_t header[REIST_CURL_HEADER_CAPACITY];
    uint32_t header_used = 0U, body_offset = 0U;
    int status = -11;
    while (status == -11) {
        x86os_tcp_io_t io = {
            X86OS_TCP_SOCKET_VERSION, sizeof(io), socket,
            sizeof(header) - header_used, CURL_IO_TIMEOUT_MS};
        int received = x86os_tcp_receive(&io, header + header_used);
        if (received <= 0) return received == 0 ? -84 : received;
        header_used += (uint32_t)received;
        status = reist_curl_find_header_end(
            header, header_used, &body_offset);
    }
    if (status != 0) return status;
    reist_curl_response_head_t response;
    status = reist_curl_parse_response_head(header, body_offset, &response);
    if (status != 0 || response.transfer_encoding_unsupported) return -84;
    if (response.status < 100U || response.status > 599U) return -84;
    if (response.content_length_present &&
        response.content_length > maximum_bytes) return -90;

    uint32_t total = header_used - body_offset;
    if (total > maximum_bytes) return -90;
    if (response.content_length_present && total > response.content_length)
        return -84;
    if (total != 0U && write_all(output, header + body_offset, total) != 0)
        return -5;
    uint64_t started = 0U;
    if (x86os_monotonic_ms(&started) != 0) return -5;
    uint8_t buffer[X86OS_TCP_RECEIVE_CAPACITY];
    for (;;) {
        if (response.content_length_present && total == response.content_length)
            return 0;
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) != 0 || now < started ||
            now - started >= CURL_TRANSFER_DEADLINE_MS) return -110;
        uint32_t capacity = sizeof(buffer);
        if (response.content_length_present &&
            capacity > response.content_length - total)
            capacity = response.content_length - total;
        if (capacity > maximum_bytes - total) capacity = maximum_bytes - total;
        if (capacity == 0U) return -90;
        x86os_tcp_io_t io = {
            X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, capacity,
            CURL_IO_TIMEOUT_MS};
        int received = x86os_tcp_receive(&io, buffer);
        if (received == 0)
            return response.content_length_present &&
                total != response.content_length ? -84 : 0;
        if (received < 0) return received;
        if ((uint32_t)received > maximum_bytes - total ||
            write_all(output, buffer, (uint32_t)received) != 0) return -90;
        total += (uint32_t)received;
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && argv != 0 && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: curl [-o file] [--max-bytes n] http://host/path\n"
                   "HTTP only; HTTPS/TLS is not implemented.\n");
        return 0;
    }
    curl_options_t options;
    if (parse_options(argc, argv, &options) != 0) {
        x86os_puts("curl: invalid arguments (try curl --help)\n");
        return 2;
    }
    reist_curl_url_t url;
    int result = reist_curl_parse_http_url(options.url, &url);
    if (result == -95) {
        x86os_puts("curl: only http:// is supported; HTTPS needs TLS\n");
        return 2;
    }
    if (result != 0) {
        x86os_puts("curl: invalid URL\n");
        return 2;
    }
    char request[CURL_REQUEST_CAPACITY]; uint32_t request_length = 0U;
    if (build_request(&url, request, &request_length) != 0) {
        x86os_puts("curl: URL is too long for the bounded request\n");
        return 2;
    }
    uint32_t address = 0U;
    if (parse_ipv4(url.host, &address) != 0) {
        x86os_dns_result_t dns;
        if (x86os_dns_resolve(url.host, CURL_DNS_TIMEOUT_MS, &dns) != 0) {
            x86os_puts("curl: name resolution failed\n");
            return 1;
        }
        address = dns.address;
    }

    char temporary[CURL_OUTPUT_PATH_CAPACITY];
    int output = X86OS_STDOUT_FILENO;
    if (options.output != 0) {
        if (make_temporary_path(options.output, temporary) != 0) {
            x86os_puts("curl: output path is too long\n");
            return 2;
        }
        output = x86os_create(temporary);
        if (output < 0) {
            x86os_puts("curl: temporary output already exists or is invalid\n");
            return 1;
        }
    }

    x86os_tcp_socket_t socket = 0U;
    result = x86os_tcp_socket_open(&socket);
    if (result == 0) {
        x86os_tcp_connect_t connect = {
            X86OS_TCP_SOCKET_VERSION, sizeof(connect), socket, address,
            url.port, 0U, CURL_CONNECT_TIMEOUT_MS};
        result = x86os_tcp_connect(&connect);
    }
    if (result == 0)
        result = send_all(socket, (const uint8_t *)request, request_length);
    if (result == 0)
        result = receive_body(socket, output, options.maximum_bytes);
    if (socket != 0U) (void)x86os_tcp_socket_close(socket, 2000U);

    if (options.output != 0) {
        int close_status = x86os_close(output);
        if (result == 0 && close_status == 0)
            result = x86os_rename(temporary, options.output);
        if (result != 0) (void)x86os_unlink(temporary);
    }
    if (result != 0) {
        x86os_puts(result == -90 ? "curl: response exceeds byte limit\n"
                                 : "curl: transfer failed\n");
        return 1;
    }
    return 0;
}
