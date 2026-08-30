#include "x86os.h"
#include "reist/tls.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/private/entropy.h"
#include "psa/crypto.h"
#include "psa/crypto_driver_random.h"
#include "psa/crypto_extra.h"
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t index = 0U; index < length; ++index) out[index] = in[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out < in) {
        for (size_t index = 0U; index < length; ++index) out[index] = in[index];
    } else if (out > in) {
        while (length != 0U) { --length; out[length] = in[length]; }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    for (size_t index = 0U; index < length; ++index)
        out[index] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t index = 0U; index < length; ++index)
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
        uint8_t a = (uint8_t)left[index], b = (uint8_t)right[index];
        if (a != b) return a < b ? -1 : 1;
        if (a == 0U) return 0;
    }
    return 0;
}

char *strchr(const char *text, int value) {
    char wanted = (char)value;
    for (;;) {
        if (*text == wanted) return (char *)text;
        if (*text++ == '\0') return 0;
    }
}

char *strstr(const char *text, const char *needle) {
    if (*needle == '\0') return (char *)text;
    for (; *text != '\0'; ++text) {
        size_t index = 0U;
        while (needle[index] != '\0' && text[index] == needle[index]) ++index;
        if (needle[index] == '\0') return (char *)text;
    }
    return 0;
}

typedef union arena_block {
    uint64_t alignment;
    struct {
        uint32_t magic;
        uint32_t span;
        uint32_t requested;
        uint32_t allocated;
    } fields;
} arena_block_t;

#define REIST_TLS_ALLOCATION_MAGIC 0x414c5354U
#define REIST_TLS_ARENA_ALIGNMENT 8U

static const reist_tls_platform_t *active_platform;
static uint8_t *allocation_arena;
static uint32_t allocation_used;

static uint32_t align_arena(uint32_t value) {
    return (value + REIST_TLS_ARENA_ALIGNMENT - 1U) &
           ~(REIST_TLS_ARENA_ALIGNMENT - 1U);
}

static int valid_arena_block(uint32_t offset, const arena_block_t *block) {
    return allocation_arena != 0 && block != 0 &&
           block->fields.magic == REIST_TLS_ALLOCATION_MAGIC &&
           block->fields.span >= sizeof(arena_block_t) &&
           (block->fields.span & (REIST_TLS_ARENA_ALIGNMENT - 1U)) == 0U &&
           offset <= REIST_TLS_HEAP_BUDGET_BYTES - block->fields.span &&
           block->fields.requested <=
               block->fields.span - sizeof(arena_block_t) &&
           block->fields.allocated <= 1U;
}

void *reist_tls_mbed_calloc(size_t count, size_t size) {
    if (active_platform == 0 || allocation_arena == 0 ||
        count == 0U || size == 0U ||
        (count != 0U && size > (size_t)-1 / count)) return 0;
    size_t total = count * size;
    if (total > REIST_TLS_MAX_ALLOCATION_BYTES ||
        total > UINT32_MAX - sizeof(arena_block_t) ||
        total > REIST_TLS_HEAP_BUDGET_BYTES - allocation_used) return 0;
    uint32_t required = align_arena(
        (uint32_t)total + (uint32_t)sizeof(arena_block_t));
    for (uint32_t offset = 0U; offset < REIST_TLS_HEAP_BUDGET_BYTES;) {
        arena_block_t *block =
            (arena_block_t *)(void *)(allocation_arena + offset);
        if (!valid_arena_block(offset, block)) return 0;
        uint32_t span = block->fields.span;
        if (!block->fields.allocated && span >= required) {
            uint32_t remainder = span - required;
            if (remainder >= sizeof(arena_block_t) + REIST_TLS_ARENA_ALIGNMENT) {
                arena_block_t *next = (arena_block_t *)(void *)
                    (allocation_arena + offset + required);
                next->fields.magic = REIST_TLS_ALLOCATION_MAGIC;
                next->fields.span = remainder;
                next->fields.requested = 0U;
                next->fields.allocated = 0U;
                block->fields.span = required;
            }
            block->fields.requested = (uint32_t)total;
            block->fields.allocated = 1U;
            allocation_used += (uint32_t)total;
            void *pointer = block + 1;
            memset(pointer, 0, total);
            return pointer;
        }
        offset += span;
    }
    return 0;
}

