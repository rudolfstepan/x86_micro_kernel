/**
 * @file userspace/programs/benchmark.c
 * @brief Begrenzter Ring-3-Benchmark fuer CPU, RAM, Datentraeger und VGA.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Alle Messungen verwenden nur oeffentliche SDK-Operationen und
 * feste Arbeitsgrenzen. Ergebnisse sind vergleichende Diagnosewerte, keine
 * Hardwarezertifizierung.
 * Safety: Der Datentraegertest verwendet eine generationsbezogene temporaere
 * Datei, validiert gelesene Daten und entfernt sie auf jedem Cleanup-Pfad.
 */
#include "x86os.h"
#include <stdbool.h>

#define BENCHMARK_TARGET_MS 200U
#define BENCHMARK_MAX_ELAPSED_MS 90000U
#define BENCHMARK_CPU_INITIAL_ITERATIONS (1U << 20U)
#define BENCHMARK_CPU_MAX_ITERATIONS (1U << 26U)
#define BENCHMARK_CPU_ATTEMPTS 7U
#define BENCHMARK_CPU_OPERATIONS_PER_ITERATION 8U
#define BENCHMARK_MEMORY_BYTES (256U * 1024U)
#define BENCHMARK_MEMORY_WORDS \
    (BENCHMARK_MEMORY_BYTES / (uint32_t)sizeof(uint32_t))
#define BENCHMARK_MEMORY_INITIAL_PASSES 8U
#define BENCHMARK_MEMORY_MAX_PASSES 256U
#define BENCHMARK_MEMORY_ATTEMPTS 6U
#define BENCHMARK_DISK_CHUNK_BYTES 4096U
#define BENCHMARK_DISK_CHUNKS 64U
#define BENCHMARK_DISK_PROGRESS_CHUNKS 16U
#define BENCHMARK_DISK_BYTES \
    (BENCHMARK_DISK_CHUNK_BYTES * BENCHMARK_DISK_CHUNKS)
#define BENCHMARK_VGA_INITIAL_FRAMES 2U
#define BENCHMARK_VGA_MAX_FRAMES 16U
#define BENCHMARK_VGA_ATTEMPTS 4U
#define BENCHMARK_PATH_CAPACITY 64U

_Static_assert(BENCHMARK_MEMORY_BYTES % sizeof(uint32_t) == 0U,
               "memory benchmark must use complete words");
_Static_assert(BENCHMARK_DISK_BYTES == 256U * 1024U,
               "disk benchmark byte bound changed");
_Static_assert(BENCHMARK_DISK_CHUNKS % BENCHMARK_DISK_PROGRESS_CHUNKS == 0U,
               "disk progress must divide the bounded workload");

typedef enum {
    BENCHMARK_OK = 0,
    BENCHMARK_UNAVAILABLE,
    BENCHMARK_FAILED
} benchmark_status_t;

typedef struct {
    uint64_t hundredths;
    const char *unit;
    benchmark_status_t status;
} benchmark_result_t;

static volatile uint32_t memory_words[BENCHMARK_MEMORY_WORDS];
static uint8_t disk_buffer[BENCHMARK_DISK_CHUNK_BYTES];
static volatile uint32_t benchmark_sink;

static uint64_t divide_unsigned(uint64_t numerator, uint64_t denominator,
                                uint64_t *remainder_out) {
    if (denominator == 0U) {
        if (remainder_out != 0) *remainder_out = 0U;
        return 0U;
    }
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    for (uint64_t bit = (uint64_t)1U << 63U; bit != 0U; bit >>= 1U) {
        remainder <<= 1U;
        if ((numerator & bit) != 0U) remainder |= 1U;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= bit;
        }
    }
    if (remainder_out != 0) *remainder_out = remainder;
    return quotient;
}

static size_t text_length(const char *text) {
    size_t length = 0U;
    if (text == 0) return 0U;
    while (text[length] != '\0') ++length;
    return length;
}

