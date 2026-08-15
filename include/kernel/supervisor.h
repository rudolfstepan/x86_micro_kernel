#ifndef KERNEL_SUPERVISOR_H
#define KERNEL_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>
#include "include/kernel/critical_object.h"

struct Process;

#define SUPERVISOR_MAX_DOMAINS 8U
#define SUPERVISOR_NAME_CAPACITY 16U
#define SUPERVISOR_STATE_VERSION 1U
#define SUPERVISOR_FENCE_OPS_VERSION 1U
#define SUPERVISOR_DESCRIPTOR_VERSION 1U
#define SUPERVISOR_NETWORK_DEGRADATION_VERSION 1U
#define SUPERVISOR_PROBE_AUTHORITY_VERSION 1U
#define SUPERVISOR_NETWORK_CONTEXT_VERSION 1U
#define SUPERVISOR_ARP_REPLY_CONTEXT_VERSION 1U
#define SUPERVISOR_ARP_RESOLUTION_CONTEXT_VERSION 1U
#define SUPERVISOR_ICMP_ECHO_CONTEXT_VERSION 1U
#define SUPERVISOR_DHCP_CONTEXT_VERSION 1U
#define SUPERVISOR_UDP_ECHO_CONTEXT_VERSION 1U
#define SUPERVISOR_PROBE_CONTROL_VERSION 1U
#define SUPERVISOR_EINTEGRITY (-84)
#define REIST_REPORT_SELF_TEST 1U
#define REIST_REPORT_PROGRESS 2U
#define REIST_REPORT_INVALID 3U
#define REIST_REPORT_NETWORK_HEADER 4U
#define REIST_REPORT_NETWORK_PROBE_ID 5U
#define REIST_REPORT_NETWORK_DEGRADED 6U
#define REIST_SERVICE_DIAGNOSTIC 1U

typedef enum {
    SUPERVISOR_STARTING = 1,
    SUPERVISOR_HEALTHY = 2,
    SUPERVISOR_DEGRADED = 3,
    SUPERVISOR_RECOVERING = 4,
    SUPERVISOR_FAILED = 5,
    SUPERVISOR_ISOLATED = 6,
    SUPERVISOR_SAFE_STATE = 7,
    SUPERVISOR_FENCING = 8,
    SUPERVISOR_IDLE = 9,
} supervisor_health_state_t;

typedef enum {
    SUPERVISOR_EVENT_NONE = 0,
    SUPERVISOR_EVENT_FENCE_REQUIRED = 1,
    SUPERVISOR_EVENT_RESTART_REQUIRED = 2,
    SUPERVISOR_EVENT_SAFE_STATE_REQUIRED = 3,
} supervisor_event_type_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
    uint32_t epoch;
} supervisor_handle_t;

typedef struct {
    uint32_t active;
    uint32_t fenced;
    uint32_t healthy;
    supervisor_handle_t handle;
    int32_t pid;
    uint32_t process_generation;
    uint32_t launch_count;
    uint32_t endpoint_handle;
    uint32_t network_epoch;
    uint64_t last_network_probe_ms;
} supervisor_probe_control_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_probe_control_t;

typedef struct {
    uint32_t heartbeat_timeout_ms;
    uint32_t recovery_timeout_ms;
    uint32_t restart_budget;
} supervisor_config_t;

typedef bool (*supervisor_fence_fn_t)(void *context);

typedef struct {
    supervisor_fence_fn_t apply;
    supervisor_fence_fn_t verify;
    void *context;
} supervisor_fence_ops_t;

typedef struct {
    supervisor_event_type_t type;
    supervisor_handle_t handle;
} supervisor_event_t;

typedef struct {
    uint64_t next_id;
    uint64_t deadline_ms;
    uint32_t active_id;
    uint32_t transaction_epoch;
} supervisor_probe_authority_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_probe_authority_t;

typedef struct {
    uint32_t delivered_id;
    uint32_t transaction_epoch;
    uint32_t gateway;
    uint32_t local_ip;
    uint8_t local_mac[6];
    uint8_t reserved[2];
    uint32_t candidate_ip;
    uint8_t candidate_mac[6];
    uint8_t candidate_reserved[2];
} supervisor_network_probe_context_t;

#define SUPERVISOR_ARP_BINDING_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t probe_id;
    uint32_t ip;
    uint8_t mac[6];
    uint8_t reserved[2];
} supervisor_arp_binding_t;

#define SUPERVISOR_ARP_REPLY_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t target_ip;
    uint8_t target_mac[6];
    uint8_t reserved[2];
} supervisor_arp_reply_t;

