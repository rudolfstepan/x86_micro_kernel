/**
 * @file userspace/sdk/include/x86os.h
 * @brief Versionierte öffentliche REIST-Userspace-ABI.
 *
 * Layer: Ring-3 SDK and protocol support.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef X86OS_USER_API_H
#define X86OS_USER_API_H

#include <stddef.h>
#include <stdint.h>

enum {
    X86OS_SYS_PUTCHAR = 0,
    X86OS_SYS_PRINT_NUMBER = 1,
    X86OS_SYS_DELAY = 2,
    X86OS_SYS_WAIT_ENTER = 3,
    X86OS_SYS_MALLOC = 4,
    X86OS_SYS_FREE = 5,
    X86OS_SYS_REALLOC = 6,
    X86OS_SYS_GETCHAR = 7,
    X86OS_SYS_EXIT = 9,
    X86OS_SYS_GET_DATE = 10,
    X86OS_SYS_GET_TIME = 11,
    X86OS_SYS_UPTIME_MS = 12,
    X86OS_SYS_MEMORY_KB = 13,
    X86OS_SYS_OPEN = 14,
    X86OS_SYS_READ = 15,
    X86OS_SYS_CLOSE = 16,
    X86OS_SYS_STAT = 17,
    X86OS_SYS_READDIR = 18,
    X86OS_SYS_CREATE = 19,
    X86OS_SYS_WRITE = 20,
    X86OS_SYS_UNLINK = 21,
    X86OS_SYS_GETPID = 22,
    X86OS_SYS_SPAWN = 23,
    X86OS_SYS_WAIT = 24,
    X86OS_SYS_READDIR_BATCH = 25,
    X86OS_SYS_PROCESS_INFO = 26,
    X86OS_SYS_KILL = 27,
    X86OS_SYS_GETCWD = 28,
    X86OS_SYS_CHDIR = 29,
    X86OS_SYS_SPAWNV = 30,
    X86OS_SYS_DRIVE_INFO = 31,
    X86OS_SYS_SPACE = 32,
    X86OS_SYS_MKDIR = 33,
    X86OS_SYS_RMDIR = 34,
    X86OS_SYS_CLEAR = 35,
    X86OS_SYS_SET_CURSOR = 36,
    X86OS_SYS_TERMINAL_WRITE = 37,
    X86OS_SYS_TERMINAL_DRAW = 38,
    X86OS_SYS_GETCHAR_NONBLOCKING = 39,
    X86OS_SYS_YIELD = 40,
    X86OS_SYS_SLEEP_MS = 41,
    X86OS_SYS_MONOTONIC_MS = 42,
    X86OS_SYS_MEMORY_STATS = 43,
    X86OS_SYS_DISPLAY_INFO = 44,
    X86OS_SYS_FILL_RECT = 45,
    X86OS_SYS_DRAW_TEXT = 46,
    X86OS_SYS_RENAME = 47,
    X86OS_SYS_FSYNC = 48,
    X86OS_SYS_IPC_CREATE = 49,
    X86OS_SYS_IPC_SEND = 50,
    X86OS_SYS_IPC_RECEIVE = 51,
    X86OS_SYS_IPC_CLOSE = 52,
    X86OS_SYS_IPC_SEND_TIMEOUT = 53,
    X86OS_SYS_IPC_RECEIVE_TIMEOUT = 54,
    X86OS_SYS_IPC_DELEGATE = 55,
    X86OS_SYS_REIST_REPORT = 56,
    X86OS_SYS_SERVICE_CONNECT = 57,
    X86OS_SYS_IPC_RELEASE = 58,
    X86OS_SYS_NETWORK_PROBE = 59,
    X86OS_SYS_NETWORK_PROBE_ID = 60,
    X86OS_SYS_NETWORK_PROBE_STATS = 61,
    X86OS_SYS_REIST_ARP_BINDING = 62,
    X86OS_SYS_REIST_ARP_REPLY = 63,
    X86OS_SYS_REIST_ARP_RESOLUTION = 64,
    X86OS_SYS_NETWORK_ARP_RESOLVE = 65,
    X86OS_SYS_STORAGE_BIND = 66,
    X86OS_SYS_STORAGE_SUBMIT = 67,
    X86OS_SYS_STORAGE_CLAIM = 68,
    X86OS_SYS_STORAGE_BLOCK_READ = 69,
    X86OS_SYS_STORAGE_COMPLETE = 70,
    X86OS_SYS_STORAGE_COLLECT = 71,
    X86OS_SYS_REIST_ICMP_ECHO_REPLY = 72,
    X86OS_SYS_REIST_DHCP_COMMIT = 73,
    X86OS_SYS_REIST_UDP_ECHO_REPLY = 74,
    X86OS_SYS_REIST_UDP_BIND = 75,
    X86OS_SYS_REIST_UDP_UNBIND = 76,
    X86OS_SYS_REIST_UDP_REPLY = 77,
    X86OS_SYS_REIST_DHCP_RENEW = 78,
    X86OS_SYS_REIST_NETWORK_FRAME = 79,
    X86OS_SYS_REIST_UDP_INGRESS = 80,
    X86OS_SYS_REIST_DHCP_INGRESS = 81,
    X86OS_SYS_REIST_DHCP_BOOT_START = 82,
    X86OS_SYS_REIST_ICMP_INGRESS = 83,
    X86OS_SYS_SCHEDULER_STATS = 84,
    X86OS_SYS_STORAGE_BLOCK_WRITE = 85,
    X86OS_SYS_STORAGE_MAINT_ACQUIRE = 86,
    X86OS_SYS_STORAGE_MAINT_RENEW = 87,
    X86OS_SYS_STORAGE_MAINT_RELEASE = 88,
    X86OS_SYS_DRIVE_STATUS = 89,
    X86OS_SYS_ADMIN_STORAGE = 90,
      X86OS_SYS_COMPONENT_CONTROL = 91,
      X86OS_SYS_PARTITION_CREATE = 92,
      X86OS_SYS_STORAGE_BLOCK_FLUSH = 93,
      X86OS_SYS_STORAGE_MEDIA_COMMIT = 94,
    X86OS_SYS_STORAGE_FORMAT_PROBE = 95,
    X86OS_SYS_NETWORK_CONTROL = 96,
    X86OS_SYS_UDP_SOCKET_CONTROL = 97,
    X86OS_SYS_UDP_SOCKET_SENDTO = 98,
    X86OS_SYS_UDP_SOCKET_RECVFROM = 99,
    X86OS_SYS_UDP_SOCKET_INGRESS = 100,
    X86OS_SYS_TCP_SOCKET_CONTROL = 101,
    X86OS_SYS_TCP_SOCKET_CONNECT = 102,
    X86OS_SYS_TCP_SOCKET_SEND = 103,
    X86OS_SYS_TCP_SOCKET_RECEIVE = 104,
    X86OS_SYS_TCP_SOCKET_INGRESS = 105,
    X86OS_SYS_TCP_SOCKET_LISTEN = 106,
    X86OS_SYS_TCP_SOCKET_ACCEPT = 107,
    X86OS_SYS_TOUCH = 108,
    X86OS_SYS_DISPLAY_CONTROL = 109,
    X86OS_SYS_MOUSE_EVENT = 110
};

#define X86OS_TCP_SOCKET_VERSION 1U
#define X86OS_TCP_MAX_SEGMENT 512U
#define X86OS_TCP_RECEIVE_CAPACITY 2048U
#define X86OS_TCP_MAX_BACKLOG 2U
enum {
    X86OS_TCP_SOCKET_OPEN = 1U,
    X86OS_TCP_SOCKET_CLOSE = 2U,
    X86OS_TCP_SOCKET_STATS = 3U,
};
typedef uint32_t x86os_tcp_socket_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    x86os_tcp_socket_t socket;
    uint32_t timeout_ms;
    uint32_t active_sockets;
    uint32_t established_sockets;
    uint32_t retransmissions;
} x86os_tcp_socket_control_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_tcp_socket_t socket;
    uint32_t destination_ip;
    uint16_t destination_port;
    uint16_t reserved;
    uint32_t timeout_ms;
} x86os_tcp_connect_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_tcp_socket_t socket;
    uint32_t length;
    uint32_t timeout_ms;
} x86os_tcp_io_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_tcp_socket_t socket;
    uint16_t port;
    uint16_t backlog;
    uint32_t reserved;
} x86os_tcp_listen_t;
/* On entry only listener and timeout_ms are nonzero. A successful accept fills
 * socket, peer_ip and peer_port with a newly owned connection descriptor. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_tcp_socket_t listener;
    x86os_tcp_socket_t socket;
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t reserved;
    uint32_t timeout_ms;
} x86os_tcp_accept_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t window;
    uint16_t length;
    uint8_t flags;
    uint8_t reserved[3];
} x86os_tcp_segment_t;

#define X86OS_UDP_SOCKET_VERSION 1U
#define X86OS_UDP_MAX_DATAGRAM 512U
enum {
    X86OS_UDP_SOCKET_OPEN = 1U,
    X86OS_UDP_SOCKET_BIND = 2U,
    X86OS_UDP_SOCKET_CLOSE = 3U,
    X86OS_UDP_SOCKET_STATS = 4U,
};
typedef uint32_t x86os_udp_socket_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    x86os_udp_socket_t socket;
    uint16_t port;
    uint16_t reserved;
    uint32_t active_sockets;
    uint32_t queued_datagrams;
    uint32_t dropped_datagrams;
} x86os_udp_socket_control_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_udp_socket_t socket;
    uint32_t ip;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t length;
    uint32_t timeout_ms;
} x86os_udp_datagram_t;

#define X86OS_DNS_RESULT_VERSION 1U
#define X86OS_DNS_MAX_NAME 253U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t address;
    uint32_t ttl_seconds;
    uint32_t from_cache;
    char canonical_name[X86OS_DNS_MAX_NAME + 1U];
} x86os_dns_result_t;

#define X86OS_NETWORK_CONTROL_VERSION 2U
enum {
    X86OS_NETWORK_STATUS = 1U,
    X86OS_NETWORK_CONFIGURE = 2U,
    X86OS_NETWORK_PING = 3U,
    X86OS_NETWORK_ARP_REQUEST = 4U
};

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t target_ip;
    uint32_t identifier;
    uint32_t sequence;
    uint32_t timeout_ms;
} x86os_network_control_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t operation_result;
    uint32_t available;
    uint32_t ready;
    uint32_t configured;
    uint32_t ip_address;
    uint32_t netmask;
    uint32_t gateway;
    char backend[16];
    uint8_t mac_address[6];
    uint8_t reserved[2];
    uint32_t dns_server;
} x86os_network_control_result_t;

enum {
    X86OS_FILE = 1,
    X86OS_DIRECTORY = 2
};

typedef struct {
    char name[256];
    uint32_t type;
    uint32_t size;
    uint32_t create_time;
    uint32_t modify_time;
    uint32_t access_time;
} x86os_file_info_t;

enum {
    X86OS_PROCESS_READY = 0,
    X86OS_PROCESS_RUNNING = 1,
    X86OS_PROCESS_SLEEPING = 2,
    X86OS_PROCESS_WAITING = 3,
    X86OS_PROCESS_ZOMBIE = 4
};

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    int32_t state;
    int32_t exit_status;
    char name[32];
} x86os_process_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint64_t detected_usable_bytes;
    uint64_t managed_bytes;
    uint64_t reserved_bytes;
    uint64_t allocated_frame_bytes;
    uint64_t free_frame_bytes;
    uint64_t heap_capacity_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_free_bytes;
    uint64_t heap_largest_free_block;
    uint64_t heap_arena_count;
    uint64_t peak_allocated_frame_bytes;
    uint64_t frame_allocation_failures;
    uint64_t peak_heap_used_bytes;
    uint64_t heap_allocation_failures;
} x86os_memory_stats_t;

#define X86OS_MEMORY_STATS_V1_VERSION 1U
#define X86OS_MEMORY_STATS_V1_SIZE 88U
#define X86OS_MEMORY_STATS_VERSION 2U

#define X86OS_SCHEDULER_STATS_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t task_capacity;
    uint32_t active_tasks;
    uint32_t peak_active_tasks;
    uint32_t capacity_rejections;
    uint32_t supervised_reserve;
    uint32_t reserved;
} x86os_scheduler_stats_t;

#define X86OS_IPC_MAX_MESSAGE_SIZE 128U
#define X86OS_IPC_QUEUE_DEPTH 4U
#define X86OS_IPC_MAX_CAPABILITIES_PER_PROCESS 8U
#define X86OS_IPC_MESSAGE_VERSION 1U
#define X86OS_IPC_INVALID_HANDLE 0U
#define X86OS_IPC_DEFAULT_TIMEOUT_MS 1000U
#define X86OS_IPC_RIGHT_SEND 0x01U
#define X86OS_IPC_RIGHT_RECEIVE 0x02U
#define X86OS_IPC_RIGHT_CONTROL 0x04U

#define X86OS_REIST_REPORT_SELF_TEST 1U
#define X86OS_REIST_REPORT_PROGRESS 2U
#define X86OS_REIST_REPORT_INVALID 3U
#define X86OS_REIST_REPORT_NETWORK_HEADER 4U
#define X86OS_REIST_REPORT_NETWORK_PROBE_ID 5U
#define X86OS_REIST_REPORT_NETWORK_DEGRADED 6U
#define X86OS_REIST_REPORT_NETWORK_FRAME 7U
#define X86OS_REIST_REPORT_NETWORK_IPV4 8U
#define X86OS_REIST_REPORT_NETWORK_UDP 9U
#define X86OS_REIST_REPORT_NETWORK_DHCP 10U
#define X86OS_REIST_REPORT_NETWORK_ICMP 11U
#define X86OS_REIST_REPORT_SERVICE_READY 12U
#define X86OS_REIST_NETWORK_DEGRADED_SEMANTIC 3U
#define X86OS_SERVICE_DIAGNOSTIC 1U

#define X86OS_NETWORK_PROBE_STATS_VERSION 1U
#define X86OS_REIST_ARP_BINDING_VERSION 1U
#define X86OS_REIST_ARP_REPLY_VERSION 1U
#define X86OS_REIST_ARP_RESOLUTION_VERSION 1U
#define X86OS_REIST_ICMP_ECHO_REPLY_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t expired;
    uint32_t queue_fallback;
    uint32_t semantic_reject;
    uint32_t reserved;
} x86os_network_probe_stats_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t probe_id;
    uint32_t ip;
    uint8_t mac[6];
    uint8_t reserved[2];
} x86os_reist_arp_binding_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t target_ip;
    uint8_t target_mac[6];
    uint8_t reserved[2];
} x86os_reist_arp_reply_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t target_ip;
} x86os_reist_arp_resolution_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} x86os_reist_icmp_echo_reply_t;

#define X86OS_REIST_ICMP_INGRESS_VERSION 1U
#define X86OS_REIST_ICMP_ECHO_MAX_DATA 32U
#define X86OS_REIST_ICMP_INGRESS_DROP 0U
#define X86OS_REIST_ICMP_INGRESS_ECHO_REQUEST 1U
#define X86OS_REIST_ICMP_INGRESS_ECHO_REPLY 2U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t frame_crc32;
    uint32_t reserved;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint8_t source_mac[6];
    uint16_t identifier;
    uint16_t sequence;
    uint16_t data_length;
    uint8_t operation;
    uint8_t reserved_byte;
    uint16_t reserved_tail;
} x86os_reist_icmp_ingress_t;

#define X86OS_REIST_DHCP_COMMIT_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} x86os_reist_dhcp_commit_t;

#define X86OS_REIST_DHCP_RENEW_REQUEST_VERSION 1U
#define X86OS_REIST_DHCP_RENEW 1U
#define X86OS_REIST_DHCP_REBIND 2U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t expected_ip;
} x86os_reist_dhcp_renew_request_t;

#define X86OS_REIST_NETWORK_FRAME_VERSION 1U
#define X86OS_REIST_NETWORK_FRAME_MAX_SIZE 1518U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t length;
    uint32_t reserved;
    uint8_t data[X86OS_REIST_NETWORK_FRAME_MAX_SIZE];
    uint8_t padding[2];
} x86os_reist_network_frame_t;

#define X86OS_REIST_UDP_ECHO_REPLY_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t reserved;
} x86os_reist_udp_echo_reply_t;

#define X86OS_REIST_UDP_BIND_REQUEST_VERSION 1U
#define X86OS_REIST_UDP_REPLY_VERSION 1U
#define X86OS_REIST_UDP_INGRESS_VERSION 1U
#define X86OS_REIST_UDP_MAX_DATA 32U
typedef uint32_t x86os_reist_udp_binding_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint16_t port;
    uint16_t max_data;
    uint32_t reserved;
} x86os_reist_udp_bind_request_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_reist_udp_binding_t binding;
    uint32_t request_id;
} x86os_reist_udp_reply_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_reist_udp_binding_t binding;
    uint32_t request_id;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint32_t frame_crc32;
    uint8_t source_mac[6];
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t data_length;
} x86os_reist_udp_ingress_t;

#define X86OS_REIST_DHCP_INGRESS_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t frame_crc32;
    uint32_t transaction_id;
    uint32_t offered_ip;
    uint32_t server_id;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t lease_seconds;
    uint32_t option_flags;
    uint8_t client_mac[6];
    uint8_t message_type;
    uint8_t checksum_present;
} x86os_reist_dhcp_ingress_t;

#define X86OS_REIST_DHCP_BOOT_START_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
} x86os_reist_dhcp_boot_start_t;

#define X86OS_STORAGE_REQUEST_VERSION 1U
#define X86OS_STORAGE_BLOCK_SIZE 512U
#define X86OS_STORAGE_BLOCK_READ 1U
#define X86OS_STORAGE_BLOCK_WRITE 2U
#define X86OS_STORAGE_BLOCK_FLUSH 3U
#define X86OS_STORAGE_VFS_READ 4U
#define X86OS_STORAGE_VFS_WRITE 5U
#define X86OS_STORAGE_VFS_SYNC 6U
#define X86OS_STORAGE_FORMAT_FAT12 7U
#define X86OS_STORAGE_FORMAT_FAT32 8U
#define X86OS_STORAGE_FORMAT_FAT32_SCAN 9U
#define X86OS_STORAGE_FORMAT_FAT32_PREPARE 10U
typedef uint32_t x86os_storage_handle_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t resource;
    uint32_t offset;
    uint32_t length;
    uint32_t timeout_ms;
} x86os_storage_submit_t;
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    x86os_storage_handle_t handle;
    uint32_t operation;
    uint32_t resource;
    uint32_t offset;
    uint32_t length;
} x86os_storage_descriptor_t;

typedef uint32_t x86os_ipc_handle_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t length;
    uint8_t payload[X86OS_IPC_MAX_MESSAGE_SIZE];
} x86os_ipc_message_t;

#define X86OS_DISPLAY_ABI_VERSION 1U
#define X86OS_DISPLAY_MAX_TEXT 256U
#define X86OS_DISPLAY_CONTROL_VERSION 1U
#define X86OS_DISPLAY_ACTIVATE 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t red_field_position;
    uint32_t red_mask_size;
    uint32_t green_field_position;
    uint32_t green_mask_size;
    uint32_t blue_field_position;
    uint32_t blue_mask_size;
    uint32_t font_width;
    uint32_t font_height;
} x86os_display_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t rgb;
} x86os_display_rect_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t foreground_rgb;
    uint32_t background_rgb;
    uint32_t text_address;
    uint32_t text_length;
} x86os_display_text_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
} x86os_display_control_t;

#define X86OS_MOUSE_EVENT_VERSION 1U
#define X86OS_MOUSE_BUTTON_LEFT 0x01U
#define X86OS_MOUSE_BUTTON_RIGHT 0x02U
#define X86OS_MOUSE_BUTTON_MIDDLE 0x04U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel;
    uint32_t buttons;
    uint32_t generation;
    uint32_t reserved;
} x86os_mouse_event_t;

enum {
    X86OS_DRIVE_ATA = 1,
    X86OS_DRIVE_FDD = 2,
    X86OS_DRIVE_AHCI = 3,
    X86OS_DRIVE_PARTITION = 4
};

typedef struct {
    uint32_t type;
    char name[8];
    char mount_point[64];
    uint32_t sectors;
} x86os_drive_info_t;

#define X86OS_PARTITION_REQUEST_VERSION 1U
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t resource;
    uint32_t first_lba;
    uint32_t sectors;
    uint32_t type;
    uint32_t confirm;
} x86os_partition_request_t;

#define X86OS_DRIVE_STATUS_VERSION 1U
#define X86OS_DRIVE_STATUS_AVAILABLE   (1U << 0)
#define X86OS_DRIVE_STATUS_READ_ONLY   (1U << 1)
#define X86OS_DRIVE_STATUS_DEGRADED    (1U << 2)
#define X86OS_DRIVE_STATUS_QUARANTINED (1U << 3)
#define X86OS_DRIVE_STATUS_RECOVERING  (1U << 4)
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t flags;
    uint32_t reserved;
} x86os_drive_status_t;

#define X86OS_ADMIN_MAINTENANCE_VERSION 1U
#define X86OS_ADMIN_PATH_MAX 64U
#define X86OS_ADMIN_FS_MAX 16U
enum {
    X86OS_ADMIN_STORAGE_STATUS = 0,
    X86OS_ADMIN_STORAGE_DEVICE_DOWN = 1,
    X86OS_ADMIN_STORAGE_DEVICE_UP = 2,
    X86OS_ADMIN_STORAGE_MOUNT = 3,
    X86OS_ADMIN_STORAGE_UMOUNT = 4,
};
enum {
    X86OS_ADMIN_RESOURCE_ONLINE = 1,
    X86OS_ADMIN_RESOURCE_TRANSITION = 2,
    X86OS_ADMIN_RESOURCE_DOWN = 3,
    X86OS_ADMIN_RESOURCE_FAILED = 4,
};
#define X86OS_ADMIN_RESOURCE_MOUNTED   (1U << 0)
#define X86OS_ADMIN_RESOURCE_ROOT      (1U << 1)
#define X86OS_ADMIN_RESOURCE_PARENT    (1U << 2)
#define X86OS_ADMIN_RESOURCE_BLOCKED   (1U << 3)
#define X86OS_ADMIN_RESOURCE_AVAILABLE (1U << 4)
#define X86OS_ADMIN_RESOURCE_READ_ONLY (1U << 5)
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t resource;
    uint32_t drain_timeout_ms;
    uint32_t reserved;
    char fs_type[X86OS_ADMIN_FS_MAX];
    char mount_path[X86OS_ADMIN_PATH_MAX];
} x86os_admin_storage_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t resource;
    uint32_t state;
    uint32_t generation;
    uint32_t flags;
    uint32_t parent_resource;
    uint32_t open_handles;
    uint32_t drive_type;
    uint32_t reserved;
    char name[8];
    char fs_type[X86OS_ADMIN_FS_MAX];
    char mount_path[X86OS_ADMIN_PATH_MAX];
} x86os_admin_storage_result_t;

#define X86OS_COMPONENT_CONTROL_VERSION 1U
#define X86OS_COMPONENT_COUNT 7U
#define X86OS_COMPONENT_NAME_CAPACITY 24U
enum {
    X86OS_COMPONENT_STATUS = 0,
    X86OS_COMPONENT_DOWN = 1,
    X86OS_COMPONENT_UP = 2,
    X86OS_COMPONENT_RESTART = 3,
};
enum {
    X86OS_COMPONENT_READY = 1,
    X86OS_COMPONENT_QUIESCING = 2,
    X86OS_COMPONENT_OFFLINE = 3,
    X86OS_COMPONENT_STARTING = 4,
    X86OS_COMPONENT_FAILED = 5,
};
#define X86OS_COMPONENT_MANAGEABLE (1U << 0)
#define X86OS_COMPONENT_PROTECTED  (1U << 1)
#define X86OS_COMPONENT_DRIVER     (1U << 2)
#define X86OS_COMPONENT_SERVICE    (1U << 3)
#define X86OS_COMPONENT_FENCED     (1U << 4)
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t command;
    uint32_t component;
    uint32_t expected_generation;
    uint32_t timeout_ms;
} x86os_component_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t component;
    uint32_t state;
    uint32_t generation;
    uint32_t flags;
    uint32_t dependency_mask;
    int32_t last_error;
    char name[X86OS_COMPONENT_NAME_CAPACITY];
} x86os_component_result_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
} x86os_space_info_t;

#define X86OS_READDIR_BATCH_CAPACITY 4U

uintptr_t x86os_syscall(uint32_t number, uintptr_t argument1,
                        uintptr_t argument2, uintptr_t argument3);
void x86os_putchar(char value);
void x86os_puts(const char* text);
void x86os_print_number(int value);
void x86os_delay(uint32_t milliseconds);
int x86os_sleep_ms(uint32_t milliseconds);
int x86os_yield(void);
int x86os_monotonic_ms(uint64_t* value);
int x86os_memory_stats(x86os_memory_stats_t* stats);
int x86os_scheduler_stats(x86os_scheduler_stats_t *stats);
int x86os_ipc_create(x86os_ipc_handle_t* handle);
int x86os_ipc_send(x86os_ipc_handle_t handle,
                   const x86os_ipc_message_t* message);
int x86os_ipc_receive(x86os_ipc_handle_t handle,
                      x86os_ipc_message_t* message);
int x86os_ipc_send_timeout(x86os_ipc_handle_t handle,
                           const x86os_ipc_message_t* message,
                           uint32_t timeout_ms);
int x86os_ipc_receive_timeout(x86os_ipc_handle_t handle,
                              x86os_ipc_message_t* message,
                              uint32_t timeout_ms);
int x86os_ipc_close(x86os_ipc_handle_t handle);
int x86os_ipc_release(x86os_ipc_handle_t handle);
int x86os_network_probe(void);
int x86os_network_probe_id(uint32_t *probe_id);
int x86os_network_probe_stats(x86os_network_probe_stats_t *stats);
int x86os_reist_commit_arp_binding(
    const x86os_reist_arp_binding_t *binding);
int x86os_reist_send_arp_reply(const x86os_reist_arp_reply_t *reply);
int x86os_reist_send_arp_request(
    const x86os_reist_arp_resolution_t *request);
int x86os_reist_send_icmp_echo_reply(
    const x86os_reist_icmp_echo_reply_t *reply);
int x86os_reist_icmp_ingress(const x86os_reist_icmp_ingress_t *ingress,
                             const uint8_t *data);
int x86os_reist_commit_dhcp(
    const x86os_reist_dhcp_commit_t *commit);
int x86os_reist_renew_dhcp(
    const x86os_reist_dhcp_renew_request_t *request);
int x86os_reist_receive_network_frame(x86os_reist_network_frame_t *frame);
int x86os_reist_send_udp_echo_reply(
    const x86os_reist_udp_echo_reply_t *reply);
int x86os_reist_udp_bind(const x86os_reist_udp_bind_request_t *request,
                         x86os_reist_udp_binding_t *binding);
int x86os_reist_udp_unbind(x86os_reist_udp_binding_t binding);
int x86os_reist_udp_reply(const x86os_reist_udp_reply_t *reply);
int x86os_reist_udp_ingress(x86os_reist_udp_ingress_t *ingress,
                            const uint8_t *data);
int x86os_reist_dhcp_ingress(
    const x86os_reist_dhcp_ingress_t *ingress);
int x86os_reist_start_dhcp_boot(
    const x86os_reist_dhcp_boot_start_t *request);
int x86os_network_arp_resolve(uint32_t target_ip);
int x86os_network_control(const x86os_network_control_request_t *request,
                          x86os_network_control_result_t *result);
int x86os_udp_socket_open(x86os_udp_socket_t *socket_out);
int x86os_udp_socket_bind(x86os_udp_socket_t socket, uint16_t port);
int x86os_udp_socket_close(x86os_udp_socket_t socket);
int x86os_udp_socket_stats(x86os_udp_socket_control_t *stats_out);
int x86os_udp_sendto(const x86os_udp_datagram_t *datagram,
                     const void *data);
int x86os_udp_recvfrom(x86os_udp_datagram_t *datagram, void *data);
int x86os_udp_socket_ingress(const x86os_udp_datagram_t *datagram,
                             const void *data);
int x86os_dns_resolve(const char *name, uint32_t timeout_ms,
                      x86os_dns_result_t *result);
int x86os_dns_resolve_at(const char *name, uint32_t server,
                         uint32_t timeout_ms, x86os_dns_result_t *result);
int x86os_tcp_socket_open(x86os_tcp_socket_t *socket_out);
int x86os_tcp_socket_close(x86os_tcp_socket_t socket, uint32_t timeout_ms);
int x86os_tcp_socket_stats(x86os_tcp_socket_control_t *stats_out);
int x86os_tcp_connect(const x86os_tcp_connect_t *request);
/* Passive-open wrappers preserve the versioned fixed-size syscall ABI. */
int x86os_tcp_listen(const x86os_tcp_listen_t *request);
int x86os_tcp_accept(x86os_tcp_accept_t *request);
int x86os_tcp_send(const x86os_tcp_io_t *request, const void *data);
int x86os_tcp_receive(x86os_tcp_io_t *request, void *data);
int x86os_tcp_socket_ingress(const x86os_tcp_segment_t *segment,
                             const void *data);
