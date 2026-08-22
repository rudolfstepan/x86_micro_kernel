/**
 * @file config_service.c
 * @brief Privilege boundary for bounded system-configuration mutations.
 *
 * Each invocation is a fresh Ring-3 process generation. It accepts one
 * validated mutation, reads and validates the complete current file, writes a
 * same-directory temporary file, fsyncs it, closes it and atomically renames
 * it over the old file. No partially validated state is published.
 */
#include "x86os.h"
#include "reist/config.h"
#include "config_service.h"

#include <stddef.h>
#include <stdint.h>

typedef struct config_target {
    const char *name;
    const char *path;
    const char *schema;
    const char *temp_prefix;
} config_target_t;

static const config_target_t targets[] = {
    {"system", "/etc/reist/system.conf", "reist.system/1", "RSC"},
    {"input", "/etc/reist/input.conf", "reist.input/1", "RIC"},
    {"desktop", "/etc/reist/desktop.conf", "reist.desktop/1", "RDC"},
};

static char read_buffer[CONFIG_WRITE_CAPACITY];
static char write_buffer[CONFIG_WRITE_CAPACITY];
static reist_config_document_t document;

static size_t text_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0U;
    while (index < REIST_CONFIG_VALUE_CAPACITY &&
           left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < REIST_CONFIG_VALUE_CAPACITY &&
           left[index] == '\0' && right[index] == '\0';
}

static uint32_t one_of(const char *value, const char *first,
                       const char *second, const char *third) {
    return text_equal(value, first) || text_equal(value, second) ||
           (third != 0 && text_equal(value, third));
}

static int parse_unsigned(const char *value, uint32_t minimum,
                          uint32_t maximum) {
    size_t length = text_length(value, 11U);
    if (length == 0U || length >= 11U) return -22;
    uint32_t number = 0U;
    for (size_t index = 0U; index < length; ++index) {
        if (value[index] < '0' || value[index] > '9') return -22;
        uint32_t digit = (uint32_t)(value[index] - '0');
        if (number > (UINT32_MAX - digit) / 10U) return -22;
        number = number * 10U + digit;
    }
    return number >= minimum && number <= maximum ? 0 : -22;
}

static int validate_setting(const config_target_t *target,
                            const char *key, const char *value) {
    if (target == 0 || key == 0 || value == 0) return -22;
    if (text_equal(target->name, "system")) {
        if (text_equal(key, "locale") || text_equal(key, "fallback_locale"))
            return one_of(value, "de_AT", "en_US", "de_DE") ? 0 : -22;
        if (text_equal(key, "timezone"))
            return one_of(value, "Europe/Vienna", "UTC",
                          "Europe/Berlin") ? 0 : -22;
        return -22;
    }
    if (text_equal(target->name, "input")) {
        if (text_equal(key, "keyboard.layout"))
            return one_of(value, "de", "us", "at") ? 0 : -22;
        if (text_equal(key, "keyboard.variant"))
            return one_of(value, "default", "nodeadkeys", 0) ? 0 : -22;
        if (text_equal(key, "keyboard.repeat_delay_ms"))
            return parse_unsigned(value, 200U, 2000U);
        if (text_equal(key, "keyboard.repeat_rate_hz"))
            return parse_unsigned(value, 2U, 50U);
        if (text_equal(key, "mouse.primary_button"))
            return one_of(value, "left", "right", 0) ? 0 : -22;
        if (text_equal(key, "mouse.speed_percent"))
            return parse_unsigned(value, 25U, 200U);
        if (text_equal(key, "mouse.acceleration"))
            return one_of(value, "adaptive", "flat", "off") ? 0 : -22;
        if (text_equal(key, "mouse.natural_scroll"))
            return one_of(value, "false", "true", 0) ? 0 : -22;
        if (text_equal(key, "mouse.double_click_ms"))
            return parse_unsigned(value, 200U, 1000U);
        return -22;
    }
    if (text_equal(target->name, "desktop")) {
        if (text_equal(key, "theme"))
            return one_of(value, "classic", "contrast", 0) ? 0 : -22;
        if (text_equal(key, "show_hidden"))
            return one_of(value, "false", "true", 0) ? 0 : -22;
        if (text_equal(key, "folder_open_mode"))
            return one_of(value, "new-window", "same-window", 0) ? 0 : -22;
        if (text_equal(key, "icon_size"))
            return one_of(value, "small", "medium", "large") ? 0 : -22;
    }
    return -22;
}

