#include "kernel/shell/path_resolver.h"

#include "lib/libc/string.h"

static bool shell_path_separator(char value) {
    return value == '/' || value == '\\';
}

static bool shell_path_drive_kind(const char* value) {
    if (!value) return false;
    if (value[0] == '\0' || value[1] == '\0' || value[2] == '\0' ||
        value[3] == '\0') {
        return false;
    }
    return ((tolower((unsigned char)value[0]) == 'h' &&
             tolower((unsigned char)value[1]) == 'd' &&
             tolower((unsigned char)value[2]) == 'd') ||
            (tolower((unsigned char)value[0]) == 'f' &&
             tolower((unsigned char)value[1]) == 'd' &&
             tolower((unsigned char)value[2]) == 'd')) &&
           value[3] >= '0' && value[3] <= '9';
}

static void shell_path_copy_drive(char output[SHELL_DRIVE_SELECTOR_MAX],
                                  const char* input, size_t length) {
    size_t copy_length = length;
    if (copy_length >= SHELL_DRIVE_SELECTOR_MAX) {
        copy_length = SHELL_DRIVE_SELECTOR_MAX - 1;
    }
    for (size_t i = 0; i < copy_length; ++i) {
        output[i] = (char)tolower((unsigned char)input[i]);
    }
    output[copy_length] = '\0';
}

shell_path_result_t shell_path_split_drive(
    const char* input,
    char selector[SHELL_DRIVE_SELECTOR_MAX],
    const char** remainder) {
    if (!input || !selector || !remainder) return SHELL_PATH_INVALID;

    selector[0] = '\0';
    *remainder = input;

    /* DOS drive letter, e.g. C:, C:\\DIR or C:DIR. */
    if (isalpha((unsigned char)input[0]) && input[1] == ':') {
        selector[0] = (char)toupper((unsigned char)input[0]);
        selector[1] = '\0';
        *remainder = input + 2;
        return SHELL_PATH_OK;
    }

    /* Native drive name, e.g. hdd0:/DIR. */
    if (strlen(input) >= 5 && shell_path_drive_kind(input) &&
        input[4] == ':') {
        shell_path_copy_drive(selector, input, 4);
        *remainder = input + 5;
        return SHELL_PATH_OK;
    }

    /* Backwards-compatible /hdd0/DIR (and DOS-style \\hdd0\\DIR). */
    if (shell_path_separator(input[0]) && strlen(input) >= 5 &&
        shell_path_drive_kind(input + 1) &&
        (input[5] == '\0' || shell_path_separator(input[5]))) {
        shell_path_copy_drive(selector, input + 1, 4);
        *remainder = input[5] == '\0' ? "/" : input + 5;
    }

    return SHELL_PATH_OK;
}

static shell_path_result_t shell_path_apply(
    char output[SHELL_PATH_MAX], size_t* output_length,
    const char* path, bool reset) {
    if (!output || !output_length || !path) return SHELL_PATH_INVALID;

    if (reset) {
        output[0] = '/';
        output[1] = '\0';
        *output_length = 1;
    }

    const char* cursor = path;
    while (*cursor != '\0') {
        while (shell_path_separator(*cursor)) ++cursor;
        if (*cursor == '\0') break;

        const char* segment = cursor;
        while (*cursor != '\0' && !shell_path_separator(*cursor)) ++cursor;
        size_t segment_length = (size_t)(cursor - segment);

        if (segment_length == 1 && segment[0] == '.') continue;
        if (segment_length == 2 && segment[0] == '.' && segment[1] == '.') {
            if (*output_length > 1) {
                while (*output_length > 1 &&
                       output[*output_length - 1] != '/') {
                    --(*output_length);
                }
                if (*output_length > 1) --(*output_length);
                output[*output_length] = '\0';
            }
            continue;
        }

        size_t separator_length = *output_length > 1 ? 1 : 0;
        if (*output_length > (SHELL_PATH_MAX - 1) - separator_length) {
            return SHELL_PATH_TOO_LONG;
        }
        size_t available = (SHELL_PATH_MAX - 1) - *output_length -
                           separator_length;
        if (segment_length > available) {
            return SHELL_PATH_TOO_LONG;
        }
        if (separator_length != 0) output[(*output_length)++] = '/';
        for (size_t i = 0; i < segment_length; ++i) {
            output[(*output_length)++] = segment[i];
        }
        output[*output_length] = '\0';
    }

    return SHELL_PATH_OK;
}

shell_path_result_t shell_path_normalize(
    const char* current_path,
    const char* input,
    char output[SHELL_PATH_MAX]) {
    if (!current_path || !input || !output || current_path[0] != '/') {
        return SHELL_PATH_INVALID;
    }

    size_t output_length = 0;
    shell_path_result_t result = shell_path_apply(
        output, &output_length, current_path, true);
    if (result != SHELL_PATH_OK) return result;

    bool absolute = shell_path_separator(input[0]);
    return shell_path_apply(output, &output_length, input, absolute);
}

shell_path_result_t shell_path_join_mount(
    const char* mount_point,
    const char* drive_path,
    char output[SHELL_PATH_MAX]) {
    if (!mount_point || !drive_path || !output ||
        mount_point[0] != '/' || drive_path[0] != '/') {
        return SHELL_PATH_INVALID;
    }

    size_t mount_length = strlen(mount_point);
    size_t path_length = strlen(drive_path);
    if (mount_length == 0 || mount_length >= SHELL_PATH_MAX ||
        path_length == 0 || path_length >= SHELL_PATH_MAX ||
        (mount_length > 1 && mount_point[mount_length - 1] == '/')) {
        return SHELL_PATH_INVALID;
    }

    if (mount_length == 1) {
        if (path_length >= SHELL_PATH_MAX) return SHELL_PATH_TOO_LONG;
        strcpy(output, drive_path);
        return SHELL_PATH_OK;
    }

    if (path_length == 1) {
        strcpy(output, mount_point);
        return SHELL_PATH_OK;
    }

    if (mount_length + path_length >= SHELL_PATH_MAX) {
        return SHELL_PATH_TOO_LONG;
    }
    strcpy(output, mount_point);
    strcpy(output + mount_length, drive_path);
    return SHELL_PATH_OK;
}

shell_path_result_t shell_path_to_dos(
    const char* drive_path,
    char output[SHELL_PATH_MAX]) {
    if (!drive_path || !output || drive_path[0] != '/') {
        return SHELL_PATH_INVALID;
    }
    size_t length = strlen(drive_path);
    if (length >= SHELL_PATH_MAX) return SHELL_PATH_TOO_LONG;
    for (size_t i = 0; i <= length; ++i) {
        output[i] = drive_path[i] == '/' ? '\\' : drive_path[i];
    }
    return SHELL_PATH_OK;
}
