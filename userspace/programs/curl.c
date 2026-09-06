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

#define CURL_REQUEST_CAPACITY (REIST_CURL_PATH_CAPACITY+1024U)
#define CURL_DEFAULT_MAX_BYTES (1024U * 1024U)
#define CURL_HARD_MAX_BYTES (16U * 1024U * 1024U)
#define CURL_DNS_TIMEOUT_MS 3000U
#define CURL_CONNECT_TIMEOUT_MS 5000U
#define CURL_IO_TIMEOUT_MS 5000U
#define CURL_TLS_HANDSHAKE_TIMEOUT_MS 30000U
#define CURL_TRANSFER_IDLE_TIMEOUT_MS 30000U
#define CURL_TRANSFER_HARD_TIMEOUT_MS 300000U
#define CURL_OUTPUT_PATH_CAPACITY 256U
#define CURL_FILE_BUFFER_CAPACITY 131072U

typedef struct curl_options {
    const char *url;
    const char *output;
    uint32_t maximum_bytes;
    uint32_t include_headers;
    uint32_t ipc_endpoint;
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
/* Conventional curl process error categories; all remain nonzero on failure. */
static int curl_exit_status(curl_failure_stage_t stage,int status) {
    if(status==-110) return 28;
    if(stage==CURL_STAGE_TCP) return 7;
    if(stage==CURL_STAGE_TLS) return 35;
    if(stage==CURL_STAGE_OUTPUT) return 23;
    if(status==-90) return 63;
    return stage==CURL_STAGE_REQUEST ? 55 : 56;
}

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
    *options = (curl_options_t){0, 0, CURL_DEFAULT_MAX_BYTES, 0U, 0U};
    for (int index = 1; index < argc; ++index) {
        if (text_equal(argv[index], "--include") || text_equal(argv[index], "-i")) {
            options->include_headers = 1U;
        } else if (text_equal(argv[index], "-o")) {
            if (++index >= argc || options->output != 0) return -1;
            options->output = argv[index];
        } else if (text_equal(argv[index], "--reist-ipc")) {
            if(++index>=argc || options->ipc_endpoint ||
                parse_positive(argv[index],UINT32_MAX,&options->ipc_endpoint)) return -1;
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
    return !options->url || (options->ipc_endpoint &&
        (options->output || options->maximum_bytes>REIST_CURL_IPC_BODY_LIMIT)) ? -1 : 0;
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
                    " HTTP/1.1\r\nHost: ") != 0 ||
        append_text(request, &used, CURL_REQUEST_CAPACITY, url->host) != 0)
        return -1;
    uint16_t default_port = url->scheme == REIST_CURL_SCHEME_HTTPS ? 443U : 80U;
    if (url->port != default_port &&
        (append_text(request, &used, CURL_REQUEST_CAPACITY, ":") != 0 ||
         append_unsigned(request, &used, CURL_REQUEST_CAPACITY,
                         url->port) != 0)) return -1;
    if (append_text(request, &used, CURL_REQUEST_CAPACITY,
            "\r\nUser-Agent: REIST-curl/1\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
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

static struct {
    uint8_t bytes[CURL_FILE_BUFFER_CAPACITY];
    uint32_t used, enabled;
    int descriptor, failed;
} file_buffer;
static struct { uint8_t *bytes; uint32_t used, capacity; } ipc_output;

static int publish_ipc_output(uint32_t endpoint) {
    if(!ipc_output.bytes || !ipc_output.used) return -84;
    uint64_t started;
    if(x86os_monotonic_ms(&started)) return -5;
    for(uint32_t at=0;at<ipc_output.used;) {
        uint64_t now;
        if(x86os_monotonic_ms(&now) || now<started) return -5;
        if(now-started>=CURL_IO_TIMEOUT_MS) return -110;
        uint32_t n=ipc_output.used-at; if(n>REIST_CURL_IPC_DATA) n=REIST_CURL_IPC_DATA;
        reist_curl_ipc_packet_t p={REIST_CURL_IPC_MAGIC,endpoint,at,ipc_output.used,{0}};
        for(uint32_t i=0;i<n;++i) p.bytes[i]=ipc_output.bytes[at+i];
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),16U+n,{0}};
        for(uint32_t i=0;i<m.length;++i) m.payload[i]=((const uint8_t *)&p)[i];
        int rc=x86os_ipc_send_bulk_timeout(endpoint,&m,(uint32_t)(CURL_IO_TIMEOUT_MS-(now-started)));
        /* Delegation follows spawn. Only an untouched stream may wait for it. */
        if(!at && (rc==-9 || rc==-13)) { if(x86os_sleep_ms(1U)) return -5; continue; }
        if(rc) return rc;
        at+=n;
    }
    return 0;
}

static int write_direct(int descriptor, const uint8_t *data, uint32_t length) {
    uint32_t written = 0U;
    while (written < length) {
        int result = x86os_write(
            descriptor, data + written, (size_t)(length - written));
        if (result <= 0 || (uint32_t)result > length - written) return result < 0 ? result : -5;
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

static int finish_output(const char *temporary, curl_options_t options, int output, int result) {
    int close_status = x86os_close(output);
    if (result == 0 && close_status != 0) result = close_status;
    if (result == 0) result = x86os_rename(temporary, options.output);
    if (result != 0) (void)x86os_unlink(temporary);
    return result;
}

static int write_all(int descriptor,const uint8_t *data,uint32_t length);

typedef struct curl_body_reader {
    const curl_stream_t *stream;
    const uint8_t *bytes;
    uint32_t used, position;
    uint8_t buffer[X86OS_TCP_RECEIVE_CAPACITY];
    uint64_t started, progressed;
} curl_body_reader_t;

static int reader_available(curl_body_reader_t *reader) {
    if (reader->position < reader->used) return 1;
    uint64_t now = 0U, started = reader->started, progressed = reader->progressed;
    if (x86os_monotonic_ms(&now) != 0 || now < started || now < progressed)
        return -5;
    if (now - started >= CURL_TRANSFER_HARD_TIMEOUT_MS ||
        now - progressed >= CURL_TRANSFER_IDLE_TIMEOUT_MS) return -110;
    int received = reader->stream->receive(reader->stream->stream, reader->buffer, sizeof(reader->buffer));
    if (received <= 0) return received;
    if ((uint32_t)received > sizeof(reader->buffer)) return -84;
    if (x86os_monotonic_ms(&reader->progressed) != 0 || reader->progressed < now) return -5;
    reader->bytes = reader->buffer; reader->used = (uint32_t)received; reader->position = 0U;
    return 1;
}
static int reader_byte(curl_body_reader_t *reader, uint8_t *byte) {
    int result = reader_available(reader);
    if (result != 1) return result == 0 ? -84 : result;
    *byte = reader->bytes[reader->position++]; return 0;
}
static int reader_line(curl_body_reader_t *reader, uint8_t *line, uint32_t capacity,
                        uint32_t *length, uint32_t *framing) {
    uint32_t used = 0U;
    for (;;) {
        uint8_t byte = 0;
        int result = reader_byte(reader, &byte);
        if (result != 0) return result;
        if (++*framing > 65536U) return -90;
        if (byte == '\r') {
            result = reader_byte(reader, &byte);
            if (result != 0) return result;
            if (++*framing > 65536U || byte != '\n') return -84;
            *length = used; return 0;
        }
        if (used >= capacity || (byte < 32U && byte != '\t') || byte == 127U) return -84;
        line[used++] = byte;
    }
}
static int reader_write(curl_body_reader_t *reader, int output, uint32_t amount) {
    while (amount) {
        int result = reader_available(reader);
        if (result != 1) return result == 0 ? -84 : result;
        uint32_t part = reader->used - reader->position;
        if (part > amount) part = amount;
        result = write_all(output, reader->bytes + reader->position, part);
        if (result != 0) return result;
        reader->position += part; amount -= part;
    }
    return 0;
}
static int flush_file_buffer(void) {
    int result=write_direct(file_buffer.descriptor,file_buffer.bytes,file_buffer.used);
    file_buffer.used=0;
    if(result) file_buffer.failed=result;
    return result;
}
static int write_all(int descriptor,const uint8_t *data,uint32_t length) {
    if(ipc_output.bytes) {
        if(length>ipc_output.capacity-ipc_output.used) return -90;
        for(uint32_t i=0;i<length;++i) ipc_output.bytes[ipc_output.used+i]=data[i];
        ipc_output.used+=length; return 0;
    }
    /* Only -o's private staging file is buffered. Terminal output stays
     * streaming; no bytes survive this response or a failed transfer. */
    if(!file_buffer.enabled) return write_direct(descriptor,data,length);
    if(descriptor!=file_buffer.descriptor || file_buffer.failed) return -5;
    while(length) {
        uint32_t n=CURL_FILE_BUFFER_CAPACITY-file_buffer.used;
        if(n>length) n=length;
        for(uint32_t i=0;i<n;++i) file_buffer.bytes[file_buffer.used+i]=data[i];
        file_buffer.used+=n; data+=n; length-=n;
        if(file_buffer.used==CURL_FILE_BUFFER_CAPACITY) {
            int result=flush_file_buffer(); if(result) return result;
        }
    }
    return 0;
}
static int chunk_token(uint8_t ch) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) return 1;
    static const char punctuation[] = "!#$%&'*+-.^_`|~";
    for (uint32_t i = 0; i < sizeof(punctuation) - 1U; ++i)
        if (ch == (uint8_t)punctuation[i]) return 1;
    return 0;
}
/* RFC 9112 section 7.1.1: ignore unknown values, not malformed framing. */
static int chunk_extensions(const uint8_t *line, uint32_t at, uint32_t length) {
    while (at < length) {
        while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at == length || line[at++] != ';') return -84;
        while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
        uint32_t name = at;
        while (at < length && chunk_token(line[at])) ++at;
        if (at == name) return -84;
        uint32_t name_end = at;
        while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at == length) return at == name_end ? 0 : -84;
        if (line[at] == ';') continue;
        if (line[at++] != '=') return -84;
        while (at < length && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at == length) return -84;
        if (line[at] == '"') {
            ++at;
            while (at < length && line[at] != '"') {
                if (line[at++] == '\\') {
                    if (at == length) return -84;
                    ++at;
                }
            }
            if (at == length) return -84;
            ++at;
        } else {
            uint32_t value = at;
            while (at < length && chunk_token(line[at])) ++at;
            if (value == at) return -84;
        }
    }
    return 0;
}
static int receive_chunked(curl_body_reader_t *reader, int output, uint32_t maximum_bytes) {
    uint32_t total = 0U, framing = 0U;
    for (uint32_t chunk = 0U; chunk < 16384U; ++chunk) {
        uint8_t line[1024U]; uint32_t length = 0U;
        int result = reader_line(reader, line, sizeof(line), &length, &framing);
        if (result != 0) return result;
        uint32_t amount = 0U, digits = 0U;
        while (digits < length) {
            uint8_t ch = line[digits];
            uint32_t digit = ch >= '0' && ch <= '9' ? ch - '0' :
                ch >= 'a' && ch <= 'f' ? ch - 'a' + 10U :
                ch >= 'A' && ch <= 'F' ? ch - 'A' + 10U : 16U;
            if (digit == 16U) break;
            if (amount > (UINT32_MAX - digit) / 16U) return -90;
            amount = amount * 16U + digit; ++digits;
        }
        if (!digits || chunk_extensions(line, digits, length) != 0) return -84;
        if (!amount) {
            uint32_t trailer_bytes = 0U;
            for (uint32_t trailers = 0U; trailers < 128U; ++trailers) {
                result = reader_line(reader, line, sizeof(line), &length, &framing);
                if (result != 0) return result;
                trailer_bytes += length + 2U;
                if (trailer_bytes > REIST_CURL_HEADER_CAPACITY) return -90;
                if (!length) return reader->position == reader->used ? 0 : -84;
                /* Ignore extension trailers, but never let trailers redefine
                 * framing, representation metadata, or redirect authority. */
                uint32_t colon = 0U;
                while (colon < length && line[colon] != ':') ++colon;
                if (!colon || colon == length) return -84;
                for (uint32_t i = 0; i < colon; ++i) if (!chunk_token(line[i])) return -84;
                static const char *const forbidden[] = {"content-length", "transfer-encoding", "location", "content-type", "content-encoding"};
                for (uint32_t f = 0U; f < sizeof(forbidden) / sizeof(forbidden[0]); ++f) {
                    uint32_t i = 0U;
                    while (i < colon && forbidden[f][i]) {
                        uint8_t ch = line[i]; if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                        if (ch != (uint8_t)forbidden[f][i]) break;
                        ++i;
                    }
                    if (i == colon && !forbidden[f][i]) return -84;
                }
            }
            return -90;
        }
        if (amount > maximum_bytes - total) return -90;
        result = reader_write(reader, output, amount);
        if (result != 0) return result;
        total += amount;
        uint8_t cr = 0, lf = 0;
        result = reader_byte(reader, &cr);
        if (result == 0) result = reader_byte(reader, &lf);
        if (result != 0) return result;
        if (cr != '\r' || lf != '\n') return -84;
        framing += 2U; if (framing > 65536U) return -90;
    }
    return -90;
}