typedef struct {
    uint32_t request_id;
    uint32_t transaction_epoch;
    uint32_t target_ip;
    uint8_t target_mac[6];
    uint8_t reserved[2];
} supervisor_arp_reply_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_arp_reply_context_t;

#define SUPERVISOR_ARP_RESOLUTION_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t target_ip;
} supervisor_arp_resolution_t;

typedef struct {
    uint32_t request_id;
    uint32_t transaction_epoch;
    uint32_t target_ip;
    uint32_t reserved;
} supervisor_arp_resolution_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_arp_resolution_context_t;

#define SUPERVISOR_ICMP_ECHO_MAX_DATA 32U
#define SUPERVISOR_ICMP_ECHO_REPLY_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} supervisor_icmp_echo_reply_t;

typedef struct {
    uint32_t request_id;
    uint32_t transaction_epoch;
    uint32_t source_ip;
    uint16_t identifier;
    uint16_t sequence;
    uint16_t data_length;
    uint16_t reserved;
    uint8_t source_mac[6];
    uint8_t data[SUPERVISOR_ICMP_ECHO_MAX_DATA];
    uint8_t reserved_tail[2];
} supervisor_icmp_echo_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_icmp_echo_context_t;

#define SUPERVISOR_DHCP_COMMIT_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} supervisor_dhcp_commit_t;

typedef struct {
    uint32_t request_id;
    uint32_t transaction_epoch;
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t reserved[2];
} supervisor_dhcp_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_dhcp_context_t;

#define SUPERVISOR_UDP_ECHO_PORT 9000U
#define SUPERVISOR_UDP_ECHO_MAX_DATA 32U
#define SUPERVISOR_UDP_ECHO_REPLY_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} supervisor_udp_echo_reply_t;

typedef struct {
    uint32_t request_id;
    uint32_t transaction_epoch;
    uint32_t source_ip;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t data_length;
    uint16_t reserved;
    uint8_t source_mac[6];
    uint8_t data[SUPERVISOR_UDP_ECHO_MAX_DATA];
    uint8_t reserved_tail[2];
} supervisor_udp_echo_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_udp_echo_context_t;

typedef struct {
    critical_object_t object;
} supervisor_protected_network_context_t;

typedef enum {
    SUPERVISOR_NETWORK_DEGRADED_EXPIRED = 1,
    SUPERVISOR_NETWORK_DEGRADED_QUEUE = 2,
    SUPERVISOR_NETWORK_DEGRADED_SEMANTIC = 3,
} supervisor_network_degradation_reason_t;

typedef struct {
    uint32_t expired;
    uint32_t queue_fallback;
    uint32_t semantic_reject;
} supervisor_network_degradation_stats_t;

void supervisor_network_degradation_init(
    supervisor_network_degradation_stats_t *stats);
void supervisor_network_degradation_record(
    supervisor_network_degradation_stats_t *stats,
    supervisor_network_degradation_reason_t reason);
int supervisor_network_degradation_snapshot(
    supervisor_network_degradation_stats_t *stats_out);

void supervisor_probe_authority_init(supervisor_probe_authority_t *authority);
int supervisor_probe_authority_begin(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t timeout_ms,
                                     uint32_t *probe_id_out);
bool supervisor_probe_authority_take(supervisor_probe_authority_t *authority,
                                     uint64_t now_ms, uint32_t *probe_id_out);
bool supervisor_probe_authority_expire(supervisor_probe_authority_t *authority,
                                       uint64_t now_ms);
void supervisor_probe_authority_cancel(supervisor_probe_authority_t *authority);
int supervisor_protected_probe_authority_init(
    supervisor_protected_probe_authority_t *authority);
int supervisor_protected_probe_authority_begin(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t timeout_ms, uint32_t *probe_id_out);
int supervisor_protected_probe_authority_begin_epoch(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t timeout_ms, uint32_t transaction_epoch,
    uint32_t *probe_id_out);
int supervisor_protected_probe_authority_take(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t *probe_id_out);
int supervisor_protected_probe_authority_take_epoch(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t transaction_epoch, uint32_t *probe_id_out);
int supervisor_protected_probe_authority_expire(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms);
int supervisor_protected_probe_authority_expire_epoch(
    supervisor_protected_probe_authority_t *authority, uint64_t now_ms,
    uint32_t transaction_epoch);
int supervisor_protected_probe_authority_cancel(
    supervisor_protected_probe_authority_t *authority);
int supervisor_protected_network_context_init(
    supervisor_protected_network_context_t *context);