void reist_tls_mbed_free(void *pointer) {
    if (pointer == 0) return;
    if (active_platform == 0 || allocation_arena == 0) return;
    arena_block_t *previous = 0;
    for (uint32_t offset = 0U; offset < REIST_TLS_HEAP_BUDGET_BYTES;) {
        arena_block_t *block =
            (arena_block_t *)(void *)(allocation_arena + offset);
        if (!valid_arena_block(offset, block)) return;
        uint32_t span = block->fields.span;
        if ((void *)(block + 1) == pointer) {
            if (!block->fields.allocated ||
                block->fields.requested > allocation_used) return;
            uint32_t requested = block->fields.requested;
            memset(pointer, 0, requested);
            allocation_used -= requested;
            block->fields.requested = 0U;
            block->fields.allocated = 0U;

            uint32_t next_offset = offset + block->fields.span;
            if (next_offset < REIST_TLS_HEAP_BUDGET_BYTES) {
                arena_block_t *next = (arena_block_t *)(void *)
                    (allocation_arena + next_offset);
                if (!valid_arena_block(next_offset, next)) return;
                if (!next->fields.allocated) {
                    block->fields.span += next->fields.span;
                    memset(next, 0, sizeof(*next));
                }
            }
            if (previous != 0 && !previous->fields.allocated) {
                previous->fields.span += block->fields.span;
                memset(block, 0, sizeof(*block));
            }
            return;
        }
        previous = block;
        offset += span;
    }
}

void *reist_tls_heap_allocate(void *platform, uint32_t count, uint32_t size) {
    (void)platform;
    if (count == 0U || size == 0U || count > UINT32_MAX / size) return 0;
    return x86os_malloc((size_t)count * size);
}

void reist_tls_heap_free(void *platform, void *pointer) {
    (void)platform;
    x86os_free(pointer);
}

static uint64_t unsigned_divide(uint64_t dividend, uint64_t divisor,
                                uint64_t *remainder_out) {
    uint64_t quotient = 0U, remainder = 0U;
    if (divisor == 0U) {
        if (remainder_out != 0) *remainder_out = 0U;
        return 0U;
    }
    for (int bit = 63; bit >= 0; --bit) {
        uint64_t next = (dividend >> (uint32_t)bit) & 1U;
        uint64_t high = remainder >> 63U;
        remainder = (remainder << 1U) | next;
        if (high != 0U || remainder >= divisor) {
            remainder -= divisor;
            quotient |= (uint64_t)1U << (uint32_t)bit;
        }
    }
    if (remainder_out != 0) *remainder_out = remainder;
    return quotient;
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    return unsigned_divide(dividend, divisor, 0);
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    uint64_t remainder = 0U;
    (void)unsigned_divide(dividend, divisor, &remainder);
    return remainder;
}

static int leap_year(uint32_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint32_t month_days(uint32_t year, uint32_t month) {
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    return days[month - 1U] + (month == 2U && leap_year(year) ? 1U : 0U);
}

int reist_tls_rtc_time(void *platform, int64_t *result) {
    (void)platform;
    if (result == 0) return -22;
    uint32_t date = x86os_get_date(), clock = x86os_get_time();
    uint32_t year = date >> 16U, month = (date >> 8U) & 0xffU;
    uint32_t day = date & 0xffU, hour = clock >> 16U;
    uint32_t minute = (clock >> 8U) & 0xffU, second = clock & 0xffU;
    if (year < 2026U || year > 2099U || month < 1U || month > 12U ||
        day < 1U || day > month_days(year, month) || hour > 23U ||
        minute > 59U || second > 59U) return -5;
    int64_t days = 0;
    for (uint32_t value = 1970U; value < year; ++value)
        days += leap_year(value) ? 366 : 365;
    for (uint32_t value = 1U; value < month; ++value)
        days += month_days(year, value);
    days += (int64_t)day - 1;
    int64_t value = days * 86400 + (int64_t)hour * 3600 +
                    (int64_t)minute * 60 + second;
    *result = value;
    return 0;
}

int reist_tls_monotonic_time(void *platform, uint64_t *milliseconds) {
    (void)platform;
    if (milliseconds == 0) return -22;
    return x86os_monotonic_ms(milliseconds);
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *source,
                                      struct tm *result) {
    if (source == 0 || result == 0 || *source < 0) return 0;
    uint64_t value = (uint64_t)*source, days = value / 86400U;
    uint32_t seconds = (uint32_t)(value % 86400U), year = 1970U;
    while (year <= 2099U) {
        uint32_t count = leap_year(year) ? 366U : 365U;
        if (days < count) break;
        days -= count; ++year;
    }
    if (year > 2099U) return 0;
    uint32_t month = 1U;
    while (month <= 12U && days >= month_days(year, month)) {
        days -= month_days(year, month); ++month;
    }
    if (month > 12U) return 0;
    *result = (struct tm){
        (int)(seconds % 60U), (int)((seconds / 60U) % 60U),
        (int)(seconds / 3600U), (int)days + 1, (int)month - 1,
        (int)year - 1900, 0, 0, 0
    };
    return result;
}

static int rdrand_supported(void) {
    uint32_t eax = 1U, ebx, ecx, edx;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ecx & (1U << 30U)) != 0U;
}

