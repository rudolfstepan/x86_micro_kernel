/**
 * @file userspace/sdk/reist_dns.c
 * @brief Bounded DNS A/CNAME resolver implemented over the public UDP ABI.
 */
#include "x86os.h"

#define DNS_PORT 53U
#define DNS_MAX_NAME 253U
#define DNS_CACHE_SLOTS 4U
#define DNS_POINTER_LIMIT 8U
#define DNS_TCP_CONNECT_MAX_MS 1500U
#define DNS_TCP_CLOSE_MAX_MS 1000U

typedef struct {
    uint8_t active;
    char name[DNS_MAX_NAME + 1U];
    uint32_t address;
    uint64_t expires_ms;
} dns_cache_entry_t;

static dns_cache_entry_t dns_cache[DNS_CACHE_SLOTS];
static uint32_t dns_cache_next;
static uint16_t dns_transaction;

static void zero_bytes(void *pointer, uint32_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)pointer;
    for (uint32_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static uint16_t be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}
static uint32_t be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}
static void put16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U); data[1] = (uint8_t)value;
}
static int name_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left++, b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return *left == *right;
}
static void name_copy(char *destination, const char *source) {
    uint32_t index = 0U;
    while (source[index] != '\0' && index < DNS_MAX_NAME) {
        char value = source[index];
        destination[index++] = value >= 'A' && value <= 'Z'
            ? (char)(value + ('a' - 'A')) : value;
    }
    destination[index] = '\0';
}
static int encode_name(const char *name, uint8_t *output,
                       uint32_t capacity, uint32_t *length_out) {
    uint32_t input = 0U, output_index = 0U;
    if (name == 0 || *name == '\0') return -22;
    while (name[input] != '\0') {
        uint32_t label_start = input, label_length = 0U;
        while (name[input] != '\0' && name[input] != '.') {
            ++input; ++label_length;
            if (label_length > 63U) return -22;
        }
        if (label_length == 0U || output_index + label_length + 1U >= capacity)
            return -22;
        output[output_index++] = (uint8_t)label_length;
        for (uint32_t index = 0U; index < label_length; ++index)
            output[output_index++] = (uint8_t)name[label_start + index];
        if (name[input] == '.') ++input;
    }
    if (output_index >= capacity) return -22;
    output[output_index++] = 0U; *length_out = output_index; return 0;
}

static int decode_name(const uint8_t *message, uint32_t length,
                       uint32_t offset, char output[DNS_MAX_NAME + 1U],
                       uint32_t *consumed_out) {
    uint32_t cursor = offset, output_index = 0U, consumed = 0U;
    uint32_t pointers = 0U; int jumped = 0;
    for (uint32_t labels = 0U; labels <= 127U; ++labels) {
        if (cursor >= length) return -74;
        uint8_t label = message[cursor];
        if ((label & 0xc0U) == 0xc0U) {
            if (cursor + 1U >= length || ++pointers > DNS_POINTER_LIMIT)
                return -74;
            uint32_t target = ((uint32_t)(label & 0x3fU) << 8U) |
                              message[cursor + 1U];
            if (target >= length || target == cursor) return -74;
            if (!jumped) consumed += 2U;
            cursor = target; jumped = 1; continue;
        }
        if ((label & 0xc0U) != 0U || label > 63U) return -74;
        ++cursor; if (!jumped) ++consumed;
        if (label == 0U) {
            output[output_index] = '\0'; *consumed_out = consumed; return 0;
        }
        if (cursor + label > length || output_index + label + 1U > DNS_MAX_NAME)
            return -74;
        if (output_index != 0U) output[output_index++] = '.';
        for (uint32_t index = 0U; index < label; ++index) {
            char value = (char)message[cursor + index];
            output[output_index++] = value >= 'A' && value <= 'Z'
                ? (char)(value + ('a' - 'A')) : value;
        }
        cursor += label; if (!jumped) consumed += label;
    }
    return -74;
}

int reist_dns_parse_response(const uint8_t *response, uint32_t length,
                             uint16_t transaction, const char *query,
                             uint32_t *address_out, uint32_t *ttl_out) {
    if (length < 12U || be16(response) != transaction ||
        (be16(response + 2U) & 0x8000U) == 0U ||
        (be16(response + 2U) & 0x020fU) != 0U ||
        be16(response + 4U) != 1U) return -74;
    uint32_t offset = 12U, consumed = 0U;
    char owner[DNS_MAX_NAME + 1U], canonical[DNS_MAX_NAME + 1U];
    name_copy(canonical, query);
    if (decode_name(response, length, offset, owner, &consumed) != 0)
        return -74;
    offset += consumed;
    if (offset + 4U > length || be16(response + offset) != 1U ||
        be16(response + offset + 2U) != 1U) return -74;
    offset += 4U;
    uint32_t records = (uint32_t)be16(response + 6U) +
                       (uint32_t)be16(response + 8U) +
                       (uint32_t)be16(response + 10U);
    if (records > 64U) return -74;
    for (uint32_t record = 0U; record < records; ++record) {
        if (decode_name(response, length, offset, owner, &consumed) != 0)
            return -74;
        offset += consumed;
        if (offset + 10U > length) return -74;
        uint16_t type = be16(response + offset);
        uint16_t class_code = be16(response + offset + 2U);
        uint32_t ttl = be32(response + offset + 4U);
        uint16_t data_length = be16(response + offset + 8U);
        offset += 10U;
        if (offset + data_length > length) return -74;
        if (class_code == 1U && type == 5U && name_equal(owner, canonical)) {
            char alias[DNS_MAX_NAME + 1U]; uint32_t alias_consumed = 0U;
            if (decode_name(response, length, offset, alias,
                            &alias_consumed) != 0) return -74;
            name_copy(canonical, alias);
        } else if (class_code == 1U && type == 1U && data_length == 4U &&
                   name_equal(owner, canonical)) {
            *address_out = be32(response + offset);
            *ttl_out = ttl > 3600U ? 3600U : ttl;
            return *address_out != 0U ? 0 : -74;
        }
        offset += data_length;
    }
    return -2;
}