static int receive_response_inner(const curl_stream_t *stream, int output,
                            uint32_t maximum_bytes, uint32_t include_headers) {
    static uint8_t header[REIST_CURL_HEADER_CAPACITY];
    uint32_t header_used = 0U, body_offset = 0U;
    static reist_curl_response_head_t response;
    uint64_t started = 0U;
    if (x86os_monotonic_ms(&started) != 0) return -5;
    uint32_t header_total = 0U;
    for (uint32_t informational = 0U;; ++informational) {
        if (informational > 4U) return -90;
        int status = reist_curl_find_header_end(header, header_used, &body_offset);
        while (status == -11) {
            uint64_t now = 0U;
            if (x86os_monotonic_ms(&now) != 0 || now < started) return -5;
            if (now - started >= CURL_TRANSFER_IDLE_TIMEOUT_MS) return -110;
            int received = stream->receive(stream->stream, header + header_used, sizeof(header) - header_used);
            if (received <= 0) return received == 0 ? -84 : received;
            if ((uint32_t)received > sizeof(header) - header_used) return -84;
            header_used += (uint32_t)received;
            status = reist_curl_find_header_end(header, header_used, &body_offset);
        }
        if (status != 0) return status;
        status = reist_curl_parse_response_head(header, body_offset, &response);
        if (status != 0 || response.transfer_encoding_unsupported) return -84;
        header_total += body_offset;
        if (header_total > sizeof(header)) return -90;
        if (include_headers && write_all(output, header, body_offset) != 0) return -5;
        if (response.status >= 200U) break;
        if (response.status == 101U || response.content_length_present || response.chunked) return -84;
        header_used -= body_offset;
        for (uint32_t i = 0; i < header_used; ++i) header[i] = header[i + body_offset];
    }
    if (response.status == 204U && (response.content_length_present || response.chunked)) return -84;
    if (response.status == 204U || response.status == 304U)
        return header_used == body_offset ? 0 : -84;
    if (response.content_length_present &&
        response.content_length > maximum_bytes) return -90;
    curl_body_reader_t reader = {0};
    reader.stream = stream; reader.bytes = header + body_offset;
    reader.used = header_used - body_offset; reader.started = reader.progressed = started;
    if (response.chunked) return receive_chunked(&reader, output, maximum_bytes);
    uint32_t total = 0U;
    for (;;) {
        if (response.content_length_present && total == response.content_length)
            return reader.position == reader.used ? 0 : -84;
        int status = reader_available(&reader);
        if (status == 0) return response.content_length_present ? -84 : 0;
        if (status < 0) return status;
        uint32_t amount = reader.used - reader.position;
        if (amount > maximum_bytes - total) return -90;
        if (response.content_length_present && amount > response.content_length - total) return -84;
        status = reader_write(&reader, output, amount);
        if (status != 0) return status;
        total += amount;
    }
}
static int receive_response(const curl_stream_t *stream,int output,
                             uint32_t maximum_bytes,uint32_t include_headers) {
    file_buffer.used=0; file_buffer.failed=0; file_buffer.descriptor=output;
    file_buffer.enabled=output!=X86OS_STDOUT_FILENO;
    int result=receive_response_inner(stream,output,maximum_bytes,include_headers);
    if(!result && file_buffer.enabled) result=flush_file_buffer();
    file_buffer.used=file_buffer.enabled=0;
    return result;
}
static int receive_body(const curl_stream_t *stream, int output, uint32_t maximum_bytes) {
    return receive_response(stream, output, maximum_bytes, 0U);
}