static void print_field(const char *text, uint32_t width) {
    size_t length = text_length(text);
    for (size_t index = 0U; index < length && index < width; ++index)
        x86os_putchar(text[index]);
    for (size_t index = length; index < width; ++index)
        x86os_putchar(' ');
}

static size_t append_unsigned(char *buffer, size_t capacity, size_t used,
                              uint64_t value) {
    char digits[20];
    size_t count = 0U;
    do {
        uint64_t remainder = 0U;
        value = divide_unsigned(value, 10U, &remainder);
        digits[count++] = (char)('0' + (uint32_t)remainder);
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U && used + 1U < capacity)
        buffer[used++] = digits[--count];
    return used;
}

static void format_result(const benchmark_result_t *result,
                          char buffer[24]) {
    if (result == 0 || result->status != BENCHMARK_OK) {
        buffer[0] = '-';
        buffer[1] = '\0';
        return;
    }
    uint64_t fraction = 0U;
    uint64_t whole = divide_unsigned(
        result->hundredths, 100U, &fraction);
    size_t used = append_unsigned(buffer, 24U, 0U, whole);
    if (used + 1U < 24U) buffer[used++] = '.';
    if (used + 1U < 24U)
        buffer[used++] = (char)('0' + (uint32_t)fraction / 10U);
    if (used + 1U < 24U)
        buffer[used++] = (char)('0' + (uint32_t)fraction % 10U);
    if (used + 1U < 24U) buffer[used++] = ' ';
    for (size_t index = 0U; result->unit[index] != '\0' &&
         used + 1U < 24U; ++index)
        buffer[used++] = result->unit[index];
    buffer[used] = '\0';
}

static const char *status_text(benchmark_status_t status) {
    if (status == BENCHMARK_OK) return "OK";
    if (status == BENCHMARK_UNAVAILABLE) return "N/V";
    return "FEHLER";
}

static void print_result_row(const char *area, const char *test,
                             const benchmark_result_t *result) {
    char value[24];
    format_result(result, value);
    x86os_puts("| ");
    print_field(area, 8U);
    x86os_puts(" | ");
    print_field(test, 20U);
    x86os_puts(" | ");
    print_field(value, 16U);
    x86os_puts(" | ");
    print_field(status_text(result->status), 8U);
    x86os_puts(" |\n");
}

static int elapsed_ms(uint64_t started, uint64_t *elapsed) {
    uint64_t finished = 0U;
    if (elapsed == 0 || x86os_monotonic_ms(&finished) != 0 ||
        finished < started) return -1;
    *elapsed = finished - started;
    return *elapsed <= BENCHMARK_MAX_ELAPSED_MS ? 0 : -1;
}

/* Keep progress visible without charging serial/framebuffer rendering time to
 * the measured device operation.  The exclusion itself is deadline-bounded
 * and fails closed on a clock regression or implausibly long console stall. */
static int timed_status(const char *status, uint64_t *excluded_ms) {
    uint64_t before = 0U;
    uint64_t after = 0U;
    if (status == 0 || excluded_ms == 0 ||
        x86os_monotonic_ms(&before) != 0) return -1;
    x86os_puts(status);
    if (x86os_monotonic_ms(&after) != 0 || after < before) return -1;
    uint64_t delta = after - before;
    if (delta > BENCHMARK_MAX_ELAPSED_MS ||
        *excluded_ms > BENCHMARK_MAX_ELAPSED_MS - delta) return -1;
    *excluded_ms += delta;
    return 0;
}

static const char *disk_write_progress(uint32_t completed_chunks) {
    switch (completed_chunks) {
        case 16U:
            return "BENCHMARK_STATUS phase=hdd-write progress_kib=64 total_kib=256\n";
        case 32U:
            return "BENCHMARK_STATUS phase=hdd-write progress_kib=128 total_kib=256\n";
        case 48U:
            return "BENCHMARK_STATUS phase=hdd-write progress_kib=192 total_kib=256\n";
        case 64U:
            return "BENCHMARK_STATUS phase=hdd-write progress_kib=256 total_kib=256\n";
        default:
            return 0;
    }
}