static int deadline_remaining(uint64_t deadline, uint32_t maximum,
                              uint32_t *remaining_out) {
    uint64_t now = 0U;
    if (remaining_out == 0 || x86os_monotonic_ms(&now) != 0 ||
        now >= deadline) return -110;
    uint64_t remaining = deadline - now;
    uint32_t bounded = remaining > UINT32_MAX
        ? UINT32_MAX : (uint32_t)remaining;
    if (bounded > maximum) bounded = maximum;
    if (bounded == 0U) return -110;
    *remaining_out = bounded;
    return 0;
}

static int dns_tcp_send_all(x86os_tcp_socket_t socket, const uint8_t *data,
                            uint32_t length, uint64_t deadline) {
    uint32_t sent = 0U;
    while (sent < length) {
        uint32_t timeout = 0U;
        if (deadline_remaining(deadline, DNS_TCP_CONNECT_MAX_MS,
                               &timeout) != 0) return -110;
        uint32_t amount = length - sent;
        if (amount > X86OS_TCP_MAX_SEGMENT) amount = X86OS_TCP_MAX_SEGMENT;
        x86os_tcp_io_t io = {
            X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, amount, timeout};
        int result = x86os_tcp_send(&io, data + sent);
        if (result <= 0 || (uint32_t)result > amount)
            return result < 0 ? result : -5;
        sent += (uint32_t)result;
    }
    return 0;
}

static int dns_tcp_receive_exact(x86os_tcp_socket_t socket, uint8_t *data,
                                 uint32_t length, uint64_t deadline) {
    uint32_t received = 0U;
    while (received < length) {
        uint32_t timeout = 0U;
        if (deadline_remaining(deadline, DNS_TCP_CONNECT_MAX_MS,
                               &timeout) != 0) return -110;
        uint32_t amount = length - received;
        if (amount > X86OS_TCP_RECEIVE_CAPACITY)
            amount = X86OS_TCP_RECEIVE_CAPACITY;
        x86os_tcp_io_t io = {
            X86OS_TCP_SOCKET_VERSION, sizeof(io), socket, amount, timeout};
        int result = x86os_tcp_receive(&io, data + received);
        if (result <= 0 || (uint32_t)result > amount)
            return result < 0 ? result : -84;
        received += (uint32_t)result;
    }
    return 0;
}

static int dns_resolve_tcp(uint32_t server, const uint8_t *query,
                           uint32_t query_length, uint16_t transaction,
                           const char *name, uint64_t deadline,
                           uint32_t *address_out, uint32_t *ttl_out) {
    if (query == 0 || query_length < 12U ||
        query_length > X86OS_UDP_MAX_DATAGRAM) return -22;
    uint8_t framed[X86OS_UDP_MAX_DATAGRAM + 2U];
    put16(framed, (uint16_t)query_length);
    for (uint32_t index = 0U; index < query_length; ++index)
        framed[index + 2U] = query[index];

    x86os_tcp_socket_t socket = 0U;
    int result = x86os_tcp_socket_open(&socket);
    uint32_t timeout = 0U;
    if (result == 0 && deadline_remaining(
            deadline, DNS_TCP_CONNECT_MAX_MS, &timeout) == 0) {
        x86os_tcp_connect_t connect = {
            X86OS_TCP_SOCKET_VERSION, sizeof(connect), socket, server,
            DNS_PORT, 0U, timeout};
        result = x86os_tcp_connect(&connect);
    } else if (result == 0) {
        result = -110;
    }
    if (result == 0)
        result = dns_tcp_send_all(socket, framed, query_length + 2U,
                                  deadline);
    uint8_t size_prefix[2];
    if (result == 0)
        result = dns_tcp_receive_exact(socket, size_prefix,
                                       sizeof(size_prefix), deadline);
    uint32_t response_length = result == 0 ? be16(size_prefix) : 0U;
    if (result == 0 &&
        (response_length < 12U ||
         response_length > X86OS_UDP_MAX_DATAGRAM)) result = -90;
    uint8_t response[X86OS_UDP_MAX_DATAGRAM];
    if (result == 0)
        result = dns_tcp_receive_exact(socket, response, response_length,
                                       deadline);
    if (result == 0)
        result = reist_dns_parse_response(
            response, response_length, transaction, name,
            address_out, ttl_out);
    if (socket != 0U)
        (void)x86os_tcp_socket_close(socket, DNS_TCP_CLOSE_MAX_MS);
    return result;
}

