/**
 * @file userspace/programs/httpd.c
 * @brief Bounded HTTP/1.0 file and directory server for /htdocs.
 *
 * The Ring-3 server uses only public filesystem and TCP ABIs. Requests, file
 * transfers and directory entries all have fixed bounds. The daemon runs until
 * the foreground process is interrupted; every accept and client operation
 * still uses a finite monotonic timeout. URL traversal and encoded/ambiguous
 * paths are rejected before VFS access.
 */
#include "x86os.h"
#include "../storage/include/reist/vfs_stat_client.h"

#define HTTP_REQUEST_CAPACITY 1024U
#define HTTP_PATH_CAPACITY 256U
#define HTTP_DIRECTORY_CAPACITY 1024U
#define HTTP_DIRECTORY_MAX_ENTRIES 32U
#define HTTP_FILE_MAX_BYTES 4096U
#define HTTP_MAX_REQUESTS 32U
#define HTTP_IO_TIMEOUT_MS 5000U
#define HTTP_ACCEPT_TIMEOUT_MS 250U

static const char header_ok[] =
    "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n";
static const char response_not_found[] =
    "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n"
    "Connection: close\r\n\r\nnot found\n";
static const char response_method[] =
    "HTTP/1.0 405 Method Not Allowed\r\nContent-Type: text/plain\r\n"
    "Connection: close\r\n\r\nmethod not allowed\n";
static const char response_too_large[] =
    "HTTP/1.0 413 Content Too Large\r\nContent-Type: text/plain\r\n"
    "Connection: close\r\n\r\nfile too large\n";
static const char response_bad_request[] =
    "HTTP/1.0 400 Bad Request\r\nContent-Type: text/plain\r\n"
    "Connection: close\r\n\r\nbad request\n";

static int metadata_client_reported;

typedef struct {
    char uri[HTTP_PATH_CAPACITY];
    int head;
} http_target_t;

static uint32_t text_length(const char *text) {
    uint32_t length = 0U;
    while (text[length] != '\0' && length < HTTP_FILE_MAX_BYTES) ++length;
    return length;
}

static int parse_number(const char *text, uint32_t maximum, uint32_t *out) {
    uint32_t value = 0U;
    if (text == 0 || out == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (digit > 9U || value > (maximum - digit) / 10U) return -1;
        value = value * 10U + digit;
    }
    if (value == 0U || value > maximum) return -1;
    *out = value; return 0;
}

