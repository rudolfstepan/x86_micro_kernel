/**
 * @file curl.c
 * @brief Bounded curl-compatible HTTP/1.x and HTTPS download client.
 *
 * HTTPS uses the reusable Ring-3 REIST TLS client with mandatory trust-chain,
 * time and hostname verification. HTTP remains available without implying
 * transport security. Both paths use fixed buffers and finite deadlines.
 */
#include "x86os.h"
#include "curl_http.h"
#include "reist/tls.h"

#if defined(REIST_CURL_TLS_RUNTIME_PROBE)
extern const uint8_t reist_tls_runtime_test_ca_pem[];
extern const size_t reist_tls_runtime_test_ca_pem_size;
#endif

#define CURL_REQUEST_CAPACITY X86OS_TCP_MAX_SEGMENT
#define CURL_DEFAULT_MAX_BYTES (1024U * 1024U)
#define CURL_HARD_MAX_BYTES (16U * 1024U * 1024U)
#define CURL_DNS_TIMEOUT_MS 3000U
#define CURL_CONNECT_TIMEOUT_MS 5000U
#define CURL_IO_TIMEOUT_MS 5000U
#define CURL_TLS_HANDSHAKE_TIMEOUT_MS 30000U
#define CURL_TRANSFER_DEADLINE_MS 30000U
#define CURL_OUTPUT_PATH_CAPACITY 256U

typedef struct curl_options {
    const char *url;
    const char *output;
    uint32_t maximum_bytes;
} curl_options_t;

typedef int (*curl_stream_send_fn)(void *stream, const uint8_t *data,
                                   uint32_t length);
typedef int (*curl_stream_receive_fn)(void *stream, uint8_t *data,
                                      uint32_t capacity);
typedef struct curl_stream {
    void *stream;
    curl_stream_send_fn send;
    curl_stream_receive_fn receive;
} curl_stream_t;

typedef enum curl_failure_stage {
    CURL_STAGE_TCP = 1,
    CURL_STAGE_TLS = 2,
    CURL_STAGE_OUTPUT = 3,
    CURL_STAGE_REQUEST = 4,
    CURL_STAGE_RESPONSE = 5
} curl_failure_stage_t;

static reist_tls_context_t tls_context;

