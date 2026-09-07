#include "display_model.h"
#include "reist/vfs_file_client.h"
#include <string.h>

void display_mode_text(char output[16], uint32_t width, uint32_t height) {
    if (!width) { memcpy(output, "auto", 5U); return; }
    if (width > REIST_DISPLAY_MODE_MAX_DIMENSION ||
        height > REIST_DISPLAY_MODE_MAX_DIMENSION) {
        memcpy(output, "unbekannt", 10U); return;
    }
    char reversed[4]; uint32_t used = 0U, length = 0U;
    do { reversed[length++] = (char)('0' + width % 10U); width /= 10U; } while (width);
    while (length) output[used++] = reversed[--length];
    output[used++] = 'x';
    do { reversed[length++] = (char)('0' + height % 10U); height /= 10U; } while (height);
    while (length) output[used++] = reversed[--length];
    output[used] = '\0';
}

static int read_setting(display_model_t *model, uint32_t *width, uint32_t *height) {
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open_rights("/etc/reist/desktop.conf",
        REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &handle);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_file_fstat(handle, &info);
    if (status == 0 && (info.type != X86OS_FILE ||
        info.size > sizeof(model->config_bytes))) status = -75;
    size_t used = 0U;
    while (status == 0 && used < info.size) {
        size_t count = info.size - used;
        if (count > X86OS_VFS_SHADOW_READ_CAPACITY) count = X86OS_VFS_SHADOW_READ_CAPACITY;
        int amount = reist_vfs_file_read(handle, model->config_bytes + used, count);
        if (amount <= 0 || (size_t)amount > count) status = amount < 0 ? amount : -5;
        else used += (size_t)amount;
    }
    char extra;
    if (status == 0 && reist_vfs_file_read(handle, &extra, 1U) != 0) status = -75;
    int closed = reist_vfs_file_close(handle);
    if (status == 0) status = closed;
    if (status == 0) status = reist_config_parse(model->config_bytes, used,
                                               "reist.desktop/1", &model->config);
    if (status == 0) status = reist_display_setting_parse(
        reist_config_get(&model->config, "resolution"), width, height);
    return status;
}

static void add_choice(display_model_t *model, uint32_t width, uint32_t height) {
    if (model->count >= DISPLAY_CHOICE_CAPACITY ||
        (width && !reist_display_mode_supported(width, height, &model->caps))) return;
    for (uint32_t i = 0; i < model->count; ++i)
        if (model->choices[i].width == width && model->choices[i].height == height) return;
    display_choice_t *choice = &model->choices[model->count++];
    choice->width = width; choice->height = height;
    display_mode_text(choice->value, width, height);
}

void display_model_initialize(display_model_t *model) {
    static const uint32_t modes[][2] = {
        {800,600}, {1024,768}, {1152,864}, {1280,720}, {1280,800}, {1280,1024},
        {1360,768}, {1440,900}, {1600,900}, {1680,1050}, {1920,1080},
        {1920,1200}, {2560,1440}, {2560,1600}
    };
    memset(model, 0, sizeof(*model));
    model->status = "Nur Lesen: Anzeige nicht verfuegbar";
    int status = x86os_display_mode_query(&model->caps);
    add_choice(model, 0U, 0U);
    if (status != 0 || !reist_display_mode_supported(
            model->caps.width, model->caps.height, &model->caps)) return;
    for (uint32_t i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i)
        add_choice(model, modes[i][0], modes[i][1]);
    add_choice(model, model->caps.width, model->caps.height);
    model->status = "Nur Lesen: Konfiguration ungueltig";
    if (read_setting(model, &model->saved_width, &model->saved_height) != 0) return;
    add_choice(model, model->saved_width, model->saved_height);
    model->writable = 1U;
    model->status = "Wirksam beim naechsten Desktopstart";
    for (uint32_t i = 0; i < model->count; ++i) {
        if (model->choices[i].width == model->saved_width &&
            model->choices[i].height == model->saved_height) {
            model->selected = i; return;
        }
    }
    model->status = "Gespeicherter Modus nicht verfuegbar";
}

int display_model_save(display_model_t *model) {
    if (model->child > 0 || !model->writable || model->selected >= model->count) return -16;
    if (x86os_monotonic_ms(&model->started_ms) != 0) return -5;
    const char *args[] = {"/sbin/config.prg", "set", "desktop", "resolution",
                          model->choices[model->selected].value};
    int child = x86os_spawnv(args[0], 5, args);
    if (child <= 0) { model->status = "Speichern verweigert"; return child ? child : -5; }
    /* Only this parent reaps this PID. Capture a generation on the first live
     * observation; an immediately exited child needs no identity to be reaped. */
    model->child = child; model->child_generation = 0U; model->cancel_sent = 0U;
    model->pending_choice = model->selected;
    model->status = "Speichern ...";
    return 0;
}

int display_model_cancel(display_model_t *model) {
    if (model->child <= 0) return 0;
    if (model->cancel_sent) return -11;
    x86os_process_identity_t identity;
    int status = x86os_process_identity_of(model->child, &identity);
    if (status == -3) { (void)display_model_poll(model); return model->child > 0 ? -11 : 0; }
    model->cancel_sent = 1U;
    if (status != 0 || identity.version != 1U || identity.struct_size != sizeof(identity) ||
        identity.pid != model->child || !identity.generation ||
        (model->child_generation && model->child_generation != identity.generation)) return -13;
    model->child_generation = identity.generation;
    status = x86os_kill(model->child);
    model->status = status == 0 ? "Speichern abgebrochen" : "Abbruch verweigert";
    return status;
}

int display_model_poll(display_model_t *model) {
    if (model->child <= 0) return 0;
    x86os_process_identity_t identity;
    int status = x86os_process_identity_of(model->child, &identity);
    if (status == -3) {
        int exit_status = -1;
        int child = model->child;
        int waited = x86os_wait(child, &exit_status); /* observed exited; cannot block */
        model->child = 0;
        uint32_t width = 0U, height = 0U;
        if (waited == child && !exit_status && !model->cancel_sent &&
            read_setting(model, &width, &height) == 0 &&
            width == model->choices[model->pending_choice].width &&
            height == model->choices[model->pending_choice].height) {
            model->saved_width = width; model->saved_height = height;
            model->status = "Gespeichert: naechster Desktopstart";
            x86os_puts("DISPLAY_SETTINGS_SAVED ");
            x86os_puts(model->choices[model->pending_choice].value); x86os_putchar('\n');
        } else model->status = "Nicht bestaetigt: bitte neu oeffnen";
        return 1;
    }
    if (status == 0 && identity.version == 1U && identity.struct_size == sizeof(identity) &&
        identity.pid == model->child && identity.generation &&
        (!model->child_generation || model->child_generation == identity.generation)) {
        model->child_generation = identity.generation;
    } else {
        model->status = "Prozessidentitaet nicht bestaetigt";
        model->writable = 0U;
    }
    uint64_t now = 0U;
    if (!model->cancel_sent && (x86os_monotonic_ms(&now) != 0 ||
        now < model->started_ms || now - model->started_ms >= DISPLAY_SAVE_DEADLINE_MS)) {
        (void)display_model_cancel(model);
        return 1;
    }
    return 0;
}