int supervisor_protected_network_context_prepare(
    supervisor_protected_network_context_t *context, uint32_t gateway,
    uint32_t local_ip, const uint8_t local_mac[6]);
int supervisor_protected_network_context_prepare_epoch(
    supervisor_protected_network_context_t *context, uint32_t transaction_epoch,
    uint32_t gateway, uint32_t local_ip, const uint8_t local_mac[6]);
int supervisor_protected_network_context_snapshot(
    supervisor_protected_network_context_t *context,
    supervisor_network_probe_context_t *snapshot_out);
int supervisor_protected_network_context_publish(
    supervisor_protected_network_context_t *context, uint32_t probe_id);
int supervisor_protected_network_context_publish_epoch(
    supervisor_protected_network_context_t *context, uint32_t transaction_epoch,
    uint32_t probe_id);
int supervisor_protected_network_context_publish_binding_epoch(
    supervisor_protected_network_context_t *context, uint32_t transaction_epoch,
    uint32_t probe_id, uint32_t candidate_ip,
    const uint8_t candidate_mac[6]);
int supervisor_protected_network_context_consume(
    supervisor_protected_network_context_t *context, uint32_t probe_id);
int supervisor_protected_network_context_consume_epoch(
    supervisor_protected_network_context_t *context, uint32_t transaction_epoch,
    uint32_t probe_id);
int supervisor_protected_network_context_clear(
    supervisor_protected_network_context_t *context);
int supervisor_protected_arp_reply_context_init(
    supervisor_protected_arp_reply_context_t *context);
int supervisor_protected_arp_reply_context_publish(
    supervisor_protected_arp_reply_context_t *context, uint32_t request_id,
    uint32_t transaction_epoch, uint32_t target_ip,
    const uint8_t target_mac[6]);
int supervisor_protected_arp_reply_context_snapshot(
    supervisor_protected_arp_reply_context_t *context,
    supervisor_arp_reply_context_t *snapshot_out);
int supervisor_protected_arp_reply_context_clear(
    supervisor_protected_arp_reply_context_t *context);
int supervisor_protected_arp_resolution_context_init(
    supervisor_protected_arp_resolution_context_t *context);
int supervisor_protected_arp_resolution_context_publish(
    supervisor_protected_arp_resolution_context_t *context,
    uint32_t request_id, uint32_t transaction_epoch, uint32_t target_ip);
int supervisor_protected_arp_resolution_context_snapshot(
    supervisor_protected_arp_resolution_context_t *context,
    supervisor_arp_resolution_context_t *snapshot_out);
int supervisor_protected_arp_resolution_context_clear(
    supervisor_protected_arp_resolution_context_t *context);
int supervisor_protected_icmp_echo_context_init(
    supervisor_protected_icmp_echo_context_t *context);
int supervisor_protected_icmp_echo_context_publish(
    supervisor_protected_icmp_echo_context_t *context, uint32_t request_id,
    uint32_t transaction_epoch, uint32_t source_ip,
    const uint8_t source_mac[6], uint16_t identifier, uint16_t sequence,
    const uint8_t *data, uint16_t data_length);
int supervisor_protected_icmp_echo_context_snapshot(
    supervisor_protected_icmp_echo_context_t *context,
    supervisor_icmp_echo_context_t *snapshot_out);
int supervisor_protected_icmp_echo_context_clear(
    supervisor_protected_icmp_echo_context_t *context);
int supervisor_protected_dhcp_context_init(
    supervisor_protected_dhcp_context_t *context);
int supervisor_protected_dhcp_context_publish(
    supervisor_protected_dhcp_context_t *context, uint32_t request_id,
    uint32_t transaction_epoch, uint32_t ip_address, uint32_t netmask,
    uint32_t gateway, uint32_t dns_server);
int supervisor_protected_dhcp_context_snapshot(
    supervisor_protected_dhcp_context_t *context,
    supervisor_dhcp_context_t *snapshot_out);
int supervisor_protected_dhcp_context_clear(
    supervisor_protected_dhcp_context_t *context);
int supervisor_protected_udp_echo_context_init(
    supervisor_protected_udp_echo_context_t *context);
int supervisor_protected_udp_echo_context_publish(
    supervisor_protected_udp_echo_context_t *context, uint32_t request_id,
    uint32_t transaction_epoch, uint32_t source_ip,
    const uint8_t source_mac[6], uint16_t source_port,
    uint16_t destination_port, const uint8_t *data, uint16_t data_length);
int supervisor_protected_udp_echo_context_snapshot(
    supervisor_protected_udp_echo_context_t *context,
    supervisor_udp_echo_context_t *snapshot_out);
