/**
 * @file lib/libc/stdlib.h
 * @brief Freestanding Laufzeit-, Konvertierungs- und Syscallkonstanten.
 *
 * Layer: Freestanding kernel/userspace runtime.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef STDLIB_H    /* This is an "include guard" */
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define SUCCESS 0
#define FAILURE -1

// define syscall indexes
#define SYS_TERMINAL_PUTCHAR 0 // One argument syscall
#define SYS_PRINT 1
#define SYS_DELAY 2
#define SYS_WAIT_ENTER 3
#define SYS_MALLOC 4
#define SYS_FREE 5
#define SYS_REALLOC 6
#define SYS_TERMINAL_GETCHAR 7
#define SYS_INSTALL_IRQ 8 /* Reserved: always rejected by the kernel. */
#define SYS_EXIT 9
#define SYS_GET_DATE 10
#define SYS_GET_TIME 11
#define SYS_UPTIME_MS 12
#define SYS_MEMORY_KB 13
#define SYS_OPEN 14
#define SYS_READ 15
#define SYS_CLOSE 16
#define SYS_STAT 17
#define SYS_READDIR 18
#define SYS_CREATE 19
#define SYS_WRITE 20
#define SYS_UNLINK 21
#define SYS_GETPID 22
#define SYS_SPAWN 23
#define SYS_WAIT 24
#define SYS_READDIR_BATCH 25
#define SYS_PROCESS_INFO 26
#define SYS_KILL 27
#define SYS_GETCWD 28
#define SYS_CHDIR 29
#define SYS_SPAWNV 30
#define SYS_DRIVE_INFO 31
#define SYS_SPACE 32
#define SYS_MKDIR 33
#define SYS_RMDIR 34
#define SYS_CLEAR 35
#define SYS_SET_CURSOR 36
#define SYS_TERMINAL_WRITE 37
#define SYS_TERMINAL_DRAW 38
#define SYS_GETCHAR_NONBLOCKING 39
#define SYS_YIELD 40
#define SYS_SLEEP_MS 41
#define SYS_MONOTONIC_MS 42
#define SYS_MEMORY_STATS 43
#define SYS_DISPLAY_INFO 44
#define SYS_FILL_RECT 45
#define SYS_DRAW_TEXT 46
#define SYS_RENAME 47
#define SYS_FSYNC 48
#define SYS_IPC_CREATE 49
#define SYS_IPC_SEND 50
#define SYS_IPC_RECEIVE 51
#define SYS_IPC_CLOSE 52
#define SYS_IPC_SEND_TIMEOUT 53
#define SYS_IPC_RECEIVE_TIMEOUT 54
#define SYS_IPC_DELEGATE 55
#define SYS_REIST_REPORT 56
#define SYS_SERVICE_CONNECT 57
#define SYS_IPC_RELEASE 58
#define SYS_NETWORK_PROBE 59
#define SYS_NETWORK_PROBE_ID 60
#define SYS_NETWORK_PROBE_STATS 61
#define SYS_REIST_ARP_BINDING 62
#define SYS_REIST_ARP_REPLY 63
#define SYS_REIST_ARP_RESOLUTION 64
#define SYS_NETWORK_ARP_RESOLVE 65
#define SYS_STORAGE_BIND 66
#define SYS_STORAGE_SUBMIT 67
#define SYS_STORAGE_CLAIM 68
#define SYS_STORAGE_BLOCK_READ 69
#define SYS_STORAGE_COMPLETE 70
#define SYS_STORAGE_COLLECT 71
#define SYS_REIST_ICMP_ECHO_REPLY 72
#define SYS_REIST_DHCP_COMMIT 73
#define SYS_REIST_UDP_ECHO_REPLY 74
#define SYS_REIST_UDP_BIND 75
#define SYS_REIST_UDP_UNBIND 76
#define SYS_REIST_UDP_REPLY 77
#define SYS_REIST_DHCP_RENEW 78
#define SYS_REIST_NETWORK_FRAME 79
#define SYS_REIST_UDP_INGRESS 80
#define SYS_REIST_DHCP_INGRESS 81
#define SYS_REIST_DHCP_BOOT_START 82
#define SYS_REIST_ICMP_INGRESS 83
#define SYS_SCHEDULER_STATS 84
#define SYS_STORAGE_BLOCK_WRITE 85
#define SYS_STORAGE_MAINT_ACQUIRE 86
#define SYS_STORAGE_MAINT_RENEW 87
#define SYS_STORAGE_MAINT_RELEASE 88
#define SYS_DRIVE_STATUS 89
#define SYS_PARTITION_CREATE 92
#define SYS_STORAGE_BLOCK_FLUSH 93
#define SYS_STORAGE_MEDIA_COMMIT 94
#define SYS_STORAGE_FORMAT_PROBE 95
#define SYS_NETWORK_CONTROL 96
#define SYS_UDP_SOCKET_CONTROL 97
#define SYS_UDP_SOCKET_SENDTO 98
#define SYS_UDP_SOCKET_RECVFROM 99
#define SYS_UDP_SOCKET_INGRESS 100
#define SYS_TCP_SOCKET_CONTROL 101
#define SYS_TCP_SOCKET_CONNECT 102
#define SYS_TCP_SOCKET_SEND 103
#define SYS_TCP_SOCKET_RECEIVE 104
#define SYS_TCP_SOCKET_INGRESS 105
#define SYS_TCP_SOCKET_LISTEN 106
#define SYS_TCP_SOCKET_ACCEPT 107
#define SYS_TOUCH 108
#define SYS_DISPLAY_CONTROL 109
#define SYS_MOUSE_EVENT 110
#define SYS_POINTER_UPDATE 111
#define SYS_USB_DIAGNOSTICS 112
#define SYS_DEVICE_CONTROL 113
#define SYS_PROCESS_IDENTITY 114
#define SYS_DRAW_TEXT_CLIPPED 115
#define SYS_RUNTIME_TIMING 116

// // Macros for try-catch handling
// #define try(ctx) if (setjmp(&(ctx)) == 0)
// #define catch(ctx, ex) else if ((ctx).exception_code == (ex))


// typedef struct TryContext {
//     //uint32_t padding1;  // Padding before the structure
//     uint32_t esp;
//     uint32_t ebp;
//     uint32_t eip;
//     int exception_code;
//     //uint32_t padding2;  // Padding after the structure
// } TryContext;

// extern TryContext* current_try_context;

uint32_t get_esp();
uint32_t get_ebp();

// Declare setjmp and longjmp
// extern int setjmp(TryContext* ctx);
// extern void longjmp(TryContext* ctx, int exception_code);

// void throw(TryContext* ctx, int exception_code);

int initialize_memory_system(void);

void* malloc(size_t size);
void* aligned_alloc(size_t alignment, size_t size);
void aligned_free(void* ptr);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);
void* memmove(void* dest, const void* src, size_t n);

// String conversion functions
int atoi(const char* str);

// wrapper functions for system calls
void delay_ms(uint32_t ms);
void wait_enter_pressed();

void exit(int status) __attribute__((noreturn));

// Disable interrupts
void disable_interrupts();

// Enable interrupts
void enable_interrupts();

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);
uint64_t __umoddi3(uint64_t dividend, uint64_t divisor);

#endif /* STDLIB_H */