static int starts_with(const uint8_t *data, uint32_t length,
                       const char *prefix) {
    uint32_t index = 0U;
    while (prefix[index] != '\0') {
        if (index >= length || data[index] != (uint8_t)prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static int request_complete(const uint8_t *data, uint32_t length) {
    if (length < 4U) return 0;
    for (uint32_t index = 3U; index < length; ++index)
        if (data[index - 3U] == '\r' && data[index - 2U] == '\n' &&
            data[index - 1U] == '\r' && data[index] == '\n') return 1;
    return 0;
}

/* Parse only origin-form GET/HEAD targets. Encodings and traversal syntax are
 * rejected rather than normalized, keeping path authorization unambiguous. */
static int parse_target(const uint8_t *request, uint32_t length,
                        http_target_t *target) {
    uint32_t method_length;
    target->head = 0;
    if (starts_with(request, length, "GET ")) method_length = 4U;
    else if (starts_with(request, length, "HEAD ")) {
        method_length = 5U; target->head = 1;
    } else return -405;
    uint32_t end = method_length;
    while (end < length && request[end] != ' ' && request[end] != '\r' &&
           request[end] != '\n') ++end;
    uint32_t uri_length = end - method_length;
    if (uri_length == 0U || uri_length >= sizeof(target->uri) - 8U ||
        request[method_length] != '/' || end + 11U > length ||
        request[end] != ' ' ||
        !(starts_with(request + end + 1U, length - end - 1U,
                      "HTTP/1.0\r\n") ||
          starts_with(request + end + 1U, length - end - 1U,
                      "HTTP/1.1\r\n"))) return -400;
    for (uint32_t index = 0U; index < uri_length; ++index) {
        uint8_t value = request[method_length + index];
        if (value < 0x20U || value >= 0x7fU || value == '\\' || value == '%' ||
            value == '?' || value == '#') return -400;
        if (value == '.' && index + 1U < uri_length &&
            request[method_length + index + 1U] == '.' &&
            (index == 0U || request[method_length + index - 1U] == '/') &&
            (index + 2U == uri_length ||
             request[method_length + index + 2U] == '/')) return -400;
        target->uri[index] = (char)value;
    }
    target->uri[uri_length] = '\0'; return 0;
}

static int send_bytes(x86os_tcp_socket_t socket, const uint8_t *data,
                      uint32_t length) {
    uint32_t sent = 0U;
    while (sent < length) {
        uint32_t amount = length - sent;
        if (amount > X86OS_TCP_MAX_SEGMENT) amount = X86OS_TCP_MAX_SEGMENT;
        x86os_tcp_io_t request = {
            .version = X86OS_TCP_SOCKET_VERSION,
            .struct_size = sizeof(request), .socket = socket,
            .length = amount, .timeout_ms = HTTP_IO_TIMEOUT_MS,
        };
        int result = x86os_tcp_send(&request, data + sent);
        if (result != (int)amount) return result < 0 ? result : -5;
        sent += amount;
    }
    return 0;
}

static int send_text(x86os_tcp_socket_t socket, const char *text) {
    uint32_t length = text_length(text);
    return length == 0U || length >= HTTP_FILE_MAX_BYTES
        ? -90 : send_bytes(socket, (const uint8_t *)text, length);
}

/* Map every accepted URI beneath the fixed document root; parse_target has
 * already rejected syntax capable of escaping this prefix. */
static int map_path(const char *uri, char *path) {
    static const char root[] = "/htdocs";
    uint32_t used = 0U;
    while (root[used] != '\0') { path[used] = root[used]; ++used; }
    if (uri[0] == '/' && uri[1] != '\0') {
        for (uint32_t index = 0U; uri[index] != '\0'; ++index) {
            if (used + 1U >= HTTP_PATH_CAPACITY) return -1;
            path[used++] = uri[index];
        }
    }
    path[used] = '\0'; return 0;
}

static int append_char(uint8_t *buffer, uint32_t *used, char value) {
    if (*used >= HTTP_DIRECTORY_CAPACITY) return -1;
    buffer[(*used)++] = (uint8_t)value; return 0;
}

static int append_text(uint8_t *buffer, uint32_t *used, const char *text) {
    for (uint32_t index = 0U; text[index] != '\0'; ++index)
        if (append_char(buffer, used, text[index]) != 0) return -1;
    return 0;
}

/* Build the complete listing in fixed storage so neither VFS enumeration nor
 * response generation can grow with an attacker-controlled directory. */
static int serve_directory(x86os_tcp_socket_t socket, const char *path,
                           const http_target_t *target) {
    uint8_t body[HTTP_DIRECTORY_CAPACITY]; uint32_t used = 0U;
    if (append_text(body, &used, "Index of ") != 0 ||
        append_text(body, &used, target->uri) != 0 ||
        append_text(body, &used, "\n\n") != 0) return -90;
    uint32_t index = 0U, emitted = 0U; int truncated = 0;
    while (emitted < HTTP_DIRECTORY_MAX_ENTRIES) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, index, entries);
        if (count < 0) return count;
        if (count == 0) break;
        for (int item = 0; item < count; ++item) {
            if (emitted >= HTTP_DIRECTORY_MAX_ENTRIES ||
                append_text(body, &used, entries[item].name) != 0 ||
                (entries[item].type == X86OS_DIRECTORY &&
                 append_char(body, &used, '/') != 0) ||
                append_char(body, &used, '\n') != 0) {
                truncated = 1; break;
            }
            ++emitted;
        }
        if (truncated) break;
        index += (uint32_t)count;
    }
    if (truncated && used + 16U < sizeof(body))
        (void)append_text(body, &used, "[truncated]\n");
    int result = send_text(socket, header_ok);
    if (result == 0 && !target->head) result = send_bytes(socket, body, used);
    return result;
}

static int serve_file(x86os_tcp_socket_t socket, const char *path,
                      const x86os_file_info_t *info, int head) {
    if (info->size > HTTP_FILE_MAX_BYTES) return send_text(
        socket, response_too_large);
    int result = send_text(socket, header_ok);
    if (result != 0 || head) return result;
    int descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    uint8_t buffer[X86OS_TCP_MAX_SEGMENT]; uint32_t total = 0U;
    while (result == 0 && total < info->size) {
        uint32_t amount = info->size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        int received = x86os_read(descriptor, buffer, amount);
        if (received <= 0) { result = received == 0 ? -5 : received; break; }
        result = send_bytes(socket, buffer, (uint32_t)received);
        total += (uint32_t)received;
    }
    if (x86os_close(descriptor) < 0 && result == 0) result = -5;
    return result;
}

static int dispatch_request(x86os_tcp_socket_t socket, const uint8_t *request,
                            uint32_t length) {
    http_target_t target; int parsed = parse_target(request, length, &target);
    if (parsed == -405) return send_text(socket, response_method);
    if (parsed != 0) return send_text(socket, response_bad_request);
    char path[HTTP_PATH_CAPACITY];
    if (map_path(target.uri, path) != 0)
        return send_text(socket, response_bad_request);
    x86os_file_info_t info;
    if (reist_vfs_stat(path, &info,
                       REIST_VFS_STAT_DEFAULT_TIMEOUT_MS) < 0)
        return send_text(socket, response_not_found);
    if (!metadata_client_reported) {
        metadata_client_reported = 1;
        x86os_puts("HTTPD_VFS_STAT_CLIENT_OK\n");
    }
    return info.type == X86OS_DIRECTORY
        ? serve_directory(socket, path, &target)
        : serve_file(socket, path, &info, target.head);
}

/* A client may contribute at most four bounded TCP segments and 1024 request
 * bytes before receiving a deterministic bad-request response. */
static int serve_client(x86os_tcp_socket_t socket) {
    uint8_t request[HTTP_REQUEST_CAPACITY]; uint32_t used = 0U;
    for (uint32_t count = 0U; count < 4U && used < sizeof(request) &&
         !request_complete(request, used); ++count) {
        uint32_t remaining = (uint32_t)sizeof(request) - used;
        if (remaining > X86OS_TCP_MAX_SEGMENT) remaining = X86OS_TCP_MAX_SEGMENT;
        x86os_tcp_io_t receive = {
            .version = X86OS_TCP_SOCKET_VERSION,
            .struct_size = sizeof(receive), .socket = socket,
            .length = remaining, .timeout_ms = HTTP_IO_TIMEOUT_MS,
        };
        int result = x86os_tcp_receive(&receive, request + used);
        if (result <= 0) return result == 0 ? -71 : result;
        used += receive.length;
    }
    return request_complete(request, used)
        ? dispatch_request(socket, request, used)
        : send_text(socket, response_bad_request);
}

int main(int argc, char **argv) {
    /* Zero is the internal sentinel for normal daemon operation. A positive
     * request limit remains available for deterministic test runs. */
    uint32_t port_value = 8080U, requests = 0U;
    if (argc > 3 || (argc >= 2 &&
        parse_number(argv[1], 65535U, &port_value) != 0) ||
        (argc == 3 && parse_number(argv[2], HTTP_MAX_REQUESTS,
                                   &requests) != 0)) {
        x86os_puts("usage: httpd [port] [requests]\n"); return 2;
    }
    x86os_tcp_socket_t listener = 0U;
    int result = x86os_tcp_socket_open(&listener);
    x86os_tcp_listen_t listen = {
        .version = X86OS_TCP_SOCKET_VERSION, .struct_size = sizeof(listen),
        .socket = listener, .port = (uint16_t)port_value,
        .backlog = X86OS_TCP_MAX_BACKLOG,
    };
    if (result == 0) result = x86os_tcp_listen(&listen);
    if (result != 0) {
        if (listener != 0U) (void)x86os_tcp_socket_close(listener, 0U);
        x86os_puts("httpd: listen failed\n"); return 1;
    }
    x86os_puts("httpd: listening\n");
    uint32_t served = 0U;
    while (requests == 0U || served < requests) {
        x86os_tcp_accept_t accept = {
            .version = X86OS_TCP_SOCKET_VERSION,
            .struct_size = sizeof(accept), .listener = listener,
            .timeout_ms = HTTP_ACCEPT_TIMEOUT_MS,
        };
        result = x86os_tcp_accept(&accept);
        /* An idle listener is healthy. Re-enter accept so httpd remains the
         * foreground service until the shell delivers Ctrl+C. */
        if (result == -110) {
            if (requests == 0U && x86os_getchar_nonblocking() == 0x03) {
                x86os_puts("^C\n");
                result = 0;
                break;
            }
            continue;
        }
        if (result != 0) break;
        int client_result = serve_client(accept.socket);
        (void)x86os_tcp_socket_close(accept.socket, 2000U);
        /* A malformed, reset or timed-out client owns only its accepted
         * socket. Keep the listener available for the next connection. */
        if (client_result == 0) ++served;
        if (requests == 0U && x86os_getchar_nonblocking() == 0x03) {
            x86os_puts("^C\n");
            break;
        }
    }
    (void)x86os_tcp_socket_close(listener, 0U);
    if (result != 0) {
        x86os_puts("httpd: accept failed\n");
        return 1;
    }
    if (requests != 0U) x86os_puts("httpd: served\n");
    return 0;
}