static const char *disk_read_progress(uint32_t completed_chunks) {
    switch (completed_chunks) {
        case 16U:
            return "BENCHMARK_STATUS phase=hdd-read progress_kib=64 total_kib=256\n";
        case 32U:
            return "BENCHMARK_STATUS phase=hdd-read progress_kib=128 total_kib=256\n";
        case 48U:
            return "BENCHMARK_STATUS phase=hdd-read progress_kib=192 total_kib=256\n";
        case 64U:
            return "BENCHMARK_STATUS phase=hdd-read progress_kib=256 total_kib=256\n";
        default:
            return 0;
    }
}

static benchmark_result_t benchmark_cpu(void) {
    benchmark_result_t result = {0U, "MOp/s", BENCHMARK_FAILED};
    uint32_t iterations = BENCHMARK_CPU_INITIAL_ITERATIONS;
    for (uint32_t attempt = 0U; attempt < BENCHMARK_CPU_ATTEMPTS; ++attempt) {
        uint64_t started = 0U;
        if (x86os_monotonic_ms(&started) != 0) return result;
        uint32_t a = 0x13579BDFU;
        uint32_t b = 0x2468ACE1U;
        for (uint32_t index = 0U; index < iterations; ++index) {
            a ^= b + 0x9E3779B9U;
            a = (a << 7U) | (a >> 25U);
            b += a ^ index;
            b = b * 1664525U + 1013904223U;
        }
        benchmark_sink ^= a ^ b;
        uint64_t elapsed = 0U;
        if (elapsed_ms(started, &elapsed) != 0) return result;
        if (elapsed != 0U &&
            (elapsed >= BENCHMARK_TARGET_MS ||
             iterations == BENCHMARK_CPU_MAX_ITERATIONS)) {
            uint64_t operations =
                (uint64_t)iterations * BENCHMARK_CPU_OPERATIONS_PER_ITERATION;
            result.hundredths = divide_unsigned(
                operations, elapsed * 10U, 0);
            result.status = BENCHMARK_OK;
            return result;
        }
        if (iterations >= BENCHMARK_CPU_MAX_ITERATIONS / 2U)
            iterations = BENCHMARK_CPU_MAX_ITERATIONS;
        else
            iterations *= 2U;
    }
    return result;
}

static benchmark_result_t benchmark_memory_write(void) {
    benchmark_result_t result = {0U, "MiB/s", BENCHMARK_FAILED};
    uint32_t passes = BENCHMARK_MEMORY_INITIAL_PASSES;
    for (uint32_t attempt = 0U; attempt < BENCHMARK_MEMORY_ATTEMPTS;
         ++attempt) {
        uint64_t started = 0U;
        if (x86os_monotonic_ms(&started) != 0) return result;
        for (uint32_t pass = 0U; pass < passes; ++pass)
            for (uint32_t index = 0U; index < BENCHMARK_MEMORY_WORDS; ++index)
                memory_words[index] = index ^ (pass * 0x9E3779B9U);
        uint64_t elapsed = 0U;
        if (elapsed_ms(started, &elapsed) != 0) return result;
        if (elapsed != 0U &&
            (elapsed >= BENCHMARK_TARGET_MS ||
             passes == BENCHMARK_MEMORY_MAX_PASSES)) {
            uint64_t bytes = (uint64_t)BENCHMARK_MEMORY_BYTES * passes;
            result.hundredths = divide_unsigned(
                bytes * 100000U, elapsed * 1024U * 1024U, 0);
            result.status = BENCHMARK_OK;
            return result;
        }
        if (passes >= BENCHMARK_MEMORY_MAX_PASSES / 2U)
            passes = BENCHMARK_MEMORY_MAX_PASSES;
        else
            passes *= 2U;
    }
    return result;
}