int supervisor_protected_udp_echo_context_clear(
    supervisor_protected_udp_echo_context_t *context);
int supervisor_protected_probe_control_init(
    supervisor_protected_probe_control_t *control);
int supervisor_protected_probe_control_read(
    supervisor_protected_probe_control_t *control,
    supervisor_probe_control_t *snapshot_out);
int supervisor_protected_probe_control_write(
    supervisor_protected_probe_control_t *control,
    const supervisor_probe_control_t *snapshot);

void supervisor_init(void);
void supervisor_clock_tick(uint64_t now_ms);
bool supervisor_start_worker(void);
bool supervisor_start_probe(uint64_t now_ms);
bool supervisor_probe_ready(void);
int supervisor_probe_report(int pid, uint32_t generation,
                            uint32_t report_type, uint32_t value,
                            uint64_t now_ms);
int supervisor_service_connect(struct Process *client, uint32_t service_id,
                               uint32_t *handle_out);
bool supervisor_network_submit_header(const uint8_t *frame, uint16_t length);
int supervisor_network_probe_request(int pid, uint32_t generation,
                                     uint64_t now_ms);
int supervisor_network_probe_request_id(int pid, uint32_t generation,
                                        uint64_t now_ms,
                                        uint32_t *probe_id_out);
int supervisor_network_commit_arp_binding(
    int pid, uint32_t generation, const supervisor_arp_binding_t *binding);
int supervisor_network_send_arp_reply(
    int pid, uint32_t generation, const supervisor_arp_reply_t *reply);
bool supervisor_network_request_arp_resolution(uint32_t target_ip);
int supervisor_network_send_arp_request(
    int pid, uint32_t generation,
    const supervisor_arp_resolution_t *request);
bool supervisor_network_submit_icmp_echo(
    uint32_t source_ip, const uint8_t source_mac[6], uint16_t identifier,
    uint16_t sequence, const uint8_t *data, uint16_t data_length);
int supervisor_network_send_icmp_echo_reply(
    int pid, uint32_t generation,
    const supervisor_icmp_echo_reply_t *reply);
bool supervisor_network_submit_dhcp_config(
    uint32_t ip_address, uint32_t netmask, uint32_t gateway,
    uint32_t dns_server);
int supervisor_network_commit_dhcp_config(
    int pid, uint32_t generation, const supervisor_dhcp_commit_t *commit);
bool supervisor_network_submit_udp_echo(
    uint32_t source_ip, const uint8_t source_mac[6], uint16_t source_port,
    uint16_t destination_port, const uint8_t *data, uint16_t data_length);
int supervisor_network_send_udp_echo_reply(
    int pid, uint32_t generation, const supervisor_udp_echo_reply_t *reply);
int supervisor_spawn_service(const char *path, int argc,
                             const char *const *argv, uint32_t domain_kind);
int supervisor_register(const char *name, const supervisor_config_t *config,
                        const supervisor_fence_ops_t *fence_ops,
                        uint64_t now_ms, supervisor_handle_t *handle_out);
int supervisor_report_progress(supervisor_handle_t handle,
                               uint64_t progress_marker, uint64_t now_ms);
int supervisor_report_idle(supervisor_handle_t handle);
supervisor_event_t supervisor_poll(uint64_t now_ms);
supervisor_event_t supervisor_service_one(uint64_t now_ms);
supervisor_event_t supervisor_apply_fence(supervisor_handle_t handle,
                                          uint64_t now_ms);
int supervisor_report_self_test(supervisor_handle_t handle, bool passed,
                                uint64_t now_ms);
bool supervisor_output_allowed(supervisor_handle_t handle);

#ifdef REIST_HOST_TEST
int supervisor_test_corrupt_fence_ops(supervisor_handle_t handle,
                                      bool corrupt_both_copies);
int supervisor_test_corrupt_descriptor(supervisor_handle_t handle,
                                       bool corrupt_both_copies);
int supervisor_test_corrupt_network_degradation(bool corrupt_both_copies);
int supervisor_test_record_network_degradation(
    supervisor_network_degradation_reason_t reason);
int supervisor_test_corrupt_probe_authority(
    supervisor_protected_probe_authority_t *authority,
    bool corrupt_both_copies);
int supervisor_test_corrupt_network_context(
    supervisor_protected_network_context_t *context,
    bool corrupt_both_copies);
int supervisor_test_corrupt_arp_reply_context(
    supervisor_protected_arp_reply_context_t *context,
    bool corrupt_both_copies);
int supervisor_test_corrupt_probe_control(
    supervisor_protected_probe_control_t *control,
    bool corrupt_both_copies);
#endif

#endif
