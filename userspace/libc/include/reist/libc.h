#ifndef REIST_LIBC_H
#define REIST_LIBC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#define REIST_LIBC_NORETURN [[noreturn]]
#else
#define REIST_LIBC_NORETURN _Noreturn
#endif
#define REIST_LIBC_VERSION 1U
#define REIST_LIBC_HEAP_LIMIT (4U * 1024U * 1024U)
#define REIST_LIBC_OBJECT_LIMIT 4096U
#define REIST_LIBC_FAULT_HEAP 1U
/* One execution context per process. No thread/IRQ/reentrant allocation support.
 * Caller owns aligned writable storage until reset; no implicit heap syscalls.
 * Init requires at least max_align_t bytes; reset refuses outstanding objects.
 * Zero-sized malloc/calloc return NULL; realloc(p,0) frees p and returns NULL.
 * Invalid ownership/metadata invokes the bounded process-local fatal path.
 * Raw pointers cannot distinguish a stale pointer after same-address reuse. */
int reist_libc_init(void *storage, size_t capacity);
int reist_libc_reset(void);
typedef struct {
    uint32_t version, struct_size, capacity, live_objects, live_bytes, peak_bytes;
} reist_libc_stats_t;
int reist_libc_stats(reist_libc_stats_t *stats);
int *reist_libc_errno(void);
REIST_LIBC_NORETURN void reist_libc_fail(unsigned code);
#ifdef __cplusplus
}
#endif
#endif