static benchmark_result_t benchmark_memory_read(void) {
    benchmark_result_t result = {0U, "MiB/s", BENCHMARK_FAILED};
    uint32_t passes = BENCHMARK_MEMORY_INITIAL_PASSES;
    for (uint32_t attempt = 0U; attempt < BENCHMARK_MEMORY_ATTEMPTS;
         ++attempt) {
        uint64_t started = 0U;
        if (x86os_monotonic_ms(&started) != 0) return result;
        uint32_t checksum = 0U;
        for (uint32_t pass = 0U; pass < passes; ++pass)
            for (uint32_t index = 0U; index < BENCHMARK_MEMORY_WORDS; ++index)
                checksum += memory_words[index] ^ pass;
        benchmark_sink ^= checksum;
        uint64_t elapsed = 0U;
        if (elapsed_ms(started, &elapsed) != 0) return result;
        if (elapsed != 0U &&
            (elapsed >= BENCHMARK_TARGET_MS ||
             passes == BENCHMARK_MEMORY_MAX_PASSES)) {
            uint64_t bytes = (uint64_t)BENCHMARK_MEMORY_BYTES * passes;
            result.hundredths = divide_unsigned(
                bytes * 100000U, elapsed * 1024U * 1024U, 0);
            result.status = BENCHMARK_OK;
            return result;
        }
        if (passes >= BENCHMARK_MEMORY_MAX_PASSES / 2U)
            passes = BENCHMARK_MEMORY_MAX_PASSES;
        else
            passes *= 2U;
    }
    return result;
}

static void append_hex32(char *path, size_t *used, uint32_t value) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        path[(*used)++] = hexadecimal[(value >> (uint32_t)shift) & 0xFU];
}

static int make_disk_path(char path[BENCHMARK_PATH_CAPACITY]) {
    static const char prefix[] = "REIST-BENCH-";
    static const char suffix[] = ".TMP";
    x86os_process_identity_t identity;
    if (x86os_process_identity(&identity) != 0 || identity.pid <= 0 ||
        identity.generation == 0U) return -1;
    size_t used = 0U;
    for (size_t index = 0U; index < sizeof(prefix) - 1U; ++index)
        path[used++] = prefix[index];
    append_hex32(path, &used, (uint32_t)identity.pid);
    path[used++] = '-';
    append_hex32(path, &used, identity.generation);
    for (size_t index = 0U; index < sizeof(suffix); ++index)
        path[used++] = suffix[index];
    return used <= BENCHMARK_PATH_CAPACITY ? 0 : -1;
}

static void prepare_disk_chunk(uint32_t chunk) {
    for (uint32_t index = 0U; index < BENCHMARK_DISK_CHUNK_BYTES; ++index)
        disk_buffer[index] = (uint8_t)(
            (chunk * 131U + index * 17U + (index >> 3U)) ^ 0xA5U);
}

static bool verify_disk_chunk(uint32_t chunk) {
    for (uint32_t index = 0U; index < BENCHMARK_DISK_CHUNK_BYTES; ++index) {
        uint8_t expected = (uint8_t)(
            (chunk * 131U + index * 17U + (index >> 3U)) ^ 0xA5U);
        if (disk_buffer[index] != expected) return false;
    }
    return true;
}