static const config_target_t *find_target(const char *name) {
    for (uint32_t index = 0U;
         index < sizeof(targets) / sizeof(targets[0]); ++index)
        if (text_equal(name, targets[index].name)) return &targets[index];
    return 0;
}

static int read_document(const config_target_t *target) {
    int descriptor = x86os_open(target->path);
    if (descriptor < 0) return descriptor;
    size_t used = 0U;
    while (used < sizeof(read_buffer)) {
        int amount = x86os_read(
            descriptor, read_buffer + used, sizeof(read_buffer) - used);
        if (amount < 0) {
            (void)x86os_close(descriptor);
            return amount;
        }
        if (amount == 0) break;
        used += (size_t)amount;
    }
    char extra = 0;
    int extra_status = x86os_read(descriptor, &extra, 1U);
    int close_status = x86os_close(descriptor);
    if (extra_status != 0 || close_status != 0) return -75;
    return reist_config_parse(read_buffer, used, target->schema, &document);
}

static int write_all(int descriptor, const char *data, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        int amount = x86os_write(descriptor, data + offset, length - offset);
        if (amount <= 0) return -5;
        offset += (size_t)amount;
    }
    return 0;
}

static int make_temp_path(const config_target_t *target, char temp_path[32]) {
    static const char prefix[] = "/etc/reist/";
    size_t used = 0U;
    for (size_t index = 0U; index < sizeof(prefix) - 1U; ++index)
        temp_path[used++] = prefix[index];
    for (size_t index = 0U; index < 3U; ++index)
        temp_path[used++] = target->temp_prefix[index];
    uint32_t pid = (uint32_t)x86os_getpid();
    for (uint32_t digit = 0U; digit < 5U; ++digit) {
        temp_path[used + 4U - digit] = (char)('0' + pid % 10U);
        pid /= 10U;
    }
    used += 5U;
    temp_path[used++] = '.';
    temp_path[used++] = 'T';
    temp_path[used++] = 'M';
    temp_path[used++] = 'P';
    temp_path[used] = '\0';
    return 0;
}

static int persist_document(const config_target_t *target) {
    size_t length = 0U;
    if (reist_config_serialize(&document, write_buffer,
                               sizeof(write_buffer), &length) != 0)
        return -75;
    char temp_path[32];
    (void)make_temp_path(target, temp_path);
    (void)x86os_unlink(temp_path);
    int descriptor = x86os_create(temp_path);
    if (descriptor < 0) return descriptor;
    int write_status = write_all(descriptor, write_buffer, length);
    int sync_status = write_status == 0 ? x86os_fsync(descriptor) : -5;
    int close_status = x86os_close(descriptor);
    if (write_status != 0 || sync_status != 0 || close_status != 0 ||
        x86os_rename(temp_path, target->path) != 0) {
        (void)x86os_unlink(temp_path);
        return -5;
    }
    return 0;
}

int reist_config_service_main(int argc, char **argv) {
    if (argc != 5 || argv == 0 || !text_equal(argv[1], "set")) {
        x86os_puts("Usage: config set <system|input|desktop> <key> <value>\n");
        return 2;
    }
    const config_target_t *target = find_target(argv[2]);
    if (target == 0 || validate_setting(target, argv[3], argv[4]) != 0) {
        x86os_puts("config: setting rejected\n");
        return 2;
    }
    int status = read_document(target);
    if (status == 0) status = reist_config_set(&document, argv[3], argv[4]);
    if (status == 0) status = persist_document(target);
    if (status != 0) {
        x86os_puts("config: update failed\n");
        return 1;
    }
    x86os_puts("CONFIG_UPDATE_OK\n");
    return 0;
}

int main(int argc, char **argv) {
    return reist_config_service_main(argc, argv);
}