int main(int argc, char **argv) {
    if (argc == 2 && argv != 0 && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: curl [-i|--include] [-o file] [--max-bytes n] "
                   "http[s]://host/path\n"
                   "HTTPS verifies the CA chain, RTC and exact host name.\n");
        return 0;
    }
    curl_options_t options;
    if (parse_options(argc, argv, &options) != 0) {
        x86os_puts("curl: invalid arguments (try curl --help)\n");
        return 2;
    }
    static reist_curl_url_t url;
    int result = reist_curl_parse_http_url(options.url, &url);
    if (result != 0) {
        x86os_puts("curl: invalid URL\n");
        return 2;
    }
    static char request[CURL_REQUEST_CAPACITY]; uint32_t request_length = 0U;
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
            return 35;
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
            return 6;
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
    if(result==0 && options.ipc_endpoint) {
        ipc_output.capacity=options.maximum_bytes+REIST_CURL_HEADER_CAPACITY;
        ipc_output.used=0; ipc_output.bytes=x86os_malloc(ipc_output.capacity);
        if(!ipc_output.bytes) { failure_stage=CURL_STAGE_OUTPUT; result=-12; }
    }
    if (result == 0) {
        failure_stage = CURL_STAGE_RESPONSE;
        result = options.include_headers
            ? receive_response(&stream, output, options.maximum_bytes, 1U)
            : receive_body(&stream, output, options.maximum_bytes);
        if(file_buffer.failed) failure_stage=CURL_STAGE_OUTPUT;
    }
    if(!result && options.ipc_endpoint) {
        failure_stage=CURL_STAGE_OUTPUT;
        result=publish_ipc_output(options.ipc_endpoint);
    }
    if(ipc_output.bytes) { x86os_free(ipc_output.bytes); ipc_output.bytes=0; ipc_output.used=ipc_output.capacity=0; }
    if (tls_open) (void)reist_tls_client_close(&tls_context);
    if (socket != 0U) (void)x86os_tcp_socket_close(socket, 2000U);

    if (options.output != 0 && output >= 0 && output != X86OS_STDOUT_FILENO) {
        if (result == 0) failure_stage = CURL_STAGE_OUTPUT;
        result = finish_output(temporary, options, output, result);
    }
    if (result != 0) {
        if (result == -90)
            x86os_puts("curl: response exceeds byte limit\n");
        else if (result == -110)
            print_timeout(failure_stage);
        else
            print_failure(failure_stage, result);
        return curl_exit_status(failure_stage,result);
    }
    return 0;
}