static void benchmark_disk(benchmark_result_t *write_result,
                           benchmark_result_t *read_result) {
    *write_result = (benchmark_result_t){0U, "KiB/s", BENCHMARK_FAILED};
    *read_result = (benchmark_result_t){0U, "KiB/s", BENCHMARK_FAILED};
    char path[BENCHMARK_PATH_CAPACITY];
    if (make_disk_path(path) != 0) return;
    x86os_puts("BENCHMARK_STATUS phase=hdd-create\n");
    /* CREATE is exclusive in the VFS: an existing generation path is never
     * opened or truncated, including when metadata lookup had an I/O fault. */
    int descriptor = x86os_create(path);
    if (descriptor < 0) {
        write_result->status = BENCHMARK_UNAVAILABLE;
        read_result->status = BENCHMARK_UNAVAILABLE;
        return;
    }

    bool write_ok = true;
    uint64_t started = 0U;
    uint64_t write_elapsed = 0U;
    uint64_t write_status_ms = 0U;
    x86os_puts(
        "BENCHMARK_STATUS phase=hdd-write progress_kib=0 total_kib=256\n");
    if (x86os_monotonic_ms(&started) != 0) write_ok = false;
    for (uint32_t chunk = 0U; chunk < BENCHMARK_DISK_CHUNKS && write_ok;
         ++chunk) {
        prepare_disk_chunk(chunk);
        write_ok = x86os_write(
            descriptor, disk_buffer, BENCHMARK_DISK_CHUNK_BYTES) ==
            (int)BENCHMARK_DISK_CHUNK_BYTES;
        uint32_t completed = chunk + 1U;
        if (write_ok &&
            completed % BENCHMARK_DISK_PROGRESS_CHUNKS == 0U) {
            const char *progress = disk_write_progress(completed);
            write_ok = progress != 0 &&
                timed_status(progress, &write_status_ms) == 0;
        }
    }
    if (write_ok) {
        write_ok = timed_status("BENCHMARK_STATUS phase=hdd-fsync\n",
                                &write_status_ms) == 0;
    }
    if (write_ok) write_ok = x86os_fsync(descriptor) == 0;
    uint64_t write_elapsed_with_status = 0U;
    if (write_ok) {
        write_ok = elapsed_ms(started, &write_elapsed_with_status) == 0 &&
                   write_elapsed_with_status >= write_status_ms;
    }
    if (write_ok) {
        write_elapsed = write_elapsed_with_status - write_status_ms;
        write_ok = write_elapsed != 0U;
    }
    if (write_ok) {
        write_result->hundredths = divide_unsigned(
            (uint64_t)BENCHMARK_DISK_BYTES * 100000U,
            write_elapsed * 1024U, 0);
        write_result->status = BENCHMARK_OK;
    }

    bool read_ok = write_ok;
    uint64_t read_status_ms = 0U;
    if (read_ok) {
        x86os_puts(
            "BENCHMARK_STATUS phase=hdd-read progress_kib=0 total_kib=256\n");
        read_ok = x86os_lseek(descriptor, 0, X86OS_SEEK_SET) == 0;
    }
    uint64_t read_elapsed = 0U;
    if (read_ok && x86os_monotonic_ms(&started) != 0) read_ok = false;
    for (uint32_t chunk = 0U; chunk < BENCHMARK_DISK_CHUNKS && read_ok;
         ++chunk) {
        read_ok = x86os_read(
            descriptor, disk_buffer, BENCHMARK_DISK_CHUNK_BYTES) ==
            (int)BENCHMARK_DISK_CHUNK_BYTES;
        if (read_ok) read_ok = verify_disk_chunk(chunk);
        uint32_t completed = chunk + 1U;
        if (read_ok &&
            completed % BENCHMARK_DISK_PROGRESS_CHUNKS == 0U) {
            const char *progress = disk_read_progress(completed);
            read_ok = progress != 0 &&
                timed_status(progress, &read_status_ms) == 0;
        }
    }
    uint64_t read_elapsed_with_status = 0U;
    if (read_ok) {
        read_ok = elapsed_ms(started, &read_elapsed_with_status) == 0 &&
                  read_elapsed_with_status >= read_status_ms;
    }
    if (read_ok) {
        read_elapsed = read_elapsed_with_status - read_status_ms;
        read_ok = read_elapsed != 0U;
    }
    if (read_ok) {
        read_result->hundredths = divide_unsigned(
            (uint64_t)BENCHMARK_DISK_BYTES * 100000U,
            read_elapsed * 1024U, 0);
        read_result->status = BENCHMARK_OK;
    }

    x86os_puts("BENCHMARK_STATUS phase=hdd-cleanup state=begin\n");
    bool cleanup_ok = x86os_close(descriptor) == 0;
    if (!cleanup_ok && read_result->status == BENCHMARK_OK)
        read_result->status = BENCHMARK_FAILED;
    if (x86os_unlink(path) != 0) {
        cleanup_ok = false;
        write_result->status = BENCHMARK_FAILED;
        read_result->status = BENCHMARK_FAILED;
    }
    x86os_puts(cleanup_ok
        ? "BENCHMARK_STATUS phase=hdd-cleanup state=complete\n"
        : "BENCHMARK_STATUS phase=hdd-cleanup state=failed\n");
}

