#ifndef REIST_TLS_H
#define REIST_TLS_H

#include <stddef.h>
#include <stdint.h>

#define REIST_TLS_ABI_VERSION 1U
#define REIST_TLS_CONTEXT_BYTES (512U * 1024U)
#define REIST_TLS_MAX_HOST_BYTES 253U
#define REIST_TLS_MAX_RECORD_BYTES 16640U
#define REIST_TLS_DEFAULT_HANDSHAKE_MS 15000U
#define REIST_TLS_HEAP_BUDGET_BYTES (4U * 1024U * 1024U)
#define REIST_TLS_MAX_ALLOCATION_BYTES (512U * 1024U)
#define REIST_TLS_MAX_TRUST_STORE_BYTES (256U * 1024U)

typedef int (*reist_tls_send_fn)(void *transport, const uint8_t *data,
                                 uint32_t length, uint32_t timeout_ms);
typedef int (*reist_tls_receive_fn)(void *transport, uint8_t *data,
                                    uint32_t capacity, uint32_t timeout_ms);
typedef int (*reist_tls_time_fn)(void *platform, int64_t *unix_seconds);
typedef int (*reist_tls_monotonic_fn)(void *platform, uint64_t *milliseconds);
typedef int (*reist_tls_entropy_fn)(void *platform, uint8_t *output,
                                    uint32_t length);
typedef void *(*reist_tls_allocate_fn)(void *platform, uint32_t count,
                                       uint32_t size);
typedef void (*reist_tls_free_fn)(void *platform, void *pointer);

typedef struct reist_tls_platform {
    uint32_t version;
    uint32_t size;
    void *platform;
    reist_tls_time_fn time;
    reist_tls_monotonic_fn monotonic;
    reist_tls_entropy_fn entropy;
    reist_tls_allocate_fn allocate;
    reist_tls_free_fn free;
} reist_tls_platform_t;

typedef struct reist_tls_transport {
    uint32_t version;
    uint32_t size;
    void *transport;
    reist_tls_send_fn send;
    reist_tls_receive_fn receive;
} reist_tls_transport_t;

typedef union reist_tls_context {
    uint64_t alignment;
    uint8_t bytes[REIST_TLS_CONTEXT_BYTES];
} reist_tls_context_t;

typedef struct reist_tls_client_options {
    uint32_t version;
    uint32_t size;
    const char *server_name;
    uint32_t handshake_timeout_ms;
    uint32_t io_timeout_ms;
    const uint8_t *trust_anchors_pem;
    uint32_t trust_anchors_pem_size;
} reist_tls_client_options_t;

int reist_tls_client_open(reist_tls_context_t *context,
                          const reist_tls_platform_t *platform,
                          const reist_tls_transport_t *transport,
                          const reist_tls_client_options_t *options);
int reist_tls_client_write(reist_tls_context_t *context, const uint8_t *data,
                           uint32_t length);
int reist_tls_client_read(reist_tls_context_t *context, uint8_t *data,
                          uint32_t capacity);
int reist_tls_client_close(reist_tls_context_t *context);
int reist_tls_rtc_time(void *platform, int64_t *unix_seconds);
int reist_tls_monotonic_time(void *platform, uint64_t *milliseconds);
int reist_tls_hardware_entropy(void *platform, uint8_t *output,
                               uint32_t length);
void *reist_tls_heap_allocate(void *platform, uint32_t count, uint32_t size);
void reist_tls_heap_free(void *platform, void *pointer);

#endif