static void wipe_bytes(uint8_t *data, uint32_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static void print_failure(curl_failure_stage_t stage, int status) {
    const char *message = stage == CURL_STAGE_TCP
        ? "curl: TCP connection failed, status="
        : stage == CURL_STAGE_TLS
            ? "curl: TLS setup or authentication failed, status="
            : stage == CURL_STAGE_OUTPUT
                ? "curl: output publication failed, status="
                : stage == CURL_STAGE_REQUEST
                    ? "curl: request transmission failed, status="
                    : "curl: invalid or incomplete response, status=";
    x86os_puts(message); x86os_print_number(status); x86os_putchar('\n');
}

static void print_timeout(curl_failure_stage_t stage) {
    const char *message = stage == CURL_STAGE_TCP
        ? "curl: TCP connection timed out, status="
        : stage == CURL_STAGE_TLS
            ? "curl: TLS setup or authentication timed out, status="
            : stage == CURL_STAGE_OUTPUT
                ? "curl: output publication timed out, status="
                : stage == CURL_STAGE_REQUEST
                    ? "curl: request transmission timed out, status="
                    : "curl: response transfer timed out, status=";
    x86os_puts(message); x86os_print_number(-110); x86os_putchar('\n');
}

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
    uint16_t default_port = url->scheme == REIST_CURL_SCHEME_HTTPS ? 443U : 80U;
    if (url->port != default_port &&
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

static int tcp_transport_send(void *opaque, const uint8_t *data,
                              uint32_t length, uint32_t timeout_ms) {
    x86os_tcp_socket_t socket = *(x86os_tcp_socket_t *)opaque;
    uint32_t amount = length;
    if (amount > X86OS_TCP_MAX_SEGMENT) amount = X86OS_TCP_MAX_SEGMENT;
    x86os_tcp_io_t io = {
        X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, amount, timeout_ms};
    return x86os_tcp_send(&io, data);
}

static int tcp_transport_receive(void *opaque, uint8_t *data,
                                 uint32_t capacity, uint32_t timeout_ms) {
    x86os_tcp_socket_t socket = *(x86os_tcp_socket_t *)opaque;
    uint32_t amount = capacity;
    if (amount > X86OS_TCP_RECEIVE_CAPACITY)
        amount = X86OS_TCP_RECEIVE_CAPACITY;
    x86os_tcp_io_t io = {
        X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, amount, timeout_ms};
    return x86os_tcp_receive(&io, data);
}

static int plain_send(void *opaque, const uint8_t *data, uint32_t length) {
    return tcp_transport_send(opaque, data, length, CURL_IO_TIMEOUT_MS);
}

static int plain_receive(void *opaque, uint8_t *data, uint32_t capacity) {
    return tcp_transport_receive(opaque, data, capacity, CURL_IO_TIMEOUT_MS);
}

static int tls_send(void *opaque, const uint8_t *data, uint32_t length) {
    return reist_tls_client_write((reist_tls_context_t *)opaque, data, length);
}

static int tls_receive(void *opaque, uint8_t *data, uint32_t capacity) {
    return reist_tls_client_read((reist_tls_context_t *)opaque, data, capacity);
}

static int send_all(const curl_stream_t *stream, const uint8_t *data,
                    uint32_t length) {
    uint32_t sent = 0U;
    while (sent < length) {
        uint32_t amount = length - sent;
        if (amount > X86OS_TCP_MAX_SEGMENT) amount = X86OS_TCP_MAX_SEGMENT;
        int result = stream->send(stream->stream, data + sent, amount);
        if (result <= 0 || (uint32_t)result > amount)
            return result < 0 ? result : -5;
        sent += (uint32_t)result;
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

static int receive_body(const curl_stream_t *stream, int output,
                        uint32_t maximum_bytes) {
    uint8_t header[REIST_CURL_HEADER_CAPACITY];
    uint32_t header_used = 0U, body_offset = 0U;
    int status = -11;
    while (status == -11) {
        int received = stream->receive(
            stream->stream, header + header_used, sizeof(header) - header_used);
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
        int received = stream->receive(stream->stream, buffer, capacity);
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
        x86os_puts("Usage: curl [-o file] [--max-bytes n] "
                   "http[s]://host/path\n"
                   "HTTPS verifies the CA chain, RTC and exact host name.\n");
        return 0;
    }
    curl_options_t options;
    if (parse_options(argc, argv, &options) != 0) {
        x86os_puts("curl: invalid arguments (try curl --help)\n");
        return 2;
    }
    reist_curl_url_t url;
    int result = reist_curl_parse_http_url(options.url, &url);
    if (result != 0) {
        x86os_puts("curl: invalid URL\n");
        return 2;
    }
    char request[CURL_REQUEST_CAPACITY]; uint32_t request_length = 0U;
    if (build_request(&url, request, &request_length) != 0) {
        x86os_puts("curl: URL is too long for the bounded request\n");
        return 2;
    }
    if (url.scheme == REIST_CURL_SCHEME_HTTPS) {
        int64_t rtc = 0;
        uint64_t monotonic = 0U;
        uint8_t entropy[32];
        result = reist_tls_rtc_time(0, &rtc);
        if (result == 0) result = reist_tls_monotonic_time(0, &monotonic);
        if (result == 0)
            result = reist_tls_hardware_entropy(0, entropy, sizeof(entropy));
        wipe_bytes(entropy, sizeof(entropy));
        if (result != 0) {
            x86os_puts("curl: TLS clock or hardware entropy unavailable, status=");
            x86os_print_number(result); x86os_putchar('\n');
            return 1;
        }
    }
    uint32_t address = 0U;
    if (parse_ipv4(url.host, &address) != 0) {
        x86os_dns_result_t dns;
        int dns_status = x86os_dns_resolve(
            url.host, CURL_DNS_TIMEOUT_MS, &dns);
        if (dns_status != 0) {
            x86os_puts("curl: name resolution failed, status=");
            x86os_print_number(dns_status); x86os_putchar('\n');
            return 1;
        }
        address = dns.address;
    }

    x86os_tcp_socket_t socket = 0U;
    int tls_open = 0;
    curl_failure_stage_t failure_stage = CURL_STAGE_TCP;
    curl_stream_t stream = {&socket, plain_send, plain_receive};
    result = x86os_tcp_socket_open(&socket);
    if (result == 0) {
        x86os_tcp_connect_t connect = {
            X86OS_TCP_SOCKET_VERSION, sizeof(connect), socket, address,
            url.port, 0U, CURL_CONNECT_TIMEOUT_MS};
        result = x86os_tcp_connect(&connect);
    }
    if (result == 0 && url.scheme == REIST_CURL_SCHEME_HTTPS) {
        failure_stage = CURL_STAGE_TLS;
        reist_tls_platform_t platform = {
            REIST_TLS_ABI_VERSION, sizeof(platform), 0, reist_tls_rtc_time,
            reist_tls_monotonic_time, reist_tls_hardware_entropy,
            reist_tls_heap_allocate, reist_tls_heap_free};
        reist_tls_transport_t transport = {
            REIST_TLS_ABI_VERSION, sizeof(transport), &socket,
            tcp_transport_send, tcp_transport_receive};
        reist_tls_client_options_t tls_options = {
            REIST_TLS_ABI_VERSION, sizeof(tls_options), url.host,
            CURL_TLS_HANDSHAKE_TIMEOUT_MS, CURL_IO_TIMEOUT_MS, 0, 0U};
#if defined(REIST_CURL_TLS_RUNTIME_PROBE)
        tls_options.trust_anchors_pem = reist_tls_runtime_test_ca_pem;
        tls_options.trust_anchors_pem_size =
            (uint32_t)reist_tls_runtime_test_ca_pem_size;
#endif
        result = reist_tls_client_open(
            &tls_context, &platform, &transport, &tls_options);
        if (result == 0) {
            tls_open = 1;
            stream = (curl_stream_t){&tls_context, tls_send, tls_receive};
        }
    }

    char temporary[CURL_OUTPUT_PATH_CAPACITY];
    int output = X86OS_STDOUT_FILENO;
    if (result == 0 && options.output != 0) {
        failure_stage = CURL_STAGE_OUTPUT;
        if (make_temporary_path(options.output, temporary) != 0) {
            x86os_puts("curl: output path is too long\n");
            result = -22;
        } else {
            output = x86os_create(temporary);
            if (output < 0) {
                x86os_puts(
                    "curl: temporary output already exists or is invalid\n");
                result = output;
            }
        }
    }
    if (result == 0) {
        failure_stage = CURL_STAGE_REQUEST;
        result = send_all(&stream, (const uint8_t *)request, request_length);
    }
    if (result == 0) {
        failure_stage = CURL_STAGE_RESPONSE;
        result = receive_body(&stream, output, options.maximum_bytes);
    }
    if (tls_open) (void)reist_tls_client_close(&tls_context);
    if (socket != 0U) (void)x86os_tcp_socket_close(socket, 2000U);

    if (options.output != 0 && output >= 0 && output != X86OS_STDOUT_FILENO) {
        int close_status = x86os_close(output);
        if (result == 0 && close_status == 0)
            result = x86os_rename(temporary, options.output);
        if (result != 0) (void)x86os_unlink(temporary);
    }
    if (result != 0) {
        if (result == -90)
            x86os_puts("curl: response exceeds byte limit\n");
        else if (result == -110)
            print_timeout(failure_stage);
        else
            print_failure(failure_stage, result);
        return 1;
    }
    return 0;
}