static benchmark_result_t benchmark_vga(void) {
    benchmark_result_t result = {0U, "MPix/s", BENCHMARK_FAILED};
    x86os_display_info_t display;
    if (x86os_display_info(&display) != 0 || display.width == 0U ||
        display.height == 0U) {
        result.status = BENCHMARK_UNAVAILABLE;
        return result;
    }
    uint32_t frames = BENCHMARK_VGA_INITIAL_FRAMES;
    bool touched = false;
    for (uint32_t attempt = 0U; attempt < BENCHMARK_VGA_ATTEMPTS; ++attempt) {
        uint64_t started = 0U;
        if (x86os_monotonic_ms(&started) != 0) break;
        bool complete = true;
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            uint32_t color = (frame & 1U) != 0U
                ? 0x00104070U : 0x00703010U;
            if (x86os_fill_rect(
                    0, 0, display.width, display.height, color) != 0) {
                complete = false;
                break;
            }
            touched = true;
        }
        uint64_t elapsed = 0U;
        if (complete && elapsed_ms(started, &elapsed) == 0 && elapsed != 0U &&
            (elapsed >= BENCHMARK_TARGET_MS ||
             frames == BENCHMARK_VGA_MAX_FRAMES)) {
            uint64_t pixels =
                (uint64_t)display.width * display.height * frames;
            result.hundredths = divide_unsigned(
                pixels, elapsed * 10U, 0);
            result.status = BENCHMARK_OK;
            break;
        }
        if (!complete) break;
        if (frames >= BENCHMARK_VGA_MAX_FRAMES / 2U)
            frames = BENCHMARK_VGA_MAX_FRAMES;
        else
            frames *= 2U;
    }
    if (touched) x86os_clear();
    return result;
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        x86os_puts("Usage: benchmark\n");
        return 2;
    }
    x86os_puts("REIST Benchmark: begrenzte Diagnose laeuft ...\n");
    x86os_puts("BENCHMARK_STATUS phase=cpu\n");
    benchmark_result_t cpu = benchmark_cpu();
    x86os_puts("BENCHMARK_STATUS phase=ram-write\n");
    benchmark_result_t memory_write = benchmark_memory_write();
    x86os_puts("BENCHMARK_STATUS phase=ram-read\n");
    benchmark_result_t memory_read = benchmark_memory_read();
    benchmark_result_t disk_write;
    benchmark_result_t disk_read;
    benchmark_disk(&disk_write, &disk_read);
    x86os_puts("BENCHMARK_STATUS phase=vga\n");
    benchmark_result_t vga = benchmark_vga();

    x86os_puts("REIST OS System Benchmark\n");
    x86os_puts("+----------+----------------------+------------------+----------+\n");
    x86os_puts("| Bereich  | Test                 | Ergebnis         | Status   |\n");
    x86os_puts("+----------+----------------------+------------------+----------+\n");
    print_result_row("CPU", "Integer-Mix", &cpu);
    print_result_row("RAM", "Schreiben", &memory_write);
    print_result_row("RAM", "Lesen", &memory_read);
    print_result_row("HDD", "Seq. Schreiben", &disk_write);
    print_result_row("HDD", "Seq. Lesen", &disk_read);
    print_result_row("VGA", "Vollbild-Fill", &vga);
    x86os_puts("+----------+----------------------+------------------+----------+\n");
    x86os_puts("Hinweis: Vergleichswerte des aktuellen REIST-Treiberpfads.\n");
    x86os_puts("BENCHMARK_STATUS phase=complete\n");

    return cpu.status == BENCHMARK_OK &&
           memory_write.status == BENCHMARK_OK &&
           memory_read.status == BENCHMARK_OK &&
           disk_write.status == BENCHMARK_OK &&
           disk_read.status == BENCHMARK_OK &&
           vga.status == BENCHMARK_OK ? 0 : 1;
}