int x86os_dns_resolve_at(const char *name, uint32_t server,
                         uint32_t timeout_ms, x86os_dns_result_t *result) {
    if (name == 0 || result == 0 || timeout_ms == 0U || timeout_ms > 10000U)
        return -22;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0) return -5;
    if (UINT64_MAX - now < timeout_ms) return -75;
    uint64_t deadline = now + timeout_ms;
    for (uint32_t slot = 0U; slot < DNS_CACHE_SLOTS; ++slot) {
        if (dns_cache[slot].active && dns_cache[slot].expires_ms > now &&
            name_equal(dns_cache[slot].name, name)) {
            zero_bytes(result, sizeof(*result));
            result->version = X86OS_DNS_RESULT_VERSION;
            result->struct_size = sizeof(*result);
            result->address = dns_cache[slot].address;
            result->from_cache = 1U;
            name_copy(result->canonical_name, dns_cache[slot].name);
            return 0;
        }
    }
    if (server == 0U) return -22;
    uint8_t query[X86OS_UDP_MAX_DATAGRAM];
    zero_bytes(query, sizeof(query));
    uint32_t encoded = 0U;
    if (encode_name(name, query + 12U, sizeof(query) - 16U, &encoded) != 0)
        return -22;
    uint16_t transaction = ++dns_transaction;
    if (transaction == 0U) transaction = ++dns_transaction;
    put16(query, transaction); put16(query + 2U, 0x0100U);
    put16(query + 4U, 1U);
    uint32_t query_length = 12U + encoded;
    put16(query + query_length, 1U); put16(query + query_length + 2U, 1U);
    query_length += 4U;
    x86os_udp_socket_t socket = 0U;
    int rc = x86os_udp_socket_open(&socket);
    uint16_t local_port = (uint16_t)(49152U +
        ((uint32_t)x86os_getpid() + transaction) % 8192U);
    if (rc == 0) rc = x86os_udp_socket_bind(socket, local_port);
    x86os_udp_datagram_t datagram;
    zero_bytes(&datagram, sizeof(datagram));
    datagram.version = X86OS_UDP_SOCKET_VERSION;
    datagram.struct_size = sizeof(datagram); datagram.socket = socket;
    datagram.ip = server; datagram.destination_port = DNS_PORT;
    uint32_t udp_timeout = timeout_ms / 2U;
    if (udp_timeout == 0U) udp_timeout = 1U;
    if (udp_timeout > 2000U) udp_timeout = 2000U;
    datagram.length = query_length; datagram.timeout_ms = udp_timeout;
    if (rc == 0) rc = x86os_udp_sendto(&datagram, query);
    if (rc >= 0) rc = 0;
    uint8_t response[X86OS_UDP_MAX_DATAGRAM];
    if (rc == 0) {
        zero_bytes(&datagram, sizeof(datagram));
        datagram.version = X86OS_UDP_SOCKET_VERSION;
        datagram.struct_size = sizeof(datagram); datagram.socket = socket;
        datagram.length = sizeof(response); datagram.timeout_ms = udp_timeout;
        rc = x86os_udp_recvfrom(&datagram, response);
    }
    uint32_t address = 0U, ttl = 0U;
    if (rc >= 0 && (datagram.ip != server ||
                    datagram.source_port != DNS_PORT)) rc = -74;
    if (rc >= 0) rc = reist_dns_parse_response(
        response, datagram.length, transaction, name, &address, &ttl);
    if (socket != 0U) (void)x86os_udp_socket_close(socket);
    if (rc != 0) {
        rc = dns_resolve_tcp(server, query, query_length, transaction, name,
                             deadline, &address, &ttl);
        if (rc != 0) return rc;
    }
    dns_cache_entry_t *entry = &dns_cache[dns_cache_next++ % DNS_CACHE_SLOTS];
    entry->active = 1U; name_copy(entry->name, name); entry->address = address;
    entry->expires_ms = now + (uint64_t)(ttl == 0U ? 1U : ttl) * 1000U;
    zero_bytes(result, sizeof(*result));
    result->version = X86OS_DNS_RESULT_VERSION;
    result->struct_size = sizeof(*result);
    result->address = address; result->ttl_seconds = ttl;
    name_copy(result->canonical_name, name);
    return 0;
}

int x86os_dns_resolve(const char *name, uint32_t timeout_ms,
                      x86os_dns_result_t *result) {
    x86os_network_control_request_t request;
    x86os_network_control_result_t status;
    zero_bytes(&request, sizeof(request));
    request.version = X86OS_NETWORK_CONTROL_VERSION;
    request.struct_size = sizeof(request);
    request.operation = X86OS_NETWORK_STATUS;
    if (x86os_network_control(&request, &status) != 0 ||
        !status.configured || status.dns_server == 0U) return -19;
    return x86os_dns_resolve_at(name, status.dns_server, timeout_ms, result);
}
