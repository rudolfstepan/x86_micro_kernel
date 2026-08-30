#include "reist/tls.h"
#include "reist_tls_trust_anchors.h"

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define REIST_TLS_MAGIC 0x31534c54U
#define REIST_TLS_MAX_HANDSHAKE_MS 30000U
#define REIST_TLS_MAX_IO_MS 5000U

typedef struct reist_tls_private {
    uint32_t magic;
    uint8_t bound;
    uint8_t psa_initialized;
    uint8_t initialized;
    uint8_t reserved;
    reist_tls_platform_t platform;
    reist_tls_transport_t transport;
    uint32_t io_timeout_ms;
    uint64_t operation_deadline;
    uint64_t last_monotonic;
    char server_name[REIST_TLS_MAX_HOST_BYTES + 1U];
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt trust_roots;
} reist_tls_private_t;

_Static_assert(sizeof(reist_tls_private_t) <= REIST_TLS_CONTEXT_BYTES,
               "REIST TLS context is too small");

int reist_tls_platform_bind(const reist_tls_platform_t *platform);
void reist_tls_platform_unbind(const reist_tls_platform_t *platform);
void mbedtls_psa_crypto_free(void);

static reist_tls_private_t *private_context(reist_tls_context_t *context) {
    return (reist_tls_private_t *)(void *)context->bytes;
}

static int valid_server_name(const char *name, size_t *length_out) {
    if (name == 0 || name[0] == '\0') return 0;
    size_t length = 0U, label = 0U;
    int previous_hyphen = 0;
    while (name[length] != '\0') {
        uint8_t value = (uint8_t)name[length];
        int alphanumeric = (value >= 'a' && value <= 'z') ||
                           (value >= 'A' && value <= 'Z') ||
                           (value >= '0' && value <= '9');
        if (length >= REIST_TLS_MAX_HOST_BYTES ||
            (!alphanumeric && value != '-' && value != '.')) return 0;
        if (value == '.') {
            if (label == 0U || label > 63U || previous_hyphen) return 0;
            label = 0U;
            previous_hyphen = 0;
        } else {
            if (label == 0U && value == '-') return 0;
            ++label;
            previous_hyphen = value == '-';
        }
        ++length;
    }
    if (label == 0U || label > 63U || previous_hyphen) return 0;
    *length_out = length;
    return 1;
}

static int monotonic_now(reist_tls_private_t *state, uint64_t *now) {
    uint64_t value = 0U;
    if (state->platform.monotonic(state->platform.platform, &value) != 0 ||
        value < state->last_monotonic) return -5;
    state->last_monotonic = value;
    *now = value;
    return 0;
}

static int begin_deadline(reist_tls_private_t *state, uint32_t timeout_ms) {
    uint64_t now = 0U;
    if (monotonic_now(state, &now) != 0 || UINT64_MAX - now < timeout_ms)
        return -5;
    state->operation_deadline = now + timeout_ms;
    return 0;
}

static int remaining_timeout(reist_tls_private_t *state, uint32_t requested,
                             uint32_t *timeout) {
    uint64_t now = 0U;
    if (monotonic_now(state, &now) != 0) return -5;
    if (now >= state->operation_deadline) return -110;
    uint64_t remaining = state->operation_deadline - now;
    uint32_t bounded = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
    if (bounded > state->io_timeout_ms) bounded = state->io_timeout_ms;
    if (requested != 0U && bounded > requested) bounded = requested;
    if (bounded == 0U) return -110;
    *timeout = bounded;
    return 0;
}