int x86os_storage_bind(void);
int x86os_storage_submit(const x86os_storage_submit_t *request,
                         const void *data, x86os_storage_handle_t *handle);
int x86os_storage_claim(x86os_storage_descriptor_t *request, void *data);
int x86os_storage_block_read(uint32_t resource, uint32_t block, void *data);
int x86os_storage_block_write(uint32_t resource, uint32_t block,
                               const void *data);
int x86os_storage_block_flush(uint32_t resource);
int x86os_storage_media_commit(uint32_t resource, uint32_t *fingerprint);
int x86os_storage_format_probe(uint32_t resource, uint32_t sector);
int x86os_storage_maintenance_acquire(uint32_t resource,
                                      uint32_t media_fingerprint,
                                      uint32_t *token);
int x86os_storage_maintenance_renew(uint32_t resource, uint32_t token,
                                    uint32_t media_fingerprint);
int x86os_storage_maintenance_release(uint32_t resource, uint32_t token);
int x86os_storage_complete(x86os_storage_handle_t handle, int32_t result,
                           const void *data);
int x86os_storage_collect(x86os_storage_handle_t handle, int32_t *result,
                          void *data);
int x86os_ipc_delegate(x86os_ipc_handle_t handle, int target_pid,
                       uint32_t rights);
int x86os_reist_report(uint32_t report_type, uint32_t value);
int x86os_service_connect(uint32_t service_id,
                          x86os_ipc_handle_t* handle);
