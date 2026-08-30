#include "reist/tls.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET host_socket_t;
#define HOST_INVALID_SOCKET INVALID_SOCKET
#define host_socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int host_socket_t;
#define HOST_INVALID_SOCKET (-1)
#define host_socket_close close
#endif

typedef struct host_transport {
    host_socket_t socket;
    int tamper_next_receive;
} host_transport_t;

static uint32_t host_live_allocations;
static uint32_t host_peak_allocations;

static int host_time(void *opaque, int64_t *seconds) {
    (void)opaque;
    if (seconds == 0) return -22;
    *seconds = INT64_C(1798761600); /* 2027-01-01T00:00:00Z */
    return 0;
}

static int host_monotonic(void *opaque, uint64_t *milliseconds) {
    (void)opaque;
    if (milliseconds == 0) return -22;
#if defined(_WIN32)
    *milliseconds = (uint64_t)GetTickCount64();
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return -5;
    *milliseconds = (uint64_t)value.tv_sec * 1000U +
                    (uint64_t)value.tv_nsec / 1000000U;
#endif
    return 0;
}

/* The production defaults remain linked into the host proof even though its
 * callback table deliberately substitutes host implementations. */
void *x86os_malloc(size_t size) { return malloc(size); }
void x86os_free(void *pointer) { free(pointer); }
uint32_t x86os_get_date(void) { return (2027U << 16U) | (1U << 8U) | 1U; }
uint32_t x86os_get_time(void) { return 0U; }
int x86os_monotonic_ms(uint64_t *milliseconds) {
    return host_monotonic(0, milliseconds);
}

static void *host_allocate(void *opaque, uint32_t count, uint32_t size) {
    (void)opaque;
    void *pointer = calloc(count, size);
    if (pointer != 0) {
        ++host_live_allocations;
        if (host_live_allocations > host_peak_allocations)
            host_peak_allocations = host_live_allocations;
    }
    return pointer;
}

static void host_free(void *opaque, void *pointer) {
    (void)opaque;
    if (pointer != 0 && host_live_allocations != 0U)
        --host_live_allocations;
    free(pointer);
}

static void set_socket_timeout(host_socket_t socket, uint32_t timeout_ms) {
#if defined(_WIN32)
    DWORD timeout = timeout_ms;
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                     (const char *)&timeout, sizeof(timeout));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                     (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout = {
        (time_t)(timeout_ms / 1000U),
        (suseconds_t)((timeout_ms % 1000U) * 1000U)};
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     sizeof(timeout));
#endif
}

static int host_send(void *opaque, const uint8_t *data, uint32_t length,
                     uint32_t timeout_ms) {
    host_transport_t *transport = (host_transport_t *)opaque;
    set_socket_timeout(transport->socket, timeout_ms);
    int result = send(transport->socket, (const char *)data, (int)length, 0);
    return result <= 0 ? -5 : result;
}

static int host_receive(void *opaque, uint8_t *data, uint32_t capacity,
                        uint32_t timeout_ms) {
    host_transport_t *transport = (host_transport_t *)opaque;
    set_socket_timeout(transport->socket, timeout_ms);
    int result = recv(transport->socket, (char *)data, (int)capacity, 0);
    if (result > 0 && transport->tamper_next_receive) {
        data[result - 1] ^= 0x01U;
        transport->tamper_next_receive = 0;
    }
    return result < 0 ? -5 : result;
}

static uint8_t *read_trust_store(const char *path, uint32_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (file == 0 || fseek(file, 0, SEEK_END) != 0) return 0;
    long length = ftell(file);
    if (length <= 0 || length >= (long)REIST_TLS_MAX_TRUST_STORE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)length + 1U);
    if (data == 0 || fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    data[length] = 0U;
    *size_out = (uint32_t)length + 1U;
    return data;
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    uint32_t port = (uint32_t)strtoul(argv[1], 0, 10);
    int expect_success = strcmp(argv[2], "success") == 0;
    int expect_record_failure = strcmp(argv[2], "record-failure") == 0;
    int expect_handshake_failure = strcmp(argv[2], "failure") == 0;
    int use_local_ca = strcmp(argv[3], "local-ca") == 0;
    uint32_t trust_size = 0U;
    uint8_t *trust = use_local_ca ? read_trust_store(argv[4], &trust_size) : 0;
    if (port == 0U || port > 65535U || (use_local_ca && trust == 0)) return 2;

#if defined(_WIN32)
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 2;
#endif
    host_socket_t socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(0x7f000001U);
    if (socket_value == HOST_INVALID_SOCKET ||
        connect(socket_value, (struct sockaddr *)&address, sizeof(address)) != 0)
        return 2;

    host_transport_t host = {socket_value, 0};
    reist_tls_platform_t platform = {
        REIST_TLS_ABI_VERSION, sizeof(platform), 0, host_time, host_monotonic,
        reist_tls_hardware_entropy, host_allocate, host_free};
    reist_tls_transport_t transport = {
        REIST_TLS_ABI_VERSION, sizeof(transport), &host, host_send, host_receive};
    reist_tls_client_options_t options = {
        REIST_TLS_ABI_VERSION, sizeof(options), "tls.reist", 5000U, 2000U,
        trust, trust_size};
    static reist_tls_context_t context;
    const char *stage = "open";
    int result = reist_tls_client_open(&context, &platform, &transport, &options);
    if ((expect_success || expect_record_failure) && result == 0) {
        static const uint8_t request[] = "ping";
        uint8_t response[4];
        stage = "write";
        result = reist_tls_client_write(&context, request, 4U);
        if (result == 4 && expect_record_failure)
            host.tamper_next_receive = 1;
        if (result == 4) {
            stage = "read";
            result = reist_tls_client_read(&context, response, 4U);
        }
        if (expect_record_failure)
            result = result < 0 ? 0 : -5;
        else if (result == 4 && memcmp(response, "pong", 4U) == 0)
            result = 0;
        else if (result >= 0)
            result = -5;
        (void)reist_tls_client_close(&context);
    } else if (expect_handshake_failure && result < 0) {
        result = 0;
    } else {
        if (result == 0) (void)reist_tls_client_close(&context);
        result = -5;
    }
    host_socket_close(socket_value);
#if defined(_WIN32)
    WSACleanup();
#endif
    free(trust);
    if (host_live_allocations != 0U || host_peak_allocations != 1U) {
        stage = "arena";
        result = -5;
    }
    if (result != 0)
        fprintf(stderr,
                "REIST TLS host proof failed: mode=%s stage=%s result=%d\n",
                argv[2], stage, result);
    return result == 0 ? 0 : 1;
}