static int transport_send(void *opaque, const unsigned char *data,
                          size_t length) {
    reist_tls_private_t *state = (reist_tls_private_t *)opaque;
    uint32_t timeout = 0U;
    if (length == 0U) return 0;
    if (length > UINT32_MAX || remaining_timeout(state, 0U, &timeout) != 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int result = state->transport.send(state->transport.transport, data,
                                       (uint32_t)length, timeout);
    if (result == -110) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (result <= 0 || (size_t)result > length)
        return MBEDTLS_ERR_NET_SEND_FAILED;
    return result;
}

static int transport_receive_timeout(void *opaque, unsigned char *data,
                                     size_t capacity, uint32_t timeout_ms) {
    reist_tls_private_t *state = (reist_tls_private_t *)opaque;
    uint32_t timeout = 0U;
    if (capacity == 0U) return 0;
    if (capacity > UINT32_MAX ||
        remaining_timeout(state, timeout_ms, &timeout) != 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int result = state->transport.receive(state->transport.transport, data,
                                          (uint32_t)capacity, timeout);
    if (result == -110) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (result < 0 || (size_t)result > capacity)
        return MBEDTLS_ERR_NET_RECV_FAILED;
    return result;
}

static int map_mbed_error(int result) {
    if (result == MBEDTLS_ERR_SSL_TIMEOUT) return -110;
    if (result == MBEDTLS_ERR_SSL_WANT_READ ||
        result == MBEDTLS_ERR_SSL_WANT_WRITE) return -11;
    return -5;
}

static int retry_permitted(reist_tls_private_t *state) {
    uint32_t remaining = 0U;
    return remaining_timeout(state, 0U, &remaining) == 0;
}

static void release_context(reist_tls_private_t *state) {
    if (state->initialized) {
        mbedtls_ssl_free(&state->ssl);
        mbedtls_ssl_config_free(&state->config);
        mbedtls_x509_crt_free(&state->trust_roots);
    }
    if (state->psa_initialized) mbedtls_psa_crypto_free();
    if (state->bound) reist_tls_platform_unbind(&state->platform);
    memset(state, 0, sizeof(*state));
}

int reist_tls_client_open(reist_tls_context_t *context,
                          const reist_tls_platform_t *platform,
                          const reist_tls_transport_t *transport,
                          const reist_tls_client_options_t *options) {
    size_t host_length = 0U;
    if (context == 0 || platform == 0 || transport == 0 || options == 0 ||
        platform->version != REIST_TLS_ABI_VERSION ||
        platform->size != sizeof(*platform) ||
        transport->version != REIST_TLS_ABI_VERSION ||
        transport->size != sizeof(*transport) ||
        options->version != REIST_TLS_ABI_VERSION ||
        options->size != sizeof(*options) || platform->time == 0 ||
        platform->monotonic == 0 || platform->entropy == 0 ||
        platform->allocate == 0 || platform->free == 0 ||
        transport->send == 0 || transport->receive == 0 ||
        options->handshake_timeout_ms == 0U ||
        options->handshake_timeout_ms > REIST_TLS_MAX_HANDSHAKE_MS ||
        options->io_timeout_ms == 0U ||
        options->io_timeout_ms > REIST_TLS_MAX_IO_MS ||
        ((options->trust_anchors_pem == 0) !=
         (options->trust_anchors_pem_size == 0U)) ||
        options->trust_anchors_pem_size > REIST_TLS_MAX_TRUST_STORE_BYTES ||
        (options->trust_anchors_pem != 0 &&
         (options->trust_anchors_pem_size < 2U ||
          options->trust_anchors_pem[
              options->trust_anchors_pem_size - 1U] != 0U)) ||
        !valid_server_name(options->server_name, &host_length)) return -22;

    reist_tls_private_t *state = private_context(context);
    memset(state, 0, sizeof(*state));
    state->platform = *platform;
    state->transport = *transport;
    state->io_timeout_ms = options->io_timeout_ms;
    memcpy(state->server_name, options->server_name, host_length + 1U);

    int result = reist_tls_platform_bind(&state->platform);
    if (result != 0) {
        memset(state, 0, sizeof(*state));
        return result;
    }
    state->bound = 1U;
    if (begin_deadline(state, options->handshake_timeout_ms) != 0) {
        release_context(state);
        return -5;
    }

    if (psa_crypto_init() != PSA_SUCCESS) {
        release_context(state);
        return -5;
    }
    state->psa_initialized = 1U;
    mbedtls_ssl_init(&state->ssl);
    mbedtls_ssl_config_init(&state->config);
    mbedtls_x509_crt_init(&state->trust_roots);
    state->initialized = 1U;

    const uint8_t *trust_anchors = options->trust_anchors_pem != 0 ?
        options->trust_anchors_pem : reist_tls_trust_anchors_pem;
    size_t trust_anchors_size = options->trust_anchors_pem != 0 ?
        options->trust_anchors_pem_size : reist_tls_trust_anchors_pem_size;
    if (mbedtls_x509_crt_parse(&state->trust_roots, trust_anchors,
                               trust_anchors_size) != 0 ||
        mbedtls_ssl_config_defaults(&state->config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        release_context(state);
        return -5;
    }

    mbedtls_ssl_conf_authmode(&state->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&state->config, &state->trust_roots, 0);
    mbedtls_ssl_conf_read_timeout(&state->config, state->io_timeout_ms);
    mbedtls_ssl_conf_min_tls_version(&state->config,
                                     MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&state->config,
                                     MBEDTLS_SSL_VERSION_TLS1_3);
    if (mbedtls_ssl_setup(&state->ssl, &state->config) != 0 ||
        mbedtls_ssl_set_hostname(&state->ssl, state->server_name) != 0) {
        release_context(state);
        return -5;
    }
    mbedtls_ssl_set_bio(&state->ssl, state, transport_send, 0,
                        transport_receive_timeout);

    do {
        result = mbedtls_ssl_handshake(&state->ssl);
        if ((result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            !retry_permitted(state)) result = MBEDTLS_ERR_SSL_TIMEOUT;
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result != 0 || mbedtls_ssl_get_verify_result(&state->ssl) != 0U) {
        int mapped = result == MBEDTLS_ERR_SSL_TIMEOUT ? -110 : -13;
        release_context(state);
        return mapped;
    }
    state->magic = REIST_TLS_MAGIC;
    return 0;
}

int reist_tls_client_write(reist_tls_context_t *context, const uint8_t *data,
                           uint32_t length) {
    if (context == 0 || data == 0 || length == 0U) return -22;
    reist_tls_private_t *state = private_context(context);
    if (state->magic != REIST_TLS_MAGIC) return -9;
    if (begin_deadline(state, state->io_timeout_ms) != 0) return -5;
    int result;
    do {
        result = mbedtls_ssl_write(&state->ssl, data, length);
        if ((result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            !retry_permitted(state)) result = MBEDTLS_ERR_SSL_TIMEOUT;
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE);
    return result >= 0 ? result : map_mbed_error(result);
}

int reist_tls_client_read(reist_tls_context_t *context, uint8_t *data,
                          uint32_t capacity) {
    if (context == 0 || data == 0 || capacity == 0U) return -22;
    reist_tls_private_t *state = private_context(context);
    if (state->magic != REIST_TLS_MAGIC) return -9;
    if (begin_deadline(state, state->io_timeout_ms) != 0) return -5;
    int result;
    do {
        result = mbedtls_ssl_read(&state->ssl, data, capacity);
        if ((result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            !retry_permitted(state)) result = MBEDTLS_ERR_SSL_TIMEOUT;
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    return result >= 0 ? result : map_mbed_error(result);
}

int reist_tls_client_close(reist_tls_context_t *context) {
    if (context == 0) return -22;
    reist_tls_private_t *state = private_context(context);
    if (state->magic != REIST_TLS_MAGIC) return -9;
    int result = 0;
    if (begin_deadline(state, state->io_timeout_ms) == 0) {
        int close_result = mbedtls_ssl_close_notify(&state->ssl);
        if (close_result != 0 && close_result != MBEDTLS_ERR_SSL_WANT_READ &&
            close_result != MBEDTLS_ERR_SSL_WANT_WRITE)
            result = map_mbed_error(close_result);
    }
    release_context(state);
    return result;
}