int x86os_display_info(x86os_display_info_t* info);
int x86os_display_activate(void);
int x86os_mouse_event(x86os_mouse_event_t* event);
int x86os_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                    uint32_t rgb);
int x86os_draw_text_pixels(int32_t x, int32_t y, const char* text,
                           size_t length, uint32_t foreground_rgb,
                           uint32_t background_rgb);
int x86os_getchar(void);
int x86os_getchar_nonblocking(void);
void* x86os_malloc(size_t size);
void x86os_free(void* pointer);
void* x86os_realloc(void* pointer, size_t size);
uint32_t x86os_get_date(void);
uint32_t x86os_get_time(void);
uint32_t x86os_uptime_ms(void);
uint32_t x86os_memory_kb(void);
int x86os_open(const char* path);
int x86os_read(int descriptor, void* buffer, size_t size);
int x86os_close(int descriptor);
int x86os_stat(const char* path, x86os_file_info_t* info);
int x86os_readdir(const char* path, uint32_t index, x86os_file_info_t* info);
int x86os_readdir_batch(const char* path, uint32_t index,
                        x86os_file_info_t* entries);
int x86os_create(const char* path);
int x86os_write(int descriptor, const void* buffer, size_t size);
int x86os_fsync(int descriptor);
int x86os_unlink(const char* path);
int x86os_rename(const char* old_path, const char* new_path);
int x86os_touch(const char* path);
int x86os_getpid(void);
int x86os_spawn(const char* path);
int x86os_spawnv(const char* path, int argc, const char* const* argv);
int x86os_wait(int pid, int* status);
int x86os_process_info(uint32_t index, x86os_process_info_t* info);
int x86os_kill(int pid);
int x86os_getcwd(char* buffer, size_t size);
int x86os_chdir(const char* path);
int x86os_drive_info(uint32_t index, x86os_drive_info_t* info);
int x86os_partition_create(const x86os_partition_request_t *request);
int x86os_drive_status(uint32_t index, x86os_drive_status_t* status);
int x86os_admin_storage(const x86os_admin_storage_request_t* request,
                        x86os_admin_storage_result_t* result);
int x86os_component_control(const x86os_component_request_t* request,
                            x86os_component_result_t* result);
int x86os_space(const char* path, x86os_space_info_t* info);
int x86os_mkdir(const char* path);
int x86os_rmdir(const char* path);
void x86os_clear(void);
void x86os_set_cursor(unsigned int column, unsigned int row);
void x86os_draw_text(unsigned int column, unsigned int row,
                     const char* text, size_t length);
void x86os_exit(int status) __attribute__((noreturn));

#endif