static int rdrand32(uint32_t *value) {
    uint8_t success;
    __asm__ __volatile__(".byte 0x0f,0xc7,0xf0; setc %1"
                         : "=a"(*value), "=qm"(success) : : "cc");
    return success != 0U;
}

int reist_tls_hardware_entropy(void *platform, uint8_t *output,
                               uint32_t length) {
    (void)platform;
    if (output == 0 || length == 0U || length > 4096U ||
        !rdrand_supported()) return -5;
    uint32_t previous = 0U;
    int have_previous = 0;
    uint32_t used = 0U;
    while (used < length) {
        uint32_t sample = 0U;
        int ready = 0;
        for (uint32_t attempt = 0U; attempt < 16U && !ready; ++attempt)
            ready = rdrand32(&sample);
        if (!ready || (have_previous && sample == previous)) {
            memset(output, 0, length);
            return -5;
        }
        previous = sample;
        have_previous = 1;
        for (uint32_t index = 0U; index < 4U && used < length; ++index)
            output[used++] = (uint8_t)(sample >> (index * 8U));
    }
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    uint64_t milliseconds = 0U;
    if (active_platform == 0 || active_platform->monotonic == 0 ||
        active_platform->monotonic(active_platform->platform,
                                   &milliseconds) != 0)
        return 0;
    return (mbedtls_ms_time_t)milliseconds;
}

int reist_tls_platform_bind(const reist_tls_platform_t *platform) {
    if (platform == 0 || platform->allocate == 0 || platform->free == 0 ||
        active_platform != 0 || allocation_arena != 0 ||
        allocation_used != 0U) return -16;
    uint8_t *arena = (uint8_t *)platform->allocate(
        platform->platform, 1U, REIST_TLS_HEAP_BUDGET_BYTES);
    if (arena == 0) return -12;
    memset(arena, 0, REIST_TLS_HEAP_BUDGET_BYTES);
    arena_block_t *first = (arena_block_t *)(void *)arena;
    first->fields.magic = REIST_TLS_ALLOCATION_MAGIC;
    first->fields.span = REIST_TLS_HEAP_BUDGET_BYTES;
    active_platform = platform;
    allocation_arena = arena;
    return 0;
}

void reist_tls_platform_unbind(const reist_tls_platform_t *platform) {
    if (active_platform == platform && allocation_arena != 0 &&
        allocation_used == 0U) {
        uint8_t *arena = allocation_arena;
        allocation_arena = 0;
        active_platform = 0;
        memset(arena, 0, REIST_TLS_HEAP_BUDGET_BYTES);
        platform->free(platform->platform, arena);
    }
}

int64_t reist_tls_mbed_time_bridge(int64_t *result) {
    int64_t value = 0;
    if (active_platform == 0 || active_platform->time == 0 ||
        active_platform->time(active_platform->platform, &value) != 0)
        return (int64_t)-1;
    if (result != 0) *result = value;
    return value;
}

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output, size_t output_size) {
    (void)flags;
    if (estimate_bits != 0) *estimate_bits = 0U;
    if (output == 0 || estimate_bits == 0 || output_size == 0U ||
        output_size > 4096U || output_size > UINT32_MAX ||
        active_platform == 0 || active_platform->entropy == 0 ||
        active_platform->entropy(active_platform->platform, output,
                                 (uint32_t)output_size) != 0) {
        if (output != 0 && output_size <= 4096U) memset(output, 0, output_size);
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *estimate_bits = output_size * 8U;
    return 0;
}
