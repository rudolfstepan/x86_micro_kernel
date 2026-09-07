/* Ring-3 browser: bounded semantic HTML, immutable image underlay and retained
 * text/chrome. CURL alone owns network/TLS authority. Scripts remain inert. */
#include "x86os.h"
#include "reist/gui/font_catalog.h"
#include "reist/gui/surface_client.h"
#include "reist/vfs_file_client.h"
#include "browser_model.h"
#include "browser_images.h"
#include "browser_response.h"
#include "html_protocol.h"
#include "browser_scene.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define BROWSER_DOCUMENT_LIMIT BROWSER_DOCUMENT_INPUT_CAPACITY
#define BROWSER_URL_CAPACITY 256U
#define BROWSER_LINK_HIT_CAPACITY 128U
#define BROWSER_READ_CHUNK 131072U
#define BROWSER_CREATE_ATTEMPTS 250U
#define BROWSER_VISIBLE_RUN_BUDGET 150U
#define BROWSER_EVENT_BATCH_LIMIT 32U
#define BROWSER_PAGE_IMAGE_DEADLINE_MS 30000U
#define BROWSER_CHILD_REAP_MS 1000U
#define BROWSER_KEY_ESCAPE 0x101U
#define BROWSER_KEY_UP 0x102U
#define BROWSER_KEY_DOWN 0x103U
#define BROWSER_KEY_HOME 0x106U
#define BROWSER_KEY_END 0x107U
#define BROWSER_KEY_PAGE_UP 0x109U
#define BROWSER_KEY_PAGE_DOWN 0x10AU
typedef struct browser_link_hit {
    reist_gui_rect_t rect;
    uint32_t link_index;
} browser_link_hit_t;
typedef struct browser_state {
    uint32_t active, loaded, redraw, chrome_redraw, status_redraw, exit_requested;
    uint32_t address_focused, address_replace_pending, address_length, address_cursor, address_start;
    uint32_t scroll_y, painted_scroll, armed_link, probe, probe_phase;
    uint32_t buffer_id, buffer_generation, body_frames, chrome_frames;
    uint32_t buffer_width, buffer_height;
    browser_scrollbar_t scrollbar;
    char address[BROWSER_URL_CAPACITY], active_url[BROWSER_URL_CAPACITY];
    char temporary_path[40U], status[64U];
    uint32_t fetch_endpoint, fetch_received, fetch_total, load_progress;
    browser_html_header_t html_request;
    x86os_ipc_handle_t css_endpoint;
    uint32_t css_sent, css_received, css_total, reflow_pending, reflow_job, active_length;
    uint32_t probe_resize_width;
    int32_t wheel_remainder;
    uint32_t probe_wheel_down, probe_wheel_up;
    uint32_t input_backspace, input_left, input_right, input_keys;
    uint32_t scene_image_sizes[BROWSER_IMAGE_CACHE_COUNT][2];
    uint32_t parse_pending, parse_mode, parser_failures, parser_timeouts;
    uint32_t resource_generation, resource_loading, resource_deadline, resource_document_length;
    char resource_document_url[BROWSER_URL_CAPACITY];
    browser_link_hit_t hits[BROWSER_LINK_HIT_CAPACITY];
    uint32_t hit_count;
    char pending_url[BROWSER_URL_CAPACITY], job_url[BROWSER_RESOURCE_URL_CAPACITY];
    uint32_t pending, job_kind, job_image, image_next, image_deadline, job_deadline;
    uint32_t job_cancelled, child_generation, poll_at, child_reap_deadline;
    uint32_t redirect_count, redirect_deadline, follow_redirect, response_status;
    uint32_t document_encoding,active_encoding,transport_retries;
    int child_pid;
    int exit_error, child_check, child_check_status;
    const char *exit_reason;
} browser_state_t;
static browser_html_reply_t html_reply;
static browser_resource_needs_t resource_needs;
/* Explicit probe-only files, never derived from web content. */
static char resource_probe_html[48], resource_probe_css[48];
static uint32_t resource_probe_files, resource_probe_generation, resource_probe_failures;
/* Conventional read-only asset embedding, as used by the desktop splash. */
#if !__STDC_HOSTED__
__asm__(".pushsection .rodata.browser_font,\"a\",@progbits\n"
        ".global browser_font_data\nbrowser_font_data:\n"
        ".incbin \"assets/fonts/reist-unicode.psf\"\n"
        ".global browser_font_end\nbrowser_font_end:\n.popsection\n");
#endif
extern const uint8_t browser_font_data[], browser_font_end[];
typedef struct browser_workspace {
    uint8_t page_bytes[BROWSER_DOCUMENT_LIMIT+REIST_CURL_HEADER_CAPACITY];
    uint8_t page_active[BROWSER_DOCUMENT_LIMIT];
    struct { browser_css_request_t request; uint8_t bytes[BROWSER_CSS_INPUT_BYTES]; } css_request;
    uint8_t input_bytes[BROWSER_CSS_WIRE_CAPACITY+BROWSER_IMAGE_INPUT_LIMIT+REIST_CURL_HEADER_CAPACITY];
    browser_scene_t scenes[2];
    browser_form_state_t forms[2];
    uint32_t forms_redraw;
    uint32_t decoded[BROWSER_IMAGE_PIXEL_LIMIT];
    uint32_t surface[REIST_GUI_SURFACE_MAX_WIDTH * REIST_GUI_SURFACE_MAX_HEIGHT];
    browser_image_slot_t images[2U][BROWSER_IMAGE_CACHE_COUNT];
    _Alignas(8) uint64_t arena[BROWSER_DECODE_ARENA_BYTES / sizeof(uint64_t)];
    reist_gui_font_mapping_t font_map[262144];
    reist_gui_font_t font;
    browser_resources_t resources[2];
} browser_workspace_t;
/* i386 aligns uint64_t members to four bytes; the decoder requires eight.
 * Keep the requirement explicit as private document buffers change size. */
_Static_assert(offsetof(browser_workspace_t,arena)%8U==0U,"decoder arena alignment");
_Static_assert(sizeof(browser_workspace_t)<=36U*1024U*1024U,"private workspace quota");
static browser_workspace_t *workspace;
static struct { uint32_t generation,width,request,frames,rejected,resets,failures,limit_refusals; } form_probe;
/* Private measurement selector on the existing input probe. A commit reply
 * confirms Surface admission, NOT scanout; the host must also observe pixels.
 * One real device event in flight, no synthesized edits or scroll operations. */
static struct { uint32_t ordinal,pending,kind,body,chrome; } model_probe;
#define decoded_pixels (workspace->decoded)
#define surface_pixels (workspace->surface)
#define image_cache (workspace->images)
#define image_bytes (workspace->input_bytes)
#define scenes (workspace->scenes)
#define document_bytes (workspace->page_bytes)
#define active_html (workspace->page_active)
#define css_input (workspace->css_request)
static reist_html_document_t documents[2U];
static browser_layout_t layouts[2U];
static void finish_load_turn(const browser_state_t *state,uint32_t processed,uint32_t progress) {
    /* Successful bounded IPC packets already yield to the peer. Do not add an
     * idle timer wait just because no keyboard/mouse event accompanied them.
     * Empty/full queues make no progress and must still sleep, never spin. */
    if(processed || state->load_progress!=progress) return;
    uint32_t images=documents[state->active].image_count;
    uint32_t images_done=state->image_next>=images || state->image_next>=BROWSER_IMAGE_CACHE_COUNT;
    /* These local service_loads branches consume a queued transition or one
     * bounded image slot on the next turn; they do not wait for a peer. Give
     * other tasks one scheduling opportunity without an unnecessary timer
     * delay. A live/cancelling child still takes the original idle path. */
    if(!state->exit_requested && state->child_pid<=0 &&
       (state->pending || state->parse_pending || state->follow_redirect || state->resource_loading ||
        (state->loaded && (state->reflow_pending || !images_done)))) {
        (void)x86os_yield();
        return;
    }
    (void)x86os_sleep_ms(1U);
}

/* Probe-only counters; no extra clock syscalls in normal browser operation.
 * Totals overlap intentionally (body includes pixels); each stage is named. */
enum { TIME_READ, TIME_DECODE, TIME_RASTER, TIME_BUFFER, TIME_PIXEL_IPC,
       TIME_BODY, TIME_CHROME, TIME_STATUS, TIME_SPAWN, TIME_COUNT };
static uint32_t timing_enabled;
static struct { uint32_t calls,total,maximum; } timings[TIME_COUNT];
static uint32_t timing_start(void) { return timing_enabled ? x86os_uptime_ms() : 0; }
static void timing_end(unsigned stage,uint32_t start) {
    if (!timing_enabled || stage>=TIME_COUNT) return;
    uint32_t elapsed=x86os_uptime_ms()-start;
    if (elapsed>INT32_MAX) elapsed=INT32_MAX;
    if (timings[stage].calls<INT32_MAX) ++timings[stage].calls;
    if (elapsed>timings[stage].maximum) timings[stage].maximum=elapsed;
    timings[stage].total=elapsed>INT32_MAX-timings[stage].total
        ? INT32_MAX : timings[stage].total+elapsed;
}
static void timing_dump(void) {
    static const char *const names[]={"read","decode","raster","buffer","pixel-ipc",
        "body","chrome","status","spawn"};
    if (!timing_enabled) return;
    timing_enabled=0;
    for (unsigned i=0;i<TIME_COUNT;++i) {
        x86os_puts("BROWSER_TIMING stage="); x86os_puts(names[i]);
        x86os_puts(" calls="); x86os_print_number((int)timings[i].calls);
        x86os_puts(" total_ms="); x86os_print_number((int)timings[i].total);
        x86os_puts(" max_ms="); x86os_print_number((int)timings[i].maximum); x86os_puts("\n");
    }
}

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static size_t utf8_prefix_length(const char *text, size_t maximum) {
    size_t length = bounded_length(text, maximum);
    while (length > 0U && ((uint8_t)text[length] & 0xC0U) == 0x80U) --length;
    return length;
}

static int text_prefix(const char *text, const char *prefix) {
    size_t index = 0U;
    if (text == 0 || prefix == 0) return 0;
    while (prefix[index] != '\0') {
        char value = text[index];
        if (value >= 'A' && value <= 'Z') value += (char)('a' - 'A');
        if (value != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static int text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0;
    while (index < BROWSER_URL_CAPACITY && left[index] != '\0' &&
           right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return index < BROWSER_URL_CAPACITY && left[index] == right[index];
}

static int copy_text(char *target, size_t capacity, const char *source) {
    size_t length = bounded_length(source, capacity);
    if (length >= capacity) return -28;
    for (size_t index = 0U; index <= length; ++index)
        target[index] = source[index];
    return 0;
}

static int make_temporary_path(browser_state_t *state) {
    static const char prefix[] = "/browser-";
    static const char suffix[] = ".tmp";
    uint32_t used = 0U;
    for (uint32_t index = 0U; prefix[index] != '\0'; ++index)
        state->temporary_path[used++] = prefix[index];
    uint32_t pid = (uint32_t)x86os_getpid();
    char digits[10U]; uint32_t count = 0U;
    do { digits[count++] = (char)('0' + pid % 10U); pid /= 10U; }
    while (pid != 0U && count < sizeof(digits));
    while (count != 0U) state->temporary_path[used++] = digits[--count];
    for (uint32_t index = 0U; suffix[index] != '\0'; ++index)
        state->temporary_path[used++] = suffix[index];
    state->temporary_path[used] = '\0';
    return 0;
}

static int strip_fragment(const char *url, char *target, size_t capacity) {
    size_t used = 0U;
    while (url[used] != '\0' && url[used] != '#') {
        if (used + 1U >= capacity) return -28;
        target[used] = url[used];
        ++used;
    }
    target[used] = '\0';
    return used == 0U ? -22 : 0;
}

static int read_file_kind(const char *path, uint8_t *bytes, uint32_t limit, uint32_t *length, uint32_t empty) {
    uint32_t measured_at=timing_start();
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open_rights(
        path, REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &handle);
    if (status != 0) { timing_end(TIME_READ,measured_at); return status; }
    x86os_file_info_t info;
    status = reist_vfs_file_fstat(handle, &info);
    if (status == 0 && (info.type != X86OS_FILE || (!empty && info.size == 0U) ||
                        info.size > limit)) status = -27;
    uint32_t used = 0U;
    while (status == 0 && used < info.size) {
        uint32_t amount = info.size - used;
        if (amount > BROWSER_READ_CHUNK) amount = BROWSER_READ_CHUNK;
        int count = reist_vfs_file_read_bulk(
            handle, bytes + used, amount);
        if (count <= 0 || (uint32_t)count > amount) {
            status = count < 0 ? count : -5;
            break;
        }
        used += (uint32_t)count;
    }
    int close_status = reist_vfs_file_close(handle);
    if (status == 0 && close_status != 0) status = close_status;
    if (status == 0 && used != info.size) status = -5;
    if (status == 0) *length = used;
    timing_end(TIME_READ,measured_at);
    return status;
}

static int read_file(const char *path, uint8_t *bytes, uint32_t limit, uint32_t *length) {
    return read_file_kind(path,bytes,limit,length,0);
}
/* Candidate resources never alias the displayed page. Immutable IPC bytes
 * remain owned until the cancelled child has been reaped exactly. */
static void abandon_resources(browser_state_t *state) {
    state->resource_loading=state->resource_document_length=0;
    state->parse_pending=state->reflow_job=state->reflow_pending=0;
}
static uint32_t viewport_height(const reist_gui_surface_client_t *client) {
    return client->height > BROWSER_CONTENT_TOP + BROWSER_STATUS_HEIGHT
        ? client->height - BROWSER_CONTENT_TOP - BROWSER_STATUS_HEIGHT : 1U;
}

static uint32_t maximum_scroll(const browser_state_t *state,
                               const reist_gui_surface_client_t *client) {
    if (!state->loaded) return 0U;
    uint32_t view = viewport_height(client);
    uint32_t total = layouts[state->active].total_height;
    return total > view ? total - view : 0U;
}

static void set_scroll(browser_state_t *state,
                       const reist_gui_surface_client_t *client,
                       int64_t desired) {
    uint32_t maximum = maximum_scroll(state, client);
    if (desired < 0) desired = 0;
    if ((uint64_t)desired > maximum) desired = maximum;
    if (state->scroll_y != (uint32_t)desired) {
        state->scroll_y = (uint32_t)desired;
        state->armed_link = UINT32_MAX;
        state->hit_count = 0U;
        state->redraw = 1U;
    }
}

static int render_result(const char *stage, int result) {
    if (result != 0) {
        x86os_puts("BROWSER_RENDER_ERROR stage="); x86os_puts(stage);
        x86os_puts(" code="); x86os_print_number(result); x86os_puts("\n");
    }
    return result;
}

static int paint_text(reist_gui_surface_client_t *client,
                      int32_t x, int32_t y, uint32_t width,
                      const char *text, uint32_t length,
                      uint32_t foreground, uint32_t background,
                      uint32_t height) {
    if (length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY) return -28;
    int result = reist_gui_surface_client_paint_font_text(
        client, x, y, width, text, length, foreground, background,
        REIST_GUI_FONT_FAMILY_UNIFONT, height);
    if (result != 0) {
        x86os_puts("BROWSER_TEXT_ERROR x="); x86os_print_number(x);
        x86os_puts(" y="); x86os_print_number(y);
        x86os_puts(" w="); x86os_print_number((int)width);
        x86os_puts(" h="); x86os_print_number((int)height);
        x86os_puts(" bytes="); x86os_print_number((int)length);
        x86os_puts(" code="); x86os_print_number(result); x86os_puts("\n");
    }
    return result;
}


static void set_status(browser_state_t *state, const char *status) {
    if (copy_text(state->status, sizeof(state->status), status) != 0) state->status[0] = '\0';
    state->status_redraw = 1U;
}
static uint32_t is_network(const char *url) {
    return text_prefix(url, "http://") || text_prefix(url, "https://");
}
static void browser_runtime_failure(browser_state_t *state,const char *reason,int error) {
    state->exit_requested=1;
    if(state->exit_error) return;
    state->exit_error=error ? error : -5; state->exit_reason=reason;
    /* One bounded diagnostic of the first decision; no page-controlled text,
     * allocation, retry or clock dependency on the fatal path. */
    x86os_puts("BROWSER_EXIT_FAILURE reason="); x86os_puts(reason);
    x86os_puts(" code="); x86os_print_number(state->exit_error);
    x86os_puts(" phase="); x86os_print_number((int)state->probe_phase);
    x86os_puts(" child="); x86os_print_number(state->child_pid);
    x86os_puts(" generation="); x86os_print_number((int)state->child_generation);
    x86os_puts(" check="); x86os_print_number(state->child_check);
    x86os_puts(" check_status="); x86os_print_number(state->child_check_status);
    x86os_puts("\n");
}
static int browser_runtime_result(const browser_state_t *state,int status) {
    return state->exit_error || status==-5 ? 1 : 0;
}
static int owned_child_info(browser_state_t *state, x86os_process_info_t *result) {
    for (uint32_t index = 0; index < 32U; ++index) {
        x86os_process_info_t info;
        if (x86os_process_info(index, &info) <= 0) break;
        if (info.pid == state->child_pid && info.parent_pid == x86os_getpid()) {
            *result = info; return 0;
        }
    }
    return -84;
}
static int child_info(browser_state_t *state, x86os_process_info_t *result) {
    /* PROCESS_IDENTITY describes live processes only, not waitable zombies.
     * A child returned by spawnv stays pinned to this single-threaded parent:
     * the kernel cannot reuse its PID/slot until THIS parent calls wait.
     * Confirm that ownership before reaping, including exit before the first
     * identity lookup. Never invent a generation or kill a zombie by PID. */
    state->child_check=state->child_check_status=0;
    for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        if (owned_child_info(state, result) != 0) { state->child_check=1; return -84; }
        if (result->state == X86OS_PROCESS_ZOMBIE) return 0;
        x86os_process_identity_t identity;
        int status = x86os_process_identity_of(state->child_pid, &identity);
        state->child_check_status=status;
        if (status == -3) continue; /* Exit raced the snapshot: recheck once. */
        if (status != 0 || identity.version != 1U ||
            identity.struct_size != sizeof(identity) ||
            identity.pid != state->child_pid || identity.generation == 0U ||
            (state->child_generation != 0U &&
             identity.generation != state->child_generation)) { state->child_check=2; return -84; }
        state->child_generation = identity.generation;
        return 0;
    }
    state->child_check=3; return -84;
}
static void cancel_fetch(browser_state_t *state) {
    if (state->child_pid <= 0 || state->job_cancelled) return;
    x86os_process_info_t info;
    state->job_cancelled = 1U;
    state->child_reap_deadline=x86os_uptime_ms()+BROWSER_CHILD_REAP_MS;
    int result = child_info(state, &info);
    if (state->css_endpoint) { (void)x86os_ipc_close(state->css_endpoint); state->css_endpoint=0; }
    if (state->fetch_endpoint) { (void)x86os_ipc_close(state->fetch_endpoint); state->fetch_endpoint=0; }
    if (result == 0 && info.state != X86OS_PROCESS_ZOMBIE) {
        int killed=x86os_kill(state->child_pid);
        if (killed != 0) {
            /* Exit revokes IPC before slow cleanup publishes the zombie;
             * duplicate termination is then refused. Revalidate ownership
             * and generation, keep the channel fenced, and poll only this
             * pinned child for the existing one-second reap budget. No
             * repeat kill, replacement child or acceptance of late bytes. */
            result = child_info(state, &info);
            if (!result && info.state != X86OS_PROCESS_ZOMBIE) {
                x86os_puts("BROWSER_CHILD_REAP_WAIT status=");
                x86os_print_number(killed); x86os_puts("\n");
            }
        }
    }
    if (result != 0) {
        set_status(state, "Transportgeneration verloren");
        browser_runtime_failure(state,"cancel-child",result);
    }
}
static void cleanup_fetch_channel(browser_state_t *state) {
    if(state->child_pid>0) return;
    if(state->fetch_endpoint) (void)x86os_ipc_close(state->fetch_endpoint);
    state->fetch_endpoint=state->fetch_received=state->fetch_total=0;
}
static void cancel_job(browser_state_t *state,const char *reason,int error,uint32_t now) {
    int report=state->probe && state->child_pid>0 && !state->job_cancelled;
    /* Fence/terminate before probe output, once per owned generation. Capture
     * the trigger time, not time spent in cancellation/logging. No body data. */
    cancel_fetch(state);
    if(!report) return;
    x86os_puts("BROWSER_JOB_CANCEL reason="); x86os_puts(reason);
    x86os_puts(" status="); x86os_print_number(error);
    x86os_puts(" kind="); x86os_print_number((int)state->job_kind);
    x86os_puts(" remaining_ms="); x86os_print_number((int32_t)(state->job_deadline-now));
    x86os_puts(" sent="); x86os_print_number((int)state->css_sent);
    x86os_puts(" received="); x86os_print_number((int)state->css_received);
    x86os_puts(" total="); x86os_print_number((int)state->css_total); x86os_puts("\n");
}
static void html_parent_phase(const char *name) {
    x86os_puts("BROWSER_HTML_PHASE "); x86os_puts(name); x86os_puts(" ms=");
    x86os_print_number((int)x86os_uptime_ms()); x86os_puts("\n");
}
static int start_html_worker(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t length=state->parse_pending;
    state->parse_pending=0U;
    if (state->child_pid>0 || !length || length>BROWSER_DOCUMENT_LIMIT ||
        state->html_request.request==UINT32_MAX) return -28;
    x86os_process_identity_t identity;
    if (x86os_process_identity_of(x86os_getpid(),&identity)!=0 ||
        identity.version!=1U || identity.struct_size!=sizeof(identity) ||
        identity.pid!=x86os_getpid() || !identity.generation) return -84;
    uint32_t sequence=state->html_request.request+1U;
    state->html_request=(browser_html_header_t){BROWSER_HTML_MAGIC,BROWSER_HTML_DOCUMENT_VERSION,
        sizeof(browser_css_request_t)+length,sequence,(uint32_t)identity.pid,identity.generation,
        0,0,length,state->probe ? state->parse_mode : 0U,{state->document_encoding,0}};
    state->parse_mode=0U;
    state->job_deadline=x86os_uptime_ms()+BROWSER_HTML_DEADLINE_MS;
    html_parent_phase("budget-start");
    cleanup_fetch_channel(state);
    css_input.request=(browser_css_request_t){.header=state->html_request,.version=BROWSER_CSS_DOCUMENT_VERSION,
        .width=client->width>BROWSER_SCROLLBAR_WIDTH ? client->width-BROWSER_SCROLLBAR_WIDTH : 1,
        .height=viewport_height(client)};
    copy_text(css_input.request.document_url,sizeof(css_input.request.document_url),state->job_url);
    if (state->reflow_job) for (unsigned i=0;i<BROWSER_IMAGE_CACHE_COUNT;++i) {
        const browser_image_slot_t *slot=&image_cache[state->active][i];
        if (slot->decoded) { css_input.request.image_sizes[i][0]=slot->source_width; css_input.request.image_sizes[i][1]=slot->source_height; }
    }
    memcpy(css_input.bytes,document_bytes,length);
    const browser_resources_t *bundle=&workspace->resources[state->reflow_job ? state->active : state->active^1U];
    int packed=browser_resources_pack(bundle,state->job_url,css_input.bytes+length,sizeof(css_input.bytes)-length);
    if (packed<0) return packed;
    css_input.request.header.size+=(uint32_t)packed;
    state->html_request=css_input.request.header;
    if (browser_css_request_validate(&css_input.request)) return -84;
    state->css_sent=state->css_received=state->css_total=0;
    if (x86os_ipc_create(&state->css_endpoint)) return -5;
    char number[11], reverse[10]; uint32_t count=0,value=state->css_endpoint;
    do { reverse[count++]=(char)('0'+value%10); value/=10; } while (value);
    for (uint32_t i=0;i<count;++i) number[i]=reverse[count-i-1]; number[count]=0;
    const char *argv[]={"/usr/bin/htmlwork.prg","--ipc",number};
    uint32_t measured_at=timing_start();
    int pid=x86os_spawnv(argv[0],3,argv);
    timing_end(TIME_SPAWN,measured_at);
    html_parent_phase("spawn-returned");
    if (pid<=0) { (void)x86os_ipc_close(state->css_endpoint); state->css_endpoint=0; return -5; }
    state->child_pid=pid; state->child_generation=0U; state->job_kind=3U;
    x86os_puts("BROWSER_HTML5_STARTED\n");
    state->job_cancelled=0U; state->response_status=0U; state->poll_at=x86os_uptime_ms();
    x86os_process_info_t info;
    if (child_info(state,&info)!=0) { browser_runtime_failure(state,"spawn-worker",-84); return -84; }
    if (x86os_ipc_delegate(state->css_endpoint,pid,X86OS_IPC_RIGHT_SEND|X86OS_IPC_RIGHT_RECEIVE)) {
        cancel_fetch(state); return -84;
    }
    set_status(state,"HTML5/CSS wird isoliert verarbeitet ...");
    return 0;
}
static int service_css_ipc(browser_state_t *state) {
    /* At most eight nonblocking packets per UI turn. Never wait for queue space
     * while withholding input or the child's replies. One half-duplex stream. */
    for (unsigned i=0;i<8;++i) {
        /* Successful final send may be followed immediately by child exit.
         * Do not read past the framed reply into the channel's normal EPIPE.
         * An incomplete reply still goes through receive/error validation. */
        if (state->css_total && state->css_received==state->css_total) return 0;
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),0,{0}};
        if (state->css_sent<css_input.request.header.size) {
            uint32_t n=css_input.request.header.size-state->css_sent;
            if (n>BROWSER_CSS_PACKET_DATA) n=BROWSER_CSS_PACKET_DATA;
            browser_css_packet_t packet={BROWSER_CSS_PACKET_MAGIC,state->html_request.request,
                state->css_sent,css_input.request.header.size,{0}};
            memcpy(packet.bytes,(uint8_t *)&css_input+state->css_sent,n);
            m.length=16+n; memcpy(m.payload,&packet,m.length);
            int rc=x86os_ipc_send_bulk_timeout(state->css_endpoint,&m,0);
            if (rc==-11) return 0; if (rc) return rc; state->css_sent+=n;
        } else {
            int rc=x86os_ipc_receive_bulk_timeout(state->css_endpoint,&m,0);
            if (rc==-11) return 0;
            if (rc || m.version!=X86OS_IPC_BULK_MESSAGE_VERSION || m.struct_size!=sizeof(m)) return -84;
            browser_css_packet_t packet; memcpy(&packet,m.payload,sizeof(packet));
            if (browser_css_packet_accept(&packet,m.length,state->html_request.request,image_bytes,
                BROWSER_CSS_WIRE_CAPACITY,&state->css_received,&state->css_total)) return -84;
        }
        /* A successful packet wakes the peer. Hand it the CPU immediately
         * instead of paying an idle timer tick for every single-slot packet.
         * No yield on EAGAIN; at most eight progress-linked handoffs per turn. */
        ++state->load_progress;
        if(x86os_yield()) return -5;
    }
    return 0;
}
static int start_fetch_hop(browser_state_t *state, const char *url, uint32_t kind, uint32_t index) {
    if (state->child_pid > 0 || copy_text(state->job_url, sizeof(state->job_url), url) != 0) return -16;
    static char fetch[BROWSER_RESOURCE_URL_CAPACITY];
    if (strip_fragment(url, fetch, sizeof(fetch)) != 0) return -22;
    uint32_t now = x86os_uptime_ms();
    if ((int32_t)(now - state->redirect_deadline) >= 0) return -110;
    cleanup_fetch_channel(state);
    if(x86os_ipc_create(&state->fetch_endpoint)) return -5;
    char number[11],reverse[10]; uint32_t count=0,value=state->fetch_endpoint;
    do { reverse[count++]=(char)('0'+value%10); value/=10; } while(value);
    for(uint32_t i=0;i<count;++i) number[i]=reverse[count-i-1]; number[count]=0;
    const char *arguments[] = {"/usr/bin/curl.prg", "--reist-ipc", number,
        "--max-bytes", kind == 1U ? "1048576" : "262144", "--include", fetch};
    int pid = x86os_spawnv("/usr/bin/curl.prg", 7, arguments);
    if (pid <= 0) { cleanup_fetch_channel(state); return -5; }
    state->child_pid = pid;
    state->child_generation = 0U;
    state->job_kind = kind; state->job_image = index;
    state->job_cancelled = 0U;
    state->response_status = 0U;
    state->job_deadline = state->redirect_deadline;
    state->poll_at = x86os_uptime_ms();
    x86os_process_info_t info;
    if (child_info(state, &info) != 0) {
        browser_runtime_failure(state,"spawn-transport",-84); return -84;
    }
    if(x86os_ipc_delegate(state->fetch_endpoint,pid,X86OS_IPC_RIGHT_SEND)) {
        cancel_fetch(state); return -84;
    }
    return 0;
}
static int service_fetch_ipc(browser_state_t *state) {
    for(unsigned i=0;i<8;++i) {
        if(state->fetch_total && state->fetch_received==state->fetch_total) return 0;
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),0,{0}};
        int rc=x86os_ipc_receive_bulk_timeout(state->fetch_endpoint,&m,0);
        if(rc==-11) return 0;
        /* A failed child can close without data; only its exact nonzero exit
         * may explain that EOF. Successful exits still require a full frame. */
        if(rc==-32 && !state->fetch_received) return 0;
        if(rc || m.version!=X86OS_IPC_BULK_MESSAGE_VERSION || m.struct_size!=sizeof(m)) return -84;
        reist_curl_ipc_packet_t p; memcpy(&p,m.payload,sizeof(p));
        uint32_t cap=(state->job_kind==1U ? BROWSER_DOCUMENT_LIMIT : BROWSER_IMAGE_INPUT_LIMIT)+REIST_CURL_HEADER_CAPACITY;
        if(reist_curl_ipc_accept(&p,m.length,state->fetch_endpoint,
            state->job_kind==1U ? document_bytes : image_bytes,cap,&state->fetch_received,&state->fetch_total)) return -84;
        ++state->load_progress;
        if(x86os_yield()) return -5;
    }
    return 0;
}
static int start_fetch(browser_state_t *state, const char *url, uint32_t kind, uint32_t index) {
    if (state->child_pid > 0) return -16;
    state->redirect_count = state->follow_redirect = 0U;
    state->transport_retries=0;
    state->redirect_deadline = x86os_uptime_ms() + BROWSER_REDIRECT_DEADLINE_MS;
    if (kind==4U && (int32_t)(state->redirect_deadline-state->resource_deadline)>0)
        state->redirect_deadline=state->resource_deadline;
    return start_fetch_hop(state, url, kind, index);
}
static const char *fragment_of(const char *url) {
    for (uint32_t i = 0; i < BROWSER_URL_CAPACITY && url[i]; ++i)
        if (url[i] == '#') return url + i + 1U;
    return 0;
}
static void scroll_to_fragment(browser_state_t *state, reist_gui_surface_client_t *client) {
    const char *fragment = fragment_of(state->active_url);
    uint32_t y;
    if (fragment && browser_anchor_y(&documents[state->active], &layouts[state->active], fragment, &y) == 0)
        set_scroll(state, client, y);
}
static int copy_reflow_assets(uint32_t candidate,uint32_t active) {
    if(candidate>1 || active>1 || candidate==active) return -84;
    const browser_resources_t *src=&workspace->resources[active];
    if(src->count>BROWSER_RESOURCE_COUNT || src->length>BROWSER_RESOURCE_BYTES) return -84;
    /* Preflight all ranges before modifying the private candidate. Unused
     * pixels and resource records are neither copied nor made live. */
    for(uint32_t i=0;i<BROWSER_IMAGE_CACHE_COUNT;++i) {
        const browser_image_slot_t *s=&image_cache[active][i];
        if(s->decoded>1 || (s->decoded && (!s->width || !s->height ||
            s->width>BROWSER_IMAGE_CACHE_SIDE || s->height>BROWSER_IMAGE_CACHE_SIDE))) return -84;
    }
    for(uint32_t i=0;i<BROWSER_IMAGE_CACHE_COUNT;++i) {
        const browser_image_slot_t *s=&image_cache[active][i];
        browser_image_slot_t *d=&image_cache[candidate][i];
        if(s->decoded) memcpy(d,s,offsetof(browser_image_slot_t,pixels)+s->width*s->height*sizeof(s->pixels[0]));
        else d->decoded=0;
    }
    browser_resources_t *dst=&workspace->resources[candidate];
    memcpy(dst,src,BROWSER_RESOURCE_HEADER_BYTES+src->count*sizeof(src->entries[0]));
    memcpy(dst->bytes,src->bytes,src->length);
    return 0;
}
static int publish_html_reply(browser_state_t *state, reist_gui_surface_client_t *client,
                              uint32_t pid, uint32_t generation) {
    uint32_t candidate = state->active ^ 1U;
    if (!state->css_total || state->css_received!=state->css_total) return -84;
    uint32_t magic=0;
    if (state->css_total>=sizeof(magic)) memcpy(&magic,image_bytes,sizeof(magic));
    if (magic==BROWSER_RESOURCE_NEED_MAGIC) {
        if (state->reflow_job || state->css_total>sizeof(resource_needs) ||
            !state->resource_document_length ||
            (int32_t)(x86os_uptime_ms()-state->resource_deadline)>=0) return -84;
        memcpy(&resource_needs,image_bytes,state->css_total);
        browser_resources_t *bundle=&workspace->resources[candidate];
        if (browser_resource_needs_validate(&resource_needs,state->css_total,&state->html_request,
            pid,generation,bundle,state->resource_document_url) ||
            resource_needs.count>BROWSER_RESOURCE_COUNT-bundle->count) return -84;
        for (uint32_t i=0;i<resource_needs.count;++i)
            if (browser_resources_add(bundle,state->resource_document_url,
                resource_needs.items[i].url,resource_needs.items[i].depth)<0) return -84;
        state->resource_loading=1;
        set_status(state,"Lade Stylesheets ... Esc: abbrechen");
        return 0;
    }
    int result=browser_css_unpack(image_bytes,state->css_total,&css_input.request,pid,generation,&html_reply,&scenes[candidate]);
    if (result) return result;
    if (css_input.request.width!=client->width-BROWSER_SCROLLBAR_WIDTH ||
        css_input.request.height!=viewport_height(client)) {
        /* Coalesced configure supersedes this scene. Retain uncommitted HTML
         * and retry the newest viewport, never publish stale hit geometry. */
        state->parse_pending=state->html_request.input_length;
        return -11;
    }
    documents[candidate]=html_reply.document;
    browser_form_state_t *form_state=&workspace->forms[candidate];
    const browser_form_state_t *previous_forms=&workspace->forms[state->active];
    uint32_t form_generation=previous_forms->generation;
    if(state->reflow_job) *form_state=*previous_forms;
    else if(form_generation==UINT32_MAX) return -28;
    else ++form_generation;
    result=browser_forms_bind(&scenes[candidate].forms,&scenes[state->active].forms,
        form_state,form_generation,(int)state->reflow_job);
    if(result) return result;
    const char *url=state->job_url;
    if (state->reflow_job) {
        if(copy_reflow_assets(candidate,state->active)) return -84;
    }
    else for (uint32_t i = 0; i < BROWSER_IMAGE_CACHE_COUNT; ++i) image_cache[candidate][i].decoded = 0U;
    /* Geometry projection only: no parser, selector or layout in chrome. */
    browser_layout_t *layout=&layouts[candidate]; layout->run_count=0;
    layout->total_height=scenes[candidate].total_height;
    for (uint32_t i=0;i<scenes[candidate].count;++i) {
        const browser_scene_run_t *r=&scenes[candidate].runs[i];
        if (r->kind==BROWSER_SCENE_FILL) continue;
        layout->runs[layout->run_count++]=(browser_layout_run_t){r->kind,r->offset,r->length,r->flags,r->link,r->x,r->y,r->width,r->height};
    }
    for (uint32_t i=0;i<client->width*client->height;++i) surface_pixels[i]=0xffffff;
    result=browser_scene_raster_forms(&documents[candidate],&scenes[candidate],&workspace->font,image_cache[candidate],form_state,
        state->reflow_job ? state->scroll_y : 0,surface_pixels,client->width,client->height,BROWSER_CONTENT_TOP,viewport_height(client));
    if (result != 0) return result;
    x86os_puts(state->reflow_job ? "BROWSER_CSS_REFLOW_MS " : "BROWSER_CSS_DOCUMENT_MS ");
    x86os_print_number((int)(x86os_uptime_ms()-(state->job_deadline-BROWSER_HTML_DEADLINE_MS)));
    x86os_puts("\n");
    copy_text(state->active_url, sizeof(state->active_url), url);
    /* Do not overwrite input typed while a previous navigation was loading. */
    if (!state->address_focused) {
        copy_text(state->address, sizeof(state->address), url);
        state->address_length = (uint32_t)bounded_length(state->address, sizeof(state->address));
        state->address_cursor = state->address_length;
    }
    state->active = candidate; state->loaded = 1U;
    workspace->forms_redraw=1;
    state->wheel_remainder=0;
    memcpy(state->scene_image_sizes,css_input.request.image_sizes,sizeof(state->scene_image_sizes));
    if (!state->reflow_job) state->scroll_y = 0U;
    state->active_length=state->html_request.input_length;
    state->active_encoding=state->document_encoding;
    memcpy(active_html,css_input.bytes,state->active_length);
    state->hit_count = 0U;
    state->armed_link = UINT32_MAX; state->scrollbar.state.captured = 0U;
    if (!state->reflow_job) {
        state->image_next = 0U;
        state->image_deadline = x86os_uptime_ms() + BROWSER_PAGE_IMAGE_DEADLINE_MS;
    }
    state->redraw = state->chrome_redraw = 1U;
    if (!state->reflow_job) scroll_to_fragment(state, client);
    else set_scroll(state,client,state->scroll_y);
    state->reflow_job=state->reflow_pending=0;
    state->resource_loading=state->resource_document_length=0;
    char title[REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY];
    const char *source = documents[candidate].title[0] ? documents[candidate].title : "REIST Web";
    size_t title_length = utf8_prefix_length(source, sizeof(title) - 1U);
    /* The Surface title limit is smaller than the parser's metadata quota.
     * Cut at a UTF-8 scalar boundary, never inside a multibyte character. */
    for (size_t i = 0U; i < title_length; ++i) title[i] = source[i];
    title[title_length] = '\0';
    (void)reist_gui_surface_client_set_title(client, title);
    set_status(state, "Bereit - Bilder werden begrenzt nachgeladen");
    x86os_puts("BROWSER_RENDER_OK\n");
    x86os_puts("BROWSER_HTML5_WORKER_OK\n");
    return 0;
}
static int publish_document_bytes(browser_state_t *state, reist_gui_surface_client_t *client,
                                   const char *url, uint32_t length) {
    if (!length || length>BROWSER_DOCUMENT_LIMIT ||
        copy_text(state->job_url,sizeof(state->job_url),url)!=0) return -27;
    char resource[BROWSER_URL_CAPACITY], active_resource[BROWSER_URL_CAPACITY];
    /* RFC 3986 section 3.5: a fragment selects within a resource, not another
     * fetched representation. This static CSS snapshot has no :target state.
     * Preserve query, path and origin byte-for-byte; never equate other URLs. */
    uint32_t same_resource=strip_fragment(url,resource,sizeof(resource))==0 &&
        strip_fragment(state->active_url,active_resource,sizeof(active_resource))==0 &&
        text_equal(resource,active_resource);
    uint32_t same_images=1;
    for (unsigned i=0;i<BROWSER_IMAGE_CACHE_COUNT;++i) {
        const browser_image_slot_t *slot=&image_cache[state->active][i];
        if (slot->decoded &&
            (!documents[state->active].images[i].width || !documents[state->active].images[i].height) &&
            (state->scene_image_sizes[i][0]!=slot->source_width ||
             state->scene_image_sizes[i][1]!=slot->source_height)) same_images=0;
    }
    if (state->loaded && !workspace->resources[state->active].count &&
        !state->parse_mode && !state->reflow_pending && same_images &&
        (state->image_next>=documents[state->active].image_count || state->image_next>=BROWSER_IMAGE_CACHE_COUNT) &&
        client->width>BROWSER_SCROLLBAR_WIDTH && (scenes[state->active].version==BROWSER_SCENE_VERSION ||
        scenes[state->active].version==BROWSER_SCENE_DOCUMENT_VERSION) &&
        scenes[state->active].width==client->width-BROWSER_SCROLLBAR_WIDTH &&
        scenes[state->active].height==viewport_height(client) && same_resource &&
        state->active_encoding==state->document_encoding && state->active_length==length && !memcmp(active_html,document_bytes,length)) {
        browser_form_state_t *fs=&workspace->forms[state->active];
        if(fs->generation==UINT32_MAX || browser_forms_bind(&scenes[state->active].forms,NULL,fs,fs->generation+1,0)) return -28;
        workspace->forms_redraw=1;
        /* Reuse only our last fully validated immutable scene after a fresh
         * successful document read/transport. Never reuse an origin or guess a
         * changed document by hash. Images still refresh; stale pixels hide. */
        state->parse_pending=state->reflow_pending=state->reflow_job=0;
        state->image_next=0; state->image_deadline=x86os_uptime_ms()+BROWSER_PAGE_IMAGE_DEADLINE_MS;
        for (unsigned i=0;i<BROWSER_IMAGE_CACHE_COUNT;++i) image_cache[state->active][i].decoded=0;
        state->wheel_remainder=0; set_scroll(state,client,0);
        state->armed_link=UINT32_MAX; state->hit_count=0; state->scrollbar.state.captured=0;
        copy_text(state->active_url,sizeof(state->active_url),url);
        if (!state->address_focused) {
            copy_text(state->address,sizeof(state->address),url);
            state->address_length=(uint32_t)bounded_length(url,sizeof(state->address));
            state->address_cursor=state->address_length;
        }
        scroll_to_fragment(state,client);
        state->redraw=state->chrome_redraw=1;
        set_status(state,"Bereit - Bilder werden begrenzt nachgeladen");
        x86os_puts("BROWSER_CSS_SCENE_REUSED\n");
        return 0;
    }
    if (state->resource_generation==UINT32_MAX) return -28;
    browser_resources_init(&workspace->resources[state->active^1U],++state->resource_generation);
    state->resource_loading=0;
    state->resource_deadline=x86os_uptime_ms()+BROWSER_RESOURCE_DEADLINE_MS;
    state->resource_document_length=length;
    copy_text(state->resource_document_url,sizeof(state->resource_document_url),url);
    state->parse_pending=length;
    state->reflow_job=state->reflow_pending=0;
    return 0; /* Spawn on the next UI turn, after transport cleanup. */
}
static int publish_document(browser_state_t *state, reist_gui_surface_client_t *client,
                             const char *path, const char *url) {
    uint32_t length = 0U;
    int result = read_file(path, document_bytes, BROWSER_DOCUMENT_LIMIT, &length);
    return result != 0 ? result : publish_document_bytes(state, client, url, length);
}
static int load_image_bytes(browser_state_t *state, reist_gui_surface_client_t *client,
                             uint32_t index, uint32_t length) {
    if (index >= BROWSER_IMAGE_CACHE_COUNT) return -28;
    uint32_t measured_at=timing_start();
    reist_image_info_t info;
    int result = browser_image_decode(image_bytes, length, decoded_pixels,
                                                  BROWSER_IMAGE_PIXEL_LIMIT, &info);
    timing_end(TIME_DECODE,measured_at);
    if (result != 0) return result;
    browser_image_slot_t *slot = &image_cache[state->active][index];
    slot->source_width = info.width; slot->source_height = info.height;
    uint32_t largest = info.width > info.height ? info.width : info.height;
    slot->width = largest > BROWSER_IMAGE_CACHE_SIDE ? info.width * BROWSER_IMAGE_CACHE_SIDE / largest : info.width;
    slot->height = largest > BROWSER_IMAGE_CACHE_SIDE ? info.height * BROWSER_IMAGE_CACHE_SIDE / largest : info.height;
    if (!slot->width) slot->width = 1U;
    if (!slot->height) slot->height = 1U;
    for (uint32_t y = 0; y < slot->height; ++y)
        for (uint32_t x = 0; x < slot->width; ++x)
            slot->pixels[y * slot->width + x] = decoded_pixels[
                (y * info.height / slot->height) * info.stride_pixels + x * info.width / slot->width];
    slot->decoded = 1U;
    /* Intrinsic-size reflow is coalesced until the image queue is exhausted. */
    if ((!documents[state->active].images[index].width || !documents[state->active].images[index].height) &&
        (state->scene_image_sizes[index][0]!=info.width || state->scene_image_sizes[index][1]!=info.height))
        state->reflow_pending=1;
    state->hit_count = 0U; state->armed_link = UINT32_MAX;
    set_scroll(state, client, state->scroll_y);
    state->redraw = 1U;
    x86os_puts("BROWSER_IMAGE_OK\n");
    return 0;
}
static int load_image(browser_state_t *state, reist_gui_surface_client_t *client,
                       const char *path, uint32_t index) {
    uint32_t length = 0U;
    int result = read_file(path, image_bytes, BROWSER_IMAGE_INPUT_LIMIT, &length);
    return result != 0 ? result : load_image_bytes(state, client, index, length);
}
static int finish_fetch(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t document = state->job_kind == 1U;
    uint8_t *bytes = document ? document_bytes : image_bytes;
    uint32_t limit = document ? BROWSER_DOCUMENT_LIMIT : BROWSER_IMAGE_INPUT_LIMIT;
    uint32_t length = state->fetch_total;
    if(!length || state->fetch_received!=length || length>limit+REIST_CURL_HEADER_CAPACITY) return -84;
    int result;
    browser_response_t response = {0};
    result = document ? browser_response_open_document(bytes,length,state->job_url,&response) :
        browser_response_open_kind(bytes,length,state->job_url,state->job_kind==4U ? BROWSER_RESPONSE_CSS : BROWSER_RESPONSE_IMAGE,&response);
    /* C++ admission returns legacy diagnostics on failure, not publishable
     * metadata. Only the status may be consumed before checking the result. */
    state->response_status = response.status;
    if (result < 0) return result;
    if (response.body_length > limit) return -90;
    if (result == 1) {
        if (state->redirect_count >= BROWSER_REDIRECT_LIMIT) return -40;
        if ((int32_t)(x86os_uptime_ms() - state->redirect_deadline) >= 0) return -110;
        if (state->job_kind==4U) {
            static char canonical[BROWSER_RESOURCE_URL_CAPACITY];
            if (browser_resource_url(state->job_url,response.redirect,canonical) ||
                browser_resource_admit(workspace->resources[state->active^1U].entries[state->job_image].url,canonical)) return -13;
            copy_text(state->job_url,sizeof(state->job_url),canonical);
        } else if (copy_text(state->job_url, sizeof(state->job_url), response.redirect) != 0) return -90;
        ++state->redirect_count;
        /* Start only on the next UI turn, after the old temp was unlinked.
         * Otherwise a fast new child could publish a file cleanup then erases. */
        state->follow_redirect = 1U;
        if (document && !state->address_focused) {
            copy_text(state->address, sizeof(state->address), response.redirect);
            state->address_cursor = state->address_length = (uint32_t)bounded_length(state->address, sizeof(state->address));
            state->chrome_redraw = 1U;
        }
        set_status(state, "HTTP-Weiterleitung ... Esc: abbrechen");
        x86os_puts("BROWSER_REDIRECT_OK\n"); return 0;
    }
    for (uint32_t i = 0; i < response.body_length; ++i) bytes[i] = bytes[response.body_offset + i];
    if (state->job_kind==4U) {
        if ((int32_t)(x86os_uptime_ms()-state->resource_deadline)>=0) return -110;
        return browser_resources_store(&workspace->resources[state->active^1U],state->job_image,
            state->job_url,bytes,response.body_length);
    }
    if(document) state->document_encoding=response.encoding;
    return document ? publish_document_bytes(state, client, state->job_url, response.body_length)
                    : load_image_bytes(state, client, state->job_image, response.body_length);
}
static int navigate(browser_state_t *state, reist_gui_surface_client_t *client, const char *target) {
    char normalized[BROWSER_URL_CAPACITY];
    int result = target && target[0] == '#' && state->loaded
        ? reist_html_url_resolve(state->active_url, target, normalized, sizeof(normalized))
        : reist_html_navigation_normalize(target, normalized, sizeof(normalized));
    if (result != 0) { set_status(state, "Adresse nicht unterstuetzt"); return result; }
    char old_path[BROWSER_URL_CAPACITY], new_path[BROWSER_URL_CAPACITY];
    strip_fragment(normalized, new_path, sizeof(new_path));
    strip_fragment(state->active_url, old_path, sizeof(old_path));
    if (state->loaded && fragment_of(normalized) && text_equal(old_path, new_path)) {
        if (state->pending || state->parse_pending || state->resource_loading || ((state->child_pid > 0 || state->follow_redirect) && state->job_kind != 2U)) {
            state->pending = 0U;
            state->parse_pending = 0U;
            state->follow_redirect = 0U;
            cancel_fetch(state);
            abandon_resources(state);
        }
        copy_text(state->active_url, sizeof(state->active_url), normalized);
        copy_text(state->address, sizeof(state->address), normalized);
        state->address_cursor = state->address_length = (uint32_t)bounded_length(normalized, sizeof(normalized));
        state->chrome_redraw = 1U; scroll_to_fragment(state, client);
        return 0;
    }
    copy_text(state->pending_url, sizeof(state->pending_url), normalized);
    state->pending = 1U;
    state->document_encoding=BROWSER_ENCODING_AUTO;
    state->parse_pending = 0U;
    state->reflow_pending=state->reflow_job=0;
    state->follow_redirect = 0U;
    state->image_next = BROWSER_IMAGE_CACHE_COUNT;
    state->armed_link = UINT32_MAX;
    cancel_fetch(state);
    abandon_resources(state);
    set_status(state, "Lade Dokument ... Esc: abbrechen");
    return 0;
}
/* Poll only completed owned generations: x86os_wait never blocks the UI.
 * A single child and a single immutable temporary resource exist at a time. */
static void service_loads(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t now = x86os_uptime_ms();
    if (state->child_pid > 0) {
        if ((int32_t)(now - state->job_deadline) >= 0) {
            if (state->job_kind==3U && !state->job_cancelled) ++state->parser_timeouts;
            cancel_job(state,"deadline",-110,now);
        }
        if (state->job_kind==3U && !state->job_cancelled) {
            int rc=service_css_ipc(state);
            if(rc) cancel_job(state,"css-ipc",rc,x86os_uptime_ms());
        }
        if (state->job_kind!=3U && !state->job_cancelled && service_fetch_ipc(state)) cancel_fetch(state);
        if (state->exit_requested) return;
        if ((int32_t)(now - state->poll_at) < 0) return;
        state->poll_at = now + 10U;
        x86os_process_info_t info;
        if (child_info(state, &info) != 0) { browser_runtime_failure(state,"poll-child",-84); return; }
        if (info.state != X86OS_PROCESS_ZOMBIE) {
            if (state->job_cancelled && (int32_t)(now-state->child_reap_deadline)>=0)
                browser_runtime_failure(state,"cancel-reap-timeout",-110);
            return;
        }
        /* Exit may race the last send after this turn's first drain. The
         * endpoint has one bulk slot: drain that final packet before reaping. */
        if (state->job_kind==3U && !state->job_cancelled) {
            int rc=service_css_ipc(state);
            if(rc) cancel_job(state,"css-ipc-final",rc,x86os_uptime_ms());
        }
        if (state->job_kind!=3U && !state->job_cancelled && service_fetch_ipc(state)) cancel_fetch(state);
        int status = -1;
        uint32_t pid=(uint32_t)state->child_pid, generation=state->child_generation;
        int waited=x86os_wait(state->child_pid, &status);
        if (waited != state->child_pid) {
            browser_runtime_failure(state,"wait-child",waited); return;
        }
        state->child_pid = 0;
        state->child_generation = 0U;
        int result = -5;
        if (!state->job_cancelled && status == 0) result = state->job_kind==3U
            ? publish_html_reply(state,client,pid,generation) : finish_fetch(state, client);
        if (state->css_endpoint) { (void)x86os_ipc_close(state->css_endpoint); state->css_endpoint=0; }
        if (state->job_kind==3U && result!=0 && result!=-11) {
            ++state->parser_failures;
            x86os_puts("BROWSER_HTML5_REJECT exit="); x86os_print_number(status);
            x86os_puts(" result="); x86os_print_number(result);
            x86os_puts(" cancelled="); x86os_print_number((int)state->job_cancelled); x86os_puts("\n");
        }
        cleanup_fetch_channel(state);
        if(!state->pending && !state->job_cancelled && state->job_kind==1U &&
           (status==7 || status==28) && !state->transport_retries &&
           (int32_t)(state->redirect_deadline-x86os_uptime_ms())>0) {
            ++state->transport_retries; state->follow_redirect=1;
            set_status(state,"Verbindung unterbrochen - ein neuer Versuch"); return;
        }
        if (result != 0 && result!=-11 && !state->pending) {
            x86os_puts("BROWSER_LOAD_ERROR kind="); x86os_print_number((int)state->job_kind);
            x86os_puts(" exit="); x86os_print_number(status); x86os_puts(" result="); x86os_print_number(result);
            x86os_puts(" cancelled="); x86os_print_number((int)state->job_cancelled); x86os_puts("\n");
            state->follow_redirect = 0U;
            if (state->job_kind==3U || state->job_kind==4U) abandon_resources(state);
            if (state->job_kind == 3U) set_status(state, "HTML5 abgelehnt - bisherige Seite bleibt");
            else if (state->job_kind == 2U) set_status(state, "Bild nicht verfuegbar - Alternativtext");
            else if (state->job_cancelled) set_status(state,"Laden abgebrochen oder Zeitlimit erreicht");
            else if (status==6) set_status(state,"DNS: Servername konnte nicht aufgeloest werden");
            else if (status==7) set_status(state,"TCP: Server nicht erreichbar");
            else if (status==28) set_status(state,"Netzwerk-Zeitlimit erreicht - Seite bleibt");
            else if (status==35 || status==60) set_status(state,"HTTPS/TLS-Pruefung fehlgeschlagen - Seite bleibt");
            else if (status==63 || result==-90) set_status(state,"Dokument groesser als 1 MiB - Seite bleibt");
            else if (status==23) set_status(state,"Ladedatei konnte nicht geschrieben werden");
            else if (result == -40) set_status(state, "Zu viele Weiterleitungen - Seite bleibt");
            else if (result == -95) set_status(state, "Inhaltstyp/Kodierung nicht unterstuetzt");
            else if (result == -13) set_status(state, "Unsichere Weiterleitung abgewiesen");
            else if (state->response_status >= 400U) {
                char message[] = "HTTP 000 - bisherige Seite bleibt";
                message[5] = (char)('0' + state->response_status / 100U);
                message[6] = (char)('0' + state->response_status / 10U % 10U);
                message[7] = (char)('0' + state->response_status % 10U);
                set_status(state, message);
            } else set_status(state, "Laden abgelehnt - bisherige Seite bleibt");
        }
        return;
    }
    if (state->parse_pending && !state->pending) {
        int result=start_html_worker(state,client);
        if (result!=0) {
            abandon_resources(state);
            x86os_puts("BROWSER_HTML5_START_ERROR result="); x86os_print_number(result); x86os_puts("\n");
            set_status(state,"HTML5 nicht verfuegbar - Seite bleibt");
        }
        return;
    }
    if (state->reflow_pending && state->loaded && !state->pending && !state->resource_loading &&
        (state->image_next>=documents[state->active].image_count || state->image_next>=BROWSER_IMAGE_CACHE_COUNT)) {
        state->reflow_pending=0; state->reflow_job=1;
        state->parse_pending=state->active_length;
        state->document_encoding=state->active_encoding;
        memcpy(document_bytes,active_html,state->active_length);
        copy_text(state->job_url,sizeof(state->job_url),state->active_url);
        return;
    }
    if (state->pending) {
        state->pending = 0U;
        char path[BROWSER_URL_CAPACITY];
        strip_fragment(state->pending_url, path, sizeof(path));
        int result = is_network(path) ? start_fetch(state, state->pending_url, 1U, 0U)
            : publish_document(state, client, path, state->pending_url);
        if (result != 0) {
            x86os_puts("BROWSER_DOCUMENT_ERROR result="); x86os_print_number(result); x86os_puts("\n");
            set_status(state, "Laden abgelehnt - bisherige Seite bleibt");
        }
        return;
    }
    if (state->follow_redirect) {
        state->follow_redirect = 0U;
        /* A redirect alias may name bytes acquired earlier in this navigation. */
        if (state->job_kind==4U) {
            browser_resources_t *bundle=&workspace->resources[state->active^1U];
            int found=browser_resources_find(bundle,state->job_url);
            if (found>=0 && bundle->entries[found].ready) {
                browser_resource_t *r=&bundle->entries[found];
                if (browser_resources_store(bundle,state->job_image,state->job_url,bundle->bytes+r->offset,r->length))
                    abandon_resources(state);
                return;
            }
        }
        if (start_fetch_hop(state, state->job_url, state->job_kind, state->job_image) != 0) {
            if (state->job_kind==4U) abandon_resources(state);
            set_status(state, "Weiterleitung fehlgeschlagen - Seite bleibt");
        }
        return;
    }
    if (state->resource_loading) {
        int result=0;
        browser_resources_t *bundle=&workspace->resources[state->active^1U];
        uint32_t index=0;
        while (index<bundle->count && bundle->entries[index].ready) ++index;
        if ((int32_t)(now-state->resource_deadline)>=0) result=-110;
        else if (index==bundle->count) {
            state->resource_loading=0;
            state->parse_pending=state->resource_document_length;
            copy_text(state->job_url,sizeof(state->job_url),state->resource_document_url);
        } else {
            const char *url=bundle->entries[index].url;
            if (is_network(url)) result=start_fetch(state,url,4U,index);
            else {
                uint32_t length=0;
                result=read_file_kind(url,image_bytes,BROWSER_RESOURCE_LIMIT,&length,1);
                if (!result && (int32_t)(x86os_uptime_ms()-state->resource_deadline)>=0) result=-110;
                if (!result) result=browser_resources_store(bundle,index,url,image_bytes,length);
            }
        }
        if (result) { abandon_resources(state); set_status(state,"Stylesheet abgelehnt - bisherige Seite bleibt"); }
        return;
    }
    if (!state->loaded || state->image_next >= documents[state->active].image_count ||
        state->image_next >= BROWSER_IMAGE_CACHE_COUNT) return;
    if ((int32_t)(now - state->image_deadline) >= 0) {
        state->image_next = BROWSER_IMAGE_CACHE_COUNT;
        set_status(state, "Bildzeitbudget erschoepft - Alternativtext"); return;
    }
    uint32_t index = state->image_next++;
    const char *source = documents[state->active].images[index].source;
    if(scenes[state->active].version==BROWSER_SCENE_DOCUMENT_VERSION && scenes[state->active].image_urls[index][0])
        source=scenes[state->active].image_urls[index];
    static char resolved[BROWSER_RESOURCE_URL_CAPACITY], path[BROWSER_RESOURCE_URL_CAPACITY];
    int result = source[0] ? browser_resource_url(state->active_url, source, resolved) : -22;
    if (result == 0 && is_network(state->active_url) && !is_network(resolved)) result = -13;
    /* HTTPS pages cannot silently downgrade image transport. */
    if (result == 0 && text_prefix(state->active_url, "https://") && text_prefix(resolved, "http://")) result = -13;
    if (result == 0) {
        strip_fragment(resolved, path, sizeof(path));
        result = is_network(path) ? start_fetch(state, resolved, 2U, index) : load_image(state, client, path, index);
    }
    if (result != 0) set_status(state, "Bild nicht verfuegbar - Alternativtext");
}

static int publish_pixels(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t measured_at=timing_start();
    uint32_t width = client->width, height = client->height;
    if (!width || !height || width > REIST_GUI_SURFACE_MAX_WIDTH || height > REIST_GUI_SURFACE_MAX_HEIGHT) return -22;
    for (uint32_t i = 0; i < width * height; ++i) surface_pixels[i] = 0x00FFFFFFU;
    if (state->loaded && scenes[state->active].version) {
        int rc=browser_scene_raster_forms(&documents[state->active],&scenes[state->active],&workspace->font,
            image_cache[state->active],&workspace->forms[state->active],state->scroll_y,surface_pixels,width,height,BROWSER_CONTENT_TOP,viewport_height(client));
        if (rc) return -27; /* Scene quota/geometry never tears down chrome. */
    } else if (state->loaded) {
        const browser_layout_t *layout = &layouts[state->active];
        int32_t bottom = (int32_t)(BROWSER_CONTENT_TOP + viewport_height(client));
        for (uint32_t i = 0; i < layout->run_count; ++i) {
            const browser_layout_run_t *run = &layout->runs[i];
            if (run->kind != REIST_HTML_ELEMENT_IMAGE || run->text_offset >= BROWSER_IMAGE_CACHE_COUNT) continue;
            const browser_image_slot_t *slot = &image_cache[state->active][run->text_offset];
            if (!slot->decoded) continue;
            int32_t top = (int32_t)BROWSER_CONTENT_TOP + (int32_t)run->y - (int32_t)state->scroll_y;
            int32_t first = top < (int32_t)BROWSER_CONTENT_TOP ? (int32_t)BROWSER_CONTENT_TOP : top;
            int32_t end = top + (int32_t)run->height;
            if (end > bottom) end = bottom;
            for (int32_t y = first; y < end; ++y) {
                uint32_t source_y = (uint32_t)(y - top) * slot->height / run->height;
                for (uint32_t x = 0; x < run->width && run->x + (int32_t)x < (int32_t)width; ++x) {
                    if (run->x + (int32_t)x < 0) continue;
                    surface_pixels[(uint32_t)y * width + (uint32_t)run->x + x] =
                        slot->pixels[source_y * slot->width + x * slot->width / run->width];
                }
            }
        }
    }
    uint32_t id = 0U, generation = 0U, registered = 0U;
    timing_end(TIME_RASTER,measured_at); measured_at=timing_start();
    int result = x86os_display_surface_buffer_create(width, height, surface_pixels, width, &id, &generation);
    timing_end(TIME_BUFFER,measured_at); measured_at=timing_start();
    if (result != 0) return render_result("buffer-create", result);
    reist_gui_surface_buffer_t descriptor = {REIST_GUI_SURFACE_BUFFER_API_VERSION, sizeof(descriptor),
        id, generation, width, height, width * 4U, REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888, width * height * 4U, 0U};
    result = render_result("buffer-register", reist_gui_surface_client_buffer_create(client, &descriptor));
    if (result == 0) registered = 1U;
    if (result == 0) result = render_result("buffer-attach", reist_gui_surface_client_attach(client, id, generation));
    /* Every underlay is white outside the page viewport. Replacing it at the
     * same geometry cannot change chrome/status pixels; their retained layers
     * publish their own damage. Initial upload and resize still cover all. */
    reist_gui_rect_t damage = {0, 0, width, height};
    if (state->buffer_id && state->buffer_width == width && state->buffer_height == height &&
        height > BROWSER_CONTENT_TOP + BROWSER_STATUS_HEIGHT) {
        damage.y = BROWSER_CONTENT_TOP;
        damage.height = viewport_height(client);
    }
    if (result == 0) result = render_result("buffer-damage", reist_gui_surface_client_damage(client, damage));
    uint32_t released = 0U, released_generation = 0U;
    if (result == 0) result = render_result("buffer-commit", reist_gui_surface_client_commit_with_release(client, &released, &released_generation));
    if (result != 0) {
        if (registered) (void)reist_gui_surface_client_buffer_destroy(client, id, generation);
        (void)x86os_display_surface_buffer_destroy(id, generation);
        return result;
    }
    uint32_t old_id = state->buffer_id, old_generation = state->buffer_generation;
    state->buffer_id = id; state->buffer_generation = generation;
    state->buffer_width = width; state->buffer_height = height;
    if (released != 0U) {
        if (released != old_id || released_generation != old_generation) return render_result("buffer-release-identity", -84);
        result = render_result("buffer-unregister", reist_gui_surface_client_buffer_destroy(client, released, released_generation));
        if (result == 0) result = render_result("buffer-release", x86os_display_surface_buffer_destroy(released, released_generation));
    }
    timing_end(TIME_PIXEL_IPC,measured_at);
    return result;
}
static int paint_button(reist_gui_surface_client_t *client, reist_gui_rect_t rect) {
    if (reist_gui_surface_client_paint_fill(client, rect, 0x00D4D0C8U) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, 0x00FFFFFFU) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, 0x00FFFFFFU) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1, rect.width, 1U}, 0x00808080U) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y, 1U, rect.height}, 0x00808080U) != 0) return -1;
    return 0;
}
static int render_body(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t measured_at=timing_start();
    const uint32_t white = 0x00FFFFFFU, dark = 0x00202020U, link = 0x000000CCU;
    const uint32_t heading = 0x00203070U, muted = 0x00606060U;
    int pixels_result=publish_pixels(state, client);
    if (pixels_result==-27) {
        state->scroll_y=state->painted_scroll; state->redraw=0;
        set_status(state,"Darstellungsgrenze - bisherige Ansicht bleibt"); return 0;
    }
    if (pixels_result || render_result("base-begin", reist_gui_surface_client_paint_begin(client)) != 0) return -1;
    state->hit_count = 0U;
    uint32_t view = viewport_height(client);
    uint32_t body_bottom = BROWSER_CONTENT_TOP + view;
    uint32_t commands = 0U;
    if (state->loaded && scenes[state->active].version) {
        const browser_scene_t *s=&scenes[state->active];
        int32_t right=(int32_t)client->width-BROWSER_SCROLLBAR_WIDTH;
        for (uint32_t i=0;i<s->count && state->hit_count<BROWSER_LINK_HIT_CAPACITY;++i) {
            const browser_scene_run_t *r=&s->runs[i];
            if (r->link==UINT32_MAX) continue;
            int32_t y=(int32_t)BROWSER_CONTENT_TOP+r->y-(int32_t)state->scroll_y;
            int32_t top=y>(int32_t)BROWSER_CONTENT_TOP ? y : (int32_t)BROWSER_CONTENT_TOP;
            int32_t bottom=y+(int32_t)r->height; if (bottom>(int32_t)body_bottom) bottom=(int32_t)body_bottom;
            int32_t left=r->x>0 ? r->x : 0, end=r->x+(int32_t)r->width;
            if (end>right) end=right;
            if (bottom>top && end>left) state->hits[state->hit_count++]=(browser_link_hit_t){
                {left,top,(uint32_t)(end-left),(uint32_t)(bottom-top)},r->link};
        }
    } else if (state->loaded) {
        const browser_layout_t *layout = &layouts[state->active];
        const reist_html_document_t *document = &documents[state->active];
        for (uint32_t index = 0U; index < layout->run_count; ++index) {
            const browser_layout_run_t *run = &layout->runs[index];
            int64_t screen_y = (int64_t)BROWSER_CONTENT_TOP + run->y -
                               state->scroll_y;
            if (screen_y + run->height <= BROWSER_CONTENT_TOP ||
                screen_y >= body_bottom) continue;
            /* Retained font calls cannot crop a partial glyph. Omit edge text
             * rows; image pixels and image hit regions are clipped separately. */
            if (run->kind != REIST_HTML_ELEMENT_IMAGE &&
                (screen_y < BROWSER_CONTENT_TOP || screen_y + run->height > body_bottom)) continue;
            if (run->kind == REIST_HTML_ELEMENT_ANCHOR) continue;
            uint32_t needed = (run->style & REIST_HTML_STYLE_LINK) ? 2U : 1U;
            if (commands + needed > BROWSER_VISIBLE_RUN_BUDGET) break;
            commands += needed;
            if (run->kind == REIST_HTML_ELEMENT_LIST_MARKER) {
                char marker[12U];
                uint32_t marker_length = 0U;
                if (run->text_length == 0U) {
                    marker[marker_length++] = '*';
                } else {
                    char digits[10U]; uint32_t count = 0U;
                    uint32_t value = run->text_length;
                    do {
                        digits[count++] = (char)('0' + value % 10U);
                        value /= 10U;
                    } while (value != 0U && count < sizeof(digits));
                    while (count != 0U)
                        marker[marker_length++] = digits[--count];
                    marker[marker_length++] = '.';
                }
                if (paint_text(client, run->x, (int32_t)screen_y, run->width,
                               marker, marker_length, dark, white,
                               BROWSER_BODY_FONT) != 0) return -1;
                continue;
            }
            if (run->kind == REIST_HTML_ELEMENT_IMAGE) {
                uint32_t image_index = run->text_offset;
                if (image_index >= document->image_count) return -1;
                if ((image_index >= BROWSER_IMAGE_CACHE_COUNT || !image_cache[state->active][image_index].decoded) &&
                    screen_y >= BROWSER_CONTENT_TOP && screen_y + BROWSER_BODY_FONT <= body_bottom) {
                    const char *alt = document->images[image_index].alt;
                    if (!alt[0]) alt = "[Bild]";
                    uint32_t length = (uint32_t)utf8_prefix_length(alt, REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U);
                    if (paint_text(client, run->x, (int32_t)screen_y, run->width,
                                   alt, length, muted, white, BROWSER_BODY_FONT) != 0) return -1;
                }
            }
            uint32_t foreground = run->style & REIST_HTML_STYLE_LINK ? link
                : run->style & (REIST_HTML_STYLE_HEADING_1 |
                                REIST_HTML_STYLE_HEADING_2 |
                                REIST_HTML_STYLE_HEADING_3) ? heading
                : run->style & REIST_HTML_STYLE_ITALIC ? 0x00405050U : dark;
            if (run->kind == REIST_HTML_ELEMENT_TEXT && paint_text(client, run->x, (int32_t)screen_y, run->width,
                           document->text + run->text_offset,
                           run->text_length, foreground, white,
                           run->height) != 0) return -1;
            if ((run->style & REIST_HTML_STYLE_LINK) &&
                run->link_index < document->link_count) {
                int64_t top = screen_y < BROWSER_CONTENT_TOP ? BROWSER_CONTENT_TOP : screen_y;
                int64_t bottom = screen_y + run->height;
                if (bottom > body_bottom) bottom = body_bottom;
                /* Preserve the link's actual bottom edge, not an artificial
                 * underline pinned to the viewport while an image scrolls. */
                int64_t underline = screen_y + (run->height >= 2U ? run->height - 2U : 0U);
                if (underline >= BROWSER_CONTENT_TOP && underline < body_bottom &&
                    reist_gui_surface_client_paint_fill(client,
                        (reist_gui_rect_t){run->x, (int32_t)underline,
                            run->width, 1U}, link) != 0) return -1;
                if (bottom > top && state->hit_count < BROWSER_LINK_HIT_CAPACITY)
                    state->hits[state->hit_count++] = (browser_link_hit_t){
                        {run->x, (int32_t)top, run->width, (uint32_t)(bottom - top)},
                        run->link_index};
            }
        }

    }
    browser_scrollbar_configure(&state->scrollbar, client->width, view,
        state->loaded ? layouts[state->active].total_height : 0U, state->scroll_y);
    browser_scrollbar_t *bar = &state->scrollbar;
    if (reist_gui_surface_client_paint_fill(client, bar->bounds, 0x00E0E0E0U) != 0 ||
        paint_button(client, bar->thumb) != 0) return -1;
    if (view > 36U) {
        if (paint_button(client, (reist_gui_rect_t){bar->bounds.x, bar->bounds.y, bar->bounds.width, 18U}) != 0 ||
            paint_button(client, (reist_gui_rect_t){bar->bounds.x, bar->bounds.y + (int32_t)view - 18, bar->bounds.width, 18U}) != 0 ||
            paint_text(client, bar->bounds.x + 5, bar->bounds.y, 12U, "^", 1U, dark, 0x00D4D0C8U, 16U) != 0 ||
            paint_text(client, bar->bounds.x + 5, bar->bounds.y + (int32_t)view - 18, 12U, "v", 1U, dark, 0x00D4D0C8U, 16U) != 0) return -1;
    }
    int result = render_result("base-commit", reist_gui_surface_client_paint_commit(client));
    if (result == 0) { state->redraw = 0U; state->painted_scroll=state->scroll_y; ++state->body_frames; workspace->forms_redraw=1; }
    timing_end(TIME_BODY,measured_at);
    return result;
}
/* HOVER contains only the top chrome, never page or image operations. */
static int render_chrome(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t measured_at=timing_start();
    uint32_t background = state->address_focused ? 0x00FFFDE0U : 0x00FFFFFFU;
    if (reist_gui_surface_client_paint_begin_layer(client, REIST_GUI_SURFACE_PAINT_LAYER_HOVER) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){0, 0, client->width, BROWSER_CONTENT_TOP}, 0x00D4D0C8U) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){10, 10, client->width > 20U ? client->width - 20U : 1U, 32U}, background) != 0) return -1;
    uint32_t cells = client->width > 40U ? (client->width - 40U) / 8U : 1U;
    if (cells > BROWSER_URL_CAPACITY - 1U) cells = BROWSER_URL_CAPACITY - 1U;
    if (state->address_cursor < state->address_start) state->address_start = state->address_cursor;
    if (state->address_cursor >= state->address_start + cells) state->address_start = state->address_cursor - cells + 1U;
    uint32_t count = state->address_length - state->address_start;
    if (count > cells) count = cells;
    for (uint32_t at = 0; at < count;) {
        uint32_t amount = count - at;
        if (amount >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY) amount = REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U;
        amount = (uint32_t)utf8_prefix_length(state->address + state->address_start + at, amount);
        if (amount == 0U) break;
        if (paint_text(client, 16 + (int32_t)at * 8, 18, amount * 8U,
            state->address + state->address_start + at, amount, 0x00202020U, background, 16U) != 0) return -1;
        at += amount;
    }
    if (state->address_focused && reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){
        16 + (int32_t)(state->address_cursor - state->address_start) * 8, 17, 1U, 18U}, 0x00202020U) != 0) return -1;
    static const char help[] = "Enter: URL  R: neu laden  Esc: Abbruch";
    if (paint_text(client, 12, 51, client->width > 24U ? client->width - 24U : 1U,
        help, sizeof(help) - 1U, 0x00606060U, 0x00D4D0C8U, 14U) != 0) return -1;
    int result = reist_gui_surface_client_paint_commit_layer(client, REIST_GUI_SURFACE_PAINT_LAYER_HOVER);
    if (result == 0) { state->chrome_redraw = 0U; ++state->chrome_frames; }
    timing_end(TIME_CHROME,measured_at);
    return result;
}
static int render_status(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t measured_at=timing_start();
    uint32_t top = client->height > BROWSER_STATUS_HEIGHT ? client->height - BROWSER_STATUS_HEIGHT : 0U;
    if (reist_gui_surface_client_paint_begin_layer(client, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){0, (int32_t)top, client->width, BROWSER_STATUS_HEIGHT}, 0x00D4D0C8U) != 0) return -1;
    uint32_t count = (uint32_t)utf8_prefix_length(state->status, REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U);
    if (paint_text(client, 8, (int32_t)top + 3, client->width > 16U ? client->width - 16U : 1U,
        state->status, count, 0x00202020U, 0x00D4D0C8U, 14U) != 0) return -1;
    int result = reist_gui_surface_client_paint_commit_layer(client, REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY);
    if (result == 0) state->status_redraw = 0U;
    timing_end(TIME_STATUS,measured_at);
    return result;
}
static const browser_scene_run_t *form_run(const browser_state_t *state,uint32_t index) {
    if(!state->loaded) return NULL;
    const browser_scene_t *s=&scenes[state->active];
    for(uint32_t i=0;i<s->count;++i) if(s->runs[i].kind==BROWSER_SCENE_CONTROL && s->runs[i].offset==index) return &s->runs[i];
    return NULL;
}
/* Only the focused native control is a dynamic overlay. Typing never rebuilds
 * the page buffer, its CSS scene, or the immutable resource generation. */
static int render_form_focus(browser_state_t *state,reist_gui_surface_client_t *client) {
    if(reist_gui_surface_client_paint_begin_layer(client,REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC)) return -1;
    const browser_form_state_t *fs=&workspace->forms[state->active];
    const browser_scene_run_t *r=state->address_focused ? NULL : form_run(state,fs->focus);
    int32_t y=r ? (int32_t)BROWSER_CONTENT_TOP+r->y-(int32_t)state->scroll_y : 0;
    if(r && r->x>=0 && r->x+(int32_t)r->width<=(int32_t)client->width-(int32_t)BROWSER_SCROLLBAR_WIDTH &&
       y>=(int32_t)BROWSER_CONTENT_TOP && y+(int32_t)r->height<=(int32_t)(BROWSER_CONTENT_TOP+viewport_height(client))) {
        const browser_forms_t *m=&scenes[state->active].forms;
        const browser_form_control_t *c=&m->controls[fs->focus];
        reist_gui_rect_t rect={r->x,y,r->width,r->height};
        uint32_t background=0xffffff,foreground=c->flags&BROWSER_FORM_DISABLED ? 0x808080 : 0x202020;
        if(c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO || c->kind==BROWSER_FORM_SELECT) {
            /* Keep the native indicator from the current page raster. Focus
             * must not replace a radio circle or select arrow with a textbox. */
            if(reist_gui_surface_client_paint_fill(client,(reist_gui_rect_t){r->x,y,r->width,1},0x203070) ||
               reist_gui_surface_client_paint_fill(client,(reist_gui_rect_t){r->x,y+(int32_t)r->height-1,r->width,1},0x203070)) return -1;
            goto commit_form_focus;
        }
        int button=c->kind>=BROWSER_FORM_SUBMIT && c->kind<=BROWSER_FORM_BUTTON;
        if(button) { if(paint_button(client,rect)) return -1; background=0xd4d0c8; }
        else if(reist_gui_surface_client_paint_fill(client,rect,0x203070) ||
                (r->width>2 && r->height>2 && reist_gui_surface_client_paint_fill(client,
                    (reist_gui_rect_t){rect.x+1,rect.y+1,rect.width-2,rect.height-2},background))) return -1;
        const char *v=browser_forms_value(m,fs,fs->focus);
        if(button) v=m->strings+c->label;
        if(c->kind==BROWSER_FORM_UNSUPPORTED) v="[unsupported]";
        if(c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO) v=fs->checked[fs->focus] ? "x" : "";
        if(c->kind==BROWSER_FORM_SELECT) {
            v=""; for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j)
                if(fs->selected[j]) { v=m->strings+m->options[j].label; break; }
        }
        uint32_t cells=r->width>8 ? (r->width-8)/8 : 0,rows=r->height>=20 ? (r->height-4)/16 : 0;
        if(cells>32) cells=32; /* bounded native text-command budget */
        if(rows>8) rows=8;
        if(c->kind!=BROWSER_FORM_TEXTAREA && rows>1) rows=1;
        int32_t text_x=r->x+4,text_y=y+4;
        if(button) {
            uint32_t count=0;
            for(uint32_t j=0;v[j] && count<=cells;++j) if(((uint8_t)v[j]&192)!=128) ++count;
            if(count<=cells) text_x=r->x+((int32_t)r->width-(int32_t)count*8)/2;
            if(r->height>=16) text_y=y+((int32_t)r->height-16)/2;
        }
        uint32_t at=0,cursor_row=0,cursor_col=0;
        int edit=c->kind==BROWSER_FORM_TEXT || c->kind==BROWSER_FORM_TEXTAREA;
        if(edit && cells) for(uint32_t j=0;j<fs->cursor;++j) {
            if(v[j]=='\n') { ++cursor_row; cursor_col=0; }
            else if(((uint8_t)v[j]&192)!=128 && ++cursor_col==cells) { ++cursor_row; cursor_col=0; }
        }
        uint32_t first=cursor_row>=rows && rows ? cursor_row-rows+1 : 0,row=0;
        while(cells && rows && row<first+rows) {
            uint32_t start=at,count=0;
            while(v[at] && v[at]!='\n' && v[at]!='\r' && count<cells) {
                ++at; while(((uint8_t)v[at]&192)==128) ++at; ++count;
            }
            if(row>=first) {
                uint32_t part=start,px=0;
                while(part<at) {
                    uint32_t amount=at-part; if(amount>39) amount=39;
                    amount=(uint32_t)utf8_prefix_length(v+part,amount);
                    if(!amount || paint_text(client,text_x+(int32_t)px*8,text_y+(int32_t)(row-first)*16,
                        (cells-px)*8,v+part,amount,foreground,background,16)) return -1;
                    for(uint32_t j=0;j<amount;++j) if(((uint8_t)v[part+j]&192)!=128) ++px;
                    part+=amount;
                }
            }
            if(v[at]=='\n' || v[at]=='\r') { if(v[at]=='\r' && v[at+1]=='\n') ++at; ++at; }
            else if(!v[at]) break;
            ++row;
        }
        if(edit && cells && rows && !(c->flags&BROWSER_FORM_READONLY) &&
            reist_gui_surface_client_paint_fill(client,(reist_gui_rect_t){r->x+4+(int32_t)cursor_col*8,
                y+4+(int32_t)(cursor_row-first)*16,1,16},foreground)) return -1;
    }
commit_form_focus:;
    int rc=reist_gui_surface_client_paint_commit_layer(client,REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC);
    if(!rc) workspace->forms_redraw=0;
    return rc;
}
static int render(browser_state_t *state, reist_gui_surface_client_t *client) {
    if (state->redraw && render_body(state, client) != 0) {
        x86os_puts("BROWSER_RENDER_STATE scroll="); x86os_print_number((int)state->scroll_y);
        x86os_puts(" frames="); x86os_print_number((int)state->body_frames); x86os_puts("\n");
        x86os_puts("BROWSER_PROBE_FAIL render-body\n"); return -1;
    }
    if(workspace->forms_redraw && render_form_focus(state,client)) return -1;
    if (state->chrome_redraw && render_chrome(state, client) != 0) {
        x86os_puts("BROWSER_PROBE_FAIL render-chrome\n"); return -1;
    }
    if (state->status_redraw && render_status(state, client) != 0) {
        x86os_puts("BROWSER_PROBE_FAIL render-status\n"); return -1;
    }
    return 0;
}

static uint32_t link_at(browser_state_t *state, reist_gui_surface_client_t *client, int32_t x, int32_t y) {
    if (y < (int32_t)BROWSER_CONTENT_TOP || y >= (int32_t)(BROWSER_CONTENT_TOP + viewport_height(client))) return UINT32_MAX;
    for (uint32_t i = 0; i < state->hit_count; ++i)
        if (browser_point_in_rect(state->hits[i].rect, x, y)) return state->hits[i].link_index;
    return UINT32_MAX;
}
static void activate_link(browser_state_t *state, reist_gui_surface_client_t *client, uint32_t index) {
    char resolved[BROWSER_URL_CAPACITY];
    if (!state->loaded || index >= documents[state->active].link_count) return;
    if (reist_html_url_resolve(state->active_url, documents[state->active].links[index].href,
                               resolved, sizeof(resolved)) != 0) {
        set_status(state, "Linkziel nicht unterstuetzt"); return;
    }
    if (navigate(state, client, resolved) == 0) x86os_puts("BROWSER_LINK_OK\n");
}
static int resource_probe_begin(browser_state_t *state,reist_gui_surface_client_t *client);
static void form_submit(browser_state_t *state,reist_gui_surface_client_t *client,uint32_t index) {
    char target[BROWSER_URL_CAPACITY]; browser_form_state_t *fs=&workspace->forms[state->active];
    int rc=browser_forms_submit(&scenes[state->active].forms,fs,fs->generation,index,state->active_url,target,sizeof(target));
    if(rc) {
        set_status(state,browser_forms_error(rc));
        if(state->probe==4) { ++form_probe.rejected; x86os_puts("BROWSER_FORMS_REJECT count=");
            x86os_print_number((int)form_probe.rejected); x86os_puts("\n"); }
    }
    else (void)navigate(state,client,target);
}
static uint32_t form_at(browser_state_t *state,reist_gui_surface_client_t *client,int32_t x,int32_t y) {
    if(!state->loaded || x<0 || x>=(int32_t)client->width-(int32_t)BROWSER_SCROLLBAR_WIDTH ||
        y<(int32_t)BROWSER_CONTENT_TOP || y>=(int32_t)(BROWSER_CONTENT_TOP+viewport_height(client))) return BROWSER_FORM_NONE;
    const browser_scene_t *s=&scenes[state->active]; uint32_t label=BROWSER_FORM_NONE;
    for(uint32_t i=s->count;i>0;--i) {
        const browser_scene_run_t *r=&s->runs[i-1];
        if(r->kind!=BROWSER_SCENE_CONTROL || !browser_point_in_rect((reist_gui_rect_t){r->x,
            (int32_t)BROWSER_CONTENT_TOP+r->y-(int32_t)state->scroll_y,r->width,r->height},x,y)) continue;
        if(s->forms.controls[r->offset].kind!=BROWSER_FORM_LABEL) return r->offset;
        label=r->offset;
    }
    return label;
}
static int resource_probe_selected(const browser_state_t *state,
                                  const reist_gui_surface_input_t *input) {
    /* Only the existing trusted probe's initial phase admits this selector.
     * USB input avoids the shell's still-shared terminal keyboard queue. */
    return state->probe==1 && state->probe_phase==0 && !state->address_focused &&
        input->delta_y==-REIST_GUI_SURFACE_SCROLL_STEP;
}
static void handle_pointer(browser_state_t *state, reist_gui_surface_client_t *client,
                            const reist_gui_surface_input_t *input) {
    browser_form_state_t *fs=&workspace->forms[state->active];
    if (input->type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL) {
        if (!reist_gui_surface_scroll_valid(input) ||
            state->scrollbar.state.captured || input->x<0 || (uint32_t)input->x>=client->width ||
            input->y<(int32_t)BROWSER_CONTENT_TOP || (uint32_t)input->y>=BROWSER_CONTENT_TOP+viewport_height(client)) return;
        if (resource_probe_selected(state,input)) {
            if(resource_probe_begin(state,client)) {
                x86os_puts("BROWSER_PROBE_FAIL resource-setup\n"); state->exit_requested=1;
            }
            return;
        }
        if (state->probe==3U && state->probe_phase==1U && !state->address_focused &&
            input->delta_y==-REIST_GUI_SURFACE_SCROLL_STEP) {
            state->probe=6U; state->probe_phase=0U;
            x86os_puts("BROWSER_MODEL_SELECTED\n");
            return;
        }
        if (!state->loaded) return;
        if (state->probe==6U) {
            ++model_probe.pending;
            model_probe.kind=input->delta_y==REIST_GUI_SURFACE_SCROLL_STEP ? 2U :
                input->delta_y==-REIST_GUI_SURFACE_SCROLL_STEP ? 3U : 0U;
        }
        fs->capture=BROWSER_FORM_NONE;
        /* Quotient/remainder decomposition avoids a signed 64-bit division on
         * i386. abs(remainder)<120, abs(whole*48)<859 million: no int32 overflow. */
        int32_t whole=input->delta_y/REIST_GUI_SURFACE_SCROLL_STEP;
        int32_t fraction=(input->delta_y%REIST_GUI_SURFACE_SCROLL_STEP)*48+state->wheel_remainder;
        int64_t desired=(int64_t)state->scroll_y+whole*48+fraction/REIST_GUI_SURFACE_SCROLL_STEP;
        state->wheel_remainder=fraction%REIST_GUI_SURFACE_SCROLL_STEP;
        uint32_t previous=state->scroll_y;
        set_scroll(state,client,desired);
        if (desired<0 || (uint64_t)desired>maximum_scroll(state,client)) state->wheel_remainder=0;
        if (state->probe && state->scroll_y>previous) state->probe_wheel_down=1;
        if (state->probe && state->scroll_y<previous) state->probe_wheel_up=1;
        return; /* Existing bounded redraw path; no parse, load or focus change. */
    }
    uint32_t motion = input->type == REIST_GUI_SURFACE_INPUT_POINTER_MOTION;
    if (!motion && (input->type != REIST_GUI_SURFACE_INPUT_POINTER_BUTTON || input->button != 1U)) return;
    if (browser_scrollbar_pointer(&state->scrollbar, motion, input->pressed, input->x, input->y)) {
        fs->capture=BROWSER_FORM_NONE;
        state->armed_link = UINT32_MAX;
        set_scroll(state, client, state->scrollbar.state.value);
        return;
    }
    if (motion) {
        if (link_at(state, client, input->x, input->y) != state->armed_link) state->armed_link = UINT32_MAX;
        return;
    }
    if (input->pressed && browser_point_in_rect((reist_gui_rect_t){10, 10,
        client->width > 20U ? client->width - 20U : 1U, 32U}, input->x, input->y)) {
        if(fs->focus<scenes[state->active].forms.control_count) state->redraw=1;
        fs->capture=fs->focus=BROWSER_FORM_NONE; workspace->forms_redraw=1;
        if (!state->address_focused) {
            state->address_focused = 1U; state->address_replace_pending = 1U;
            state->address_cursor = state->address_length;
        } else {
            uint32_t cursor = state->address_start + (uint32_t)(input->x > 16 ? input->x - 16 : 0) / 8U;
            state->address_cursor = cursor > state->address_length ? state->address_length : cursor;
            state->address_replace_pending = 0U;
        }
        state->armed_link = UINT32_MAX;
        state->chrome_redraw = 1U; return;
    }
    uint32_t control=form_at(state,client,input->x,input->y);
    if(control!=BROWSER_FORM_NONE || fs->capture!=BROWSER_FORM_NONE) {
        const browser_forms_t *m=&scenes[state->active].forms;
        state->armed_link=UINT32_MAX;
        if(input->pressed) {
            if(fs->focus!=control && fs->focus<m->control_count) state->redraw=1;
            fs->capture=control;
            if(control!=BROWSER_FORM_NONE && !browser_forms_focus(m,fs,control)) {
                state->address_focused=0; state->chrome_redraw=1; workspace->forms_redraw=1;
            }
        } else {
            uint32_t armed=fs->capture; fs->capture=BROWSER_FORM_NONE;
            if(armed!=BROWSER_FORM_NONE && armed==control) {
                const browser_form_control_t *c=&m->controls[control];
                if(c->flags&BROWSER_FORM_DISABLED) return;
                if(c->kind==BROWSER_FORM_SUBMIT) form_submit(state,client,control);
                else {
                    int rc=browser_forms_activate(m,fs,control);
                    if(state->probe==4 && c->kind==BROWSER_FORM_RESET && rc>0) ++form_probe.resets;
                    if(c->kind==BROWSER_FORM_SELECT) rc=browser_forms_key(m,fs,259);
                    if(rc<0) set_status(state,browser_forms_error(rc));
                    if(rc>0) { state->address_focused=0; state->chrome_redraw=1; workspace->forms_redraw=1;
                        if(c->kind!=BROWSER_FORM_TEXT && c->kind!=BROWSER_FORM_TEXTAREA) state->redraw=1; }
                }
            }
        }
        return;
    }
    if(input->pressed) { if(fs->focus<scenes[state->active].forms.control_count) state->redraw=1;
        fs->focus=fs->capture=BROWSER_FORM_NONE; workspace->forms_redraw=1; }
    uint32_t hit = link_at(state, client, input->x, input->y);
    if (input->pressed) {
        if (state->address_focused) state->chrome_redraw = 1U;
        state->address_focused = 0U; state->armed_link = hit;
    } else {
        uint32_t armed = state->armed_link;
        state->armed_link = UINT32_MAX;
        if (armed != UINT32_MAX && armed == hit) activate_link(state, client, armed);
    }
}
static int resource_probe_write(const char *path,const char *text,uint32_t bit) {
    uint32_t length=(uint32_t)bounded_length(text,1024);
    if(length==1024) return -28;
    int fd=x86os_create(path); if(fd<0) return fd;
    resource_probe_files|=bit;
    int written=x86os_write(fd,text,length), closed=x86os_close(fd);
    return written==(int)length && !closed ? 0 : -5;
}
static int resource_probe_begin(browser_state_t *state,reist_gui_surface_client_t *client) {
    copy_text(resource_probe_html,sizeof(resource_probe_html),state->temporary_path);
    copy_text(resource_probe_css,sizeof(resource_probe_css),state->temporary_path);
    uint32_t path_length=(uint32_t)bounded_length(state->temporary_path,sizeof(state->temporary_path));
    if(path_length>=sizeof(state->temporary_path) ||
        copy_text(resource_probe_html+path_length,sizeof(resource_probe_html)-path_length,".html") ||
        copy_text(resource_probe_css+path_length,sizeof(resource_probe_css)-path_length,".css")) return -28;
    /* Reuse the packaged styles for source order, media, duplicates and a
     * cycle. Only a PID-scoped probe file gains a mutable final stylesheet. */
    char html[1024];
    /* Absolute base for the read-only packaged styles; no <base> parser needed. */
    const char *parts[]={"<link rel=stylesheet href='/htdocs/browser-stylesheet-main.css'>",
        "<link rel=stylesheet href='/htdocs/./browser-stylesheet-main.css#duplicate'>",
        "<link rel='alternate stylesheet' href='/never-fetch.css'>",
        "<link rel=stylesheet href='",resource_probe_css,"'><div class=external><p>External CSS</p></div>"};
    uint32_t used=0;
    for(unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);++i) {
        if(copy_text(html+used,sizeof(html)-used,parts[i])) return -28;
        used+=(uint32_t)bounded_length(parts[i],sizeof(html)-used);
    }
    if(resource_probe_write(resource_probe_html,html,1) || resource_probe_write(resource_probe_css,"/* initial empty sheet */",2)) return -5;
    state->probe=2; state->probe_phase=0; state->parse_mode=0;
    state->address_focused=0;
    x86os_puts("BROWSER_RESOURCES_STARTED\n");
    return navigate(state,client,resource_probe_html);
}
static void handle_keyboard(browser_state_t *state, reist_gui_surface_client_t *client, uint32_t key) {
    browser_form_state_t *fs=&workspace->forms[state->active];
    const browser_forms_t *fm=&scenes[state->active].forms;
    if (key == BROWSER_KEY_ESCAPE || key == 27U) {
        if (state->child_pid > 0 || state->pending || state->follow_redirect || state->parse_pending || state->resource_loading) {
            state->pending = 0U; state->image_next = BROWSER_IMAGE_CACHE_COUNT;
            state->parse_pending = 0U;
            state->follow_redirect = 0U;
            cancel_fetch(state); abandon_resources(state); set_status(state, "Laden abgebrochen");
        } else if (state->address_focused) {
            state->address_focused = 0U; state->chrome_redraw = 1U;
        } else if(state->loaded && fs->focus<fm->control_count) {
            fs->focus=fs->capture=BROWSER_FORM_NONE; workspace->forms_redraw=1; state->redraw=1;
        } else state->exit_requested = 1U;
        return;
    }
    if (state->address_focused) {
        if (key == '\r' || key == '\n') {
            char target[BROWSER_URL_CAPACITY];
            copy_text(target, sizeof(target), state->address);
            state->address_focused = 0U; state->chrome_redraw = 1U;
            (void)navigate(state, client, target);
        } else if (browser_address_edit(state->address, sizeof(state->address),
            &state->address_length, &state->address_cursor, &state->address_replace_pending, key) > 0)
            state->chrome_redraw = 1U;
        return;
    }
    if(key=='\t' && state->loaded) {
        if(fs->focus<fm->control_count) state->redraw=1;
        uint32_t index=fs->focus;
        for(uint32_t i=0;i<fm->control_count;++i) {
            index=index==BROWSER_FORM_NONE || index+1>=fm->control_count ? 0 : index+1;
            const browser_scene_run_t *r=form_run(state,index);
            if(r && !browser_forms_focus(fm,fs,index)) {
                if(r->y<(int32_t)state->scroll_y) set_scroll(state,client,r->y);
                else if(r->y+(int32_t)r->height>(int32_t)(state->scroll_y+viewport_height(client)))
                    set_scroll(state,client,(int64_t)r->y+r->height-viewport_height(client));
                workspace->forms_redraw=1; break;
            }
        }
        return;
    }
    if(state->loaded && fs->focus<fm->control_count) {
        uint32_t kind=fm->controls[fs->focus].kind;
        if((key=='\r' || key=='\n') && kind!=BROWSER_FORM_TEXTAREA) {
            uint32_t submit=fs->focus;
            if(kind==BROWSER_FORM_TEXT) {
                submit=BROWSER_FORM_NONE;
                for(uint32_t i=0;i<fm->control_count;++i) if(fm->controls[i].kind==BROWSER_FORM_SUBMIT &&
                    fm->controls[i].owner==fm->controls[fs->focus].owner) { submit=i; break; }
            }
            if(submit<fm->control_count && fm->controls[submit].kind==BROWSER_FORM_SUBMIT) form_submit(state,client,submit);
            else if(kind==BROWSER_FORM_RESET) { (void)browser_forms_activate(fm,fs,fs->focus); state->redraw=1; }
            else set_status(state,"Formular: explizite Senden-Schaltflaeche fehlt");
        } else {
            int rc=browser_forms_key(fm,fs,key);
            if(state->probe==4 && rc==-75) {
                ++form_probe.limit_refusals;
                x86os_puts("BROWSER_FORMS_MAXLENGTH_REFUSED\n");
            }
            if(rc<0) set_status(state,browser_forms_error(rc));
            if(rc>0) { workspace->forms_redraw=1; if(kind!=BROWSER_FORM_TEXT && kind!=BROWSER_FORM_TEXTAREA) state->redraw=1; }
        }
        return;
    }
    uint32_t page = viewport_height(client);
    if (key == BROWSER_KEY_UP) set_scroll(state, client, (int64_t)state->scroll_y - 24);
    else if (key == BROWSER_KEY_DOWN) set_scroll(state, client, (int64_t)state->scroll_y + 24);
    else if (key == BROWSER_KEY_PAGE_UP) set_scroll(state, client, (int64_t)state->scroll_y - page);
    else if (key == BROWSER_KEY_PAGE_DOWN || key == ' ') set_scroll(state, client, (int64_t)state->scroll_y + page);
    else if (key == BROWSER_KEY_HOME) set_scroll(state, client, 0);
    else if (key == BROWSER_KEY_END) set_scroll(state, client, maximum_scroll(state, client));
    else if (key == 'r' || key == 'R') {
        char path[BROWSER_URL_CAPACITY];
        if (strip_fragment(state->active_url, path, sizeof(path)) == 0) (void)navigate(state, client, path);
    } else if (key == '\r' || key == '\n') {
        state->address_focused = state->address_replace_pending = 1U;
        state->address_cursor = state->address_length; state->chrome_redraw = 1U;
    }
}
static int resource_probe_pixels(browser_state_t *state,reist_gui_surface_client_t *client,uint32_t color) {
    if(!state->loaded || !text_equal(state->active_url,resource_probe_html) ||
        workspace->resources[state->active].count!=3 || render(state,client)) return -1;
    uint32_t text=0,box=0;
    const browser_scene_t *s=&scenes[state->active];
    for(uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i];
        if(r->kind==1 && r->length==12 && !memcmp(documents[state->active].text+r->offset,"External CSS",12)) {
            if(r->color!=(0xff000000U|color) || r->height!=20 || r->x<0 || r->y<0 ||
                (uint32_t)r->x+r->width>client->width || (uint32_t)r->y+r->height>viewport_height(client)) return -1;
            for(uint32_t y=0;y<r->height;++y) for(uint32_t x=0;x<r->width;++x)
                if(surface_pixels[(BROWSER_CONTENT_TOP+(uint32_t)r->y+y)*client->width+(uint32_t)r->x+x]==color) text=1;
        }
        if(r->kind==BROWSER_SCENE_FILL && r->color==0xffe0f0ff) box=1;
    }
    return text && box ? 0 : -1;
}
static int resource_probe_step(browser_state_t *state,reist_gui_surface_client_t *client) {
    if(state->probe_phase==2 && state->resource_loading && !state->child_pid) {
        handle_keyboard(state,client,BROWSER_KEY_ESCAPE);
        if(state->resource_loading || state->parse_pending || state->child_pid || resource_probe_pixels(state,client,0x135724)) return -1;
        x86os_puts("BROWSER_RESOURCES_CANCEL_OK\n");
        handle_keyboard(state,client,'r'); ++state->probe_phase; return state->pending ? 0 : -1;
    }
    if(state->pending || state->parse_pending || state->resource_loading || state->follow_redirect || state->child_pid) return 0;
    if(state->probe_phase==0) {
        if(resource_probe_pixels(state,client,0x135724)) return -1;
        resource_probe_generation=workspace->resources[state->active].generation;
        resource_probe_failures=state->parser_failures;
        x86os_puts("BROWSER_RESOURCES_CASCADE_PIXELS_OK\nBROWSER_RESOURCES_DEDUPE_CYCLE_OK\n");
        if(x86os_unlink(resource_probe_css)) return -1;
        resource_probe_files&=~2U;
        handle_keyboard(state,client,'r');
    } else if(state->probe_phase==1) {
        if(workspace->resources[state->active].generation!=resource_probe_generation ||
            resource_probe_pixels(state,client,0x135724)) return -1;
        x86os_puts("BROWSER_RESOURCES_FAILURE_CONTAINED_OK\n");
        if(resource_probe_write(resource_probe_css,".external p {color:#2468ac !important}",2)) return -1;
        handle_keyboard(state,client,'r');
    } else if(state->probe_phase==3) {
        if(resource_probe_pixels(state,client,0x2468ac) ||
            workspace->resources[state->active].generation<=resource_probe_generation ||
            state->parser_failures!=resource_probe_failures) return -1;
        x86os_puts("BROWSER_RESOURCES_RELOAD_FRESH_OK\nBROWSER_RESOURCES_RECOVERY_OK\n");
        state->exit_requested=1;
    } else return -1;
    ++state->probe_phase; return 0;
}
static int probe_step(browser_state_t *state, reist_gui_surface_client_t *client) {
    uint32_t count = documents[state->active].image_count;
    if (count > BROWSER_IMAGE_CACHE_COUNT) count = BROWSER_IMAGE_CACHE_COUNT;
    if (state->pending || state->parse_pending || state->resource_loading || state->follow_redirect || state->reflow_pending || state->child_pid > 0 || state->image_next < count) return 0;
    if (!state->loaded) return -1;
    if (state->probe_phase == 0U) {
        if (!image_cache[state->active][0].decoded) return -1;
        x86os_puts("BROWSER_IMAGE_PAINTED\n");
        uint32_t body = state->body_frames;
        state->address_focused = 0U;
        reist_gui_surface_input_t click = {0};
        click.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON; click.button = click.pressed = 1U; click.x = click.y = 20;
        handle_pointer(state, client, &click);
        static const char typed[] = "/htdocs/browser-test.html";
        for (uint32_t i = 0; typed[i]; ++i) {
            handle_keyboard(state, client, (uint8_t)typed[i]);
            if (state->redraw || render(state, client) != 0 || state->body_frames != body) return -1;
        }
        if (!text_equal(state->address, typed)) return -1;
        x86os_puts("BROWSER_ADDRESS_CHROME_ONLY_OK\nBROWSER_ADDRESS_REPLACE_OK\n");
        char normalized[BROWSER_URL_CAPACITY];
        if (reist_html_navigation_normalize("example.test/docs", normalized, sizeof(normalized)) != 0 ||
            !text_equal(normalized, "https://example.test/docs")) return -1;
        x86os_puts("BROWSER_HTTPS_DEFAULT_OK\n");
        handle_keyboard(state, client, '\n');
    } else if (state->probe_phase == 1U) {
        if (!state->hit_count) return -1;
        reist_gui_surface_input_t click = {0};
        click.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON; click.button = click.pressed = 1U;
        click.x = state->hits[0].rect.x + 1; click.y = state->hits[0].rect.y + 1;
        uint32_t old_scroll = state->scroll_y;
        handle_pointer(state, client, &click);
        if (state->scroll_y != old_scroll || state->pending) return -1;
        click.pressed = 0U; click.x = -1; handle_pointer(state, client, &click);
        if (state->scroll_y != old_scroll || state->pending) return -1;
        click.x = state->hits[0].rect.x + 1; click.pressed = 1U;
        handle_pointer(state, client, &click);
        click.pressed = 0U; handle_pointer(state, client, &click);
        if (state->scroll_y == old_scroll || state->pending) return -1;
        x86os_puts("BROWSER_LINK_RELEASE_OK\nBROWSER_ANCHOR_OK\n");
    } else if (state->probe_phase == 2U) {
        set_scroll(state, client, 0);
        if (render(state, client) != 0 || !maximum_scroll(state, client)) return -1;
        reist_gui_surface_input_t click = {0};
        click.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON; click.button = click.pressed = 1U;
        click.x = state->scrollbar.thumb.x + 5; click.y = state->scrollbar.thumb.y + 7;
        handle_pointer(state, client, &click);
        if (!state->scrollbar.state.captured || state->scroll_y != 0U) return -1;
        click.type = REIST_GUI_SURFACE_INPUT_POINTER_MOTION; click.y += 70;
        handle_pointer(state, client, &click);
        if (!state->scroll_y) return -1;
        click.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON; click.pressed = 0U; click.x = -1;
        handle_pointer(state, client, &click);
        if (state->scrollbar.state.captured) return -1;
        x86os_puts("BROWSER_SCROLLBAR_CAPTURE_OK\nBROWSER_SCROLL_OK\n");
        /* The real Surface validator must accept partly visible linked
         * images and a failed image's long UTF-8 alternative text. */
        for (uint32_t i = 0U; i < layouts[state->active].run_count; ++i) {
            const browser_layout_run_t *run = &layouts[state->active].runs[i];
            if (run->kind != REIST_HTML_ELEMENT_IMAGE) continue;
            int64_t positions[] = {(int64_t)run->y - viewport_height(client) + 1,
                run->y, (int64_t)run->y + run->height / 2U};
            for (uint32_t p = 0U; p < 3U; ++p) {
                set_scroll(state, client, positions[p]);
                if (render(state, client) != 0) return -1;
            }
        }
        x86os_puts("BROWSER_SCROLL_CLIP_OK\n");
        set_scroll(state, client, 0);
    } else if (state->probe_phase == 3U) {
        handle_keyboard(state, client, 'r');
        if (!state->pending) return -1;
        x86os_puts("BROWSER_RELOAD_OK\n");
    } else if (state->probe_phase == 4U) {
        x86os_puts("BROWSER_RELOAD_PAINTED\n");
        /* A real CURL child rejects a missing host before any network access.
         * Exercise normal nonzero exit/reaping even in the NIC-less guest. */
        if (start_fetch(state, "https://", 1U, 0U) != 0) return -1;
    } else if (state->probe_phase == 5U) {
        if (state->job_cancelled || state->exit_requested || !state->loaded ||
            is_network(state->active_url)) return -1;
        x86os_puts("BROWSER_TRANSPORT_EXIT_OK\n");
        state->parse_mode=1U;
        if (navigate(state,client,"/htdocs/browser-html5-test.html")!=0) return -1;
    } else if (state->probe_phase == 6U) {
        if (state->parser_failures!=1U || state->parser_timeouts ||
            !text_equal(state->active_url,"/htdocs/browser-test.html")) return -1;
        x86os_puts("BROWSER_HTML5_FAULT_CONTAINED_OK\n");
        state->parse_mode=2U;
        if (navigate(state,client,"/htdocs/browser-html5-test.html")!=0) return -1;
    } else if (state->probe_phase == 7U) {
        if (state->parser_failures!=2U || state->parser_timeouts!=1U ||
            !text_equal(state->active_url,"/htdocs/browser-test.html")) return -1;
        x86os_puts("BROWSER_HTML5_TIMEOUT_CONTAINED_OK\n");
        if (navigate(state,client,"/htdocs/browser-html5-test.html")!=0) return -1;
    } else if (state->probe_phase==8U) {
        if (!text_equal(state->active_url,"/htdocs/browser-html5-test.html") ||
            !text_equal(documents[state->active].title,"HTML5 Recovery") ||
            state->parser_failures!=2U || state->parser_timeouts!=1U) return -1;
        set_scroll(state, client, 0);
        if (render(state, client) != 0) return -1;
        x86os_puts("BROWSER_HTML5_RECOVERY_OK\n");
        const browser_scene_t *s=&scenes[state->active];
        uint32_t text_ok=0, box_ok=0;
        for (uint32_t i=0;i<s->count;++i) {
            const browser_scene_run_t *r=&s->runs[i];
            if (r->kind==1 && r->length==11 && !memcmp(documents[state->active].text+r->offset,"CSS Cascade",11)) {
                if (r->color!=0xff123456 || r->height!=20 || (r->flags&3)!=3) return -1;
                for (uint32_t y=0;y<r->height;++y) for (uint32_t x=0;x<r->width;++x)
                    if (surface_pixels[(BROWSER_CONTENT_TOP+(uint32_t)r->y+y)*client->width+(uint32_t)r->x+x]==0x123456) text_ok=1;
            }
            if (r->kind==BROWSER_SCENE_FILL && r->color==0xffe0f0ff) {
                if (r->width!=s->width/2+30 || r->x!=(int32_t)(s->width-r->width)/2 || r->y!=12 ||
                    surface_pixels[(BROWSER_CONTENT_TOP+(uint32_t)r->y+4)*client->width+(uint32_t)r->x+4]!=0xe0f0ff) return -1;
                box_ok=1;
            }
        }
        if (!text_ok || !box_ok) return -1;
        x86os_puts("BROWSER_CSS_PIXELS_OK\nBROWSER_CSS_RESIZE_WAIT width="); x86os_print_number((int)client->width);
        x86os_puts(" height="); x86os_print_number((int)client->height); x86os_puts("\n");
        state->probe_resize_width=client->width;
    } else if (state->probe_phase==9U) {
        if (client->width==state->probe_resize_width) return 0;
        if (scenes[state->active].width!=client->width-BROWSER_SCROLLBAR_WIDTH ||
            scenes[state->active].height!=viewport_height(client) || state->parser_failures!=2 ||
            render(state,client)) return -1;
        if (!maximum_scroll(state,client)) return -1;
        state->probe_wheel_down=state->probe_wheel_up=0;
        x86os_puts("BROWSER_CSS_RESIZE_OK\nBROWSER_WHEEL_WAIT\n");
    } else if (state->probe_phase==10U) {
        if (!state->probe_wheel_down) return 0;
        if (!state->scroll_y || state->painted_scroll!=state->scroll_y) return -1;
        x86os_puts("BROWSER_WHEEL_DOWN_OK\n");
    } else {
        if (!state->probe_wheel_up) return 0;
        if (state->scroll_y || state->painted_scroll) return -1;
        x86os_puts("BROWSER_WHEEL_UP_OK\n");
        state->exit_requested = 1U;
    }
    ++state->probe_phase;
    return 0;
}
static int model_probe_step(browser_state_t *state,reist_gui_surface_client_t *client) {
    if (!state->loaded || state->child_pid || state->pending || state->parse_pending ||
        state->resource_loading || state->reflow_pending || state->follow_redirect ||
        maximum_scroll(state,client)<48U) return -1;
    if (!state->probe_phase) {
        if (!state->address_focused) return 0;
        if (state->scroll_y || state->painted_scroll || model_probe.pending) return -1;
        model_probe.body=state->body_frames; model_probe.chrome=state->chrome_frames;
        x86os_puts("BROWSER_MODEL_READY width="); x86os_print_number((int)client->width);
        x86os_puts(" height="); x86os_print_number((int)client->height); x86os_puts("\n");
        state->probe_phase=1U;
        return 0;
    }
    if (!model_probe.pending) return 0;
    if (model_probe.pending!=1U || model_probe.ordinal>=64U) return -1;
    uint32_t n=model_probe.ordinal+1U;
    uint32_t y=n>32U && (n&1U) ? 48U : 0U;
    if (!state->address_focused || state->address_length!=(n<=32U ? n : 32U) ||
        state->address_cursor!=state->address_length || state->scroll_y!=y || state->painted_scroll!=y) return -1;
    for (uint32_t i=0;i<state->address_length;++i) if(state->address[i]!='x') return -1;
    if (n<=32U ? (model_probe.kind!=1U || state->chrome_frames!=model_probe.chrome+1U || state->body_frames!=model_probe.body) :
        (model_probe.kind!=((n&1U) ? 2U : 3U) || state->body_frames!=model_probe.body+1U || state->chrome_frames!=model_probe.chrome)) return -1;
    model_probe.ordinal=n; model_probe.pending=0U;
    model_probe.body=state->body_frames; model_probe.chrome=state->chrome_frames;
    x86os_puts("BROWSER_MODEL_COMMIT ordinal="); x86os_print_number((int)n);
    x86os_puts(" length="); x86os_print_number((int)state->address_length);
    x86os_puts(" scroll="); x86os_print_number((int)y);
    x86os_puts(" body="); x86os_print_number((int)model_probe.body);
    x86os_puts(" chrome="); x86os_print_number((int)model_probe.chrome); x86os_puts("\n");
    return 0;
}
static int input_probe_step(browser_state_t *state) {
    /* Assertions only: every edit/navigation originates in a real Surface
     * keyboard message. Do not synthesize calls to handle_keyboard here. */
    if (state->probe_phase == 0U) {
        uint32_t images = documents[state->active].image_count;
        if (images > BROWSER_IMAGE_CACHE_COUNT) images = BROWSER_IMAGE_CACHE_COUNT;
        if (!state->loaded || state->child_pid || state->pending ||
            state->parse_pending || state->resource_loading || state->reflow_pending ||
            state->follow_redirect || state->image_next < images) return 0;
        x86os_puts("BROWSER_INPUT_READY\n");
        state->probe_phase = 1U;
    } else if (state->probe_phase == 1U && state->address_focused &&
               text_equal(state->address,"https://intracom.at") &&
               state->address_cursor == state->address_length &&
               state->input_backspace == 1U && state->input_left == 1U &&
               state->input_right == 1U) {
        x86os_puts("BROWSER_INPUT_ADDRESS_OK\n");
        state->probe_phase = 2U;
    } else if (state->probe_phase == 2U && !state->address_focused &&
               state->loaded && !state->child_pid && !state->pending &&
               !state->parse_pending && !state->resource_loading &&
               text_equal(state->active_url,"/htdocs/index.html")) {
        x86os_puts("BROWSER_INPUT_NAVIGATION_OK\n");
        state->probe_phase = 3U;
    }
    return 0;
}
static void forms_probe_geometry(browser_state_t *state) {
    const browser_scene_t *s=&scenes[state->active];
    for(uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i]; if(r->kind!=BROWSER_SCENE_CONTROL) continue;
        x86os_puts("BROWSER_FORMS_CONTROL id="); x86os_print_number((int)r->offset);
        x86os_puts(" kind="); x86os_print_number((int)s->forms.controls[r->offset].kind);
        x86os_puts(" owner="); x86os_print_number((int)s->forms.controls[r->offset].owner);
        x86os_puts(" x="); x86os_print_number(r->x); x86os_puts(" y="); x86os_print_number(r->y);
        x86os_puts(" w="); x86os_print_number((int)r->width); x86os_puts(" h="); x86os_print_number((int)r->height); x86os_puts("\n");
    }
}
static int forms_probe_step(browser_state_t *state,reist_gui_surface_client_t *client) {
    if(!state->loaded || state->pending || state->parse_pending || state->resource_loading ||
       state->reflow_pending || state->child_pid || state->follow_redirect) return 0;
    const browser_forms_t *m=&scenes[state->active].forms; browser_form_state_t *fs=&workspace->forms[state->active];
    uint32_t query=BROWSER_FORM_NONE;
    for(uint32_t i=0;i<m->control_count;++i) if(m->controls[i].kind==BROWSER_FORM_TEXT &&
        !strcmp(m->strings+m->controls[i].name,"q")) { query=i; break; }
    const char *value=browser_forms_value(m,fs,query);
    if(state->probe_phase==0) {
        if(m->form_count!=4 || query==BROWSER_FORM_NONE || m->version!=BROWSER_FORMS_VERSION ||
            m->max_length_plus_one[query]!=6) return -1;
        form_probe.generation=fs->generation; form_probe.width=client->width;
        form_probe.request=state->html_request.request; form_probe.frames=state->body_frames;
        form_probe.failures=state->parser_failures;
        forms_probe_geometry(state); x86os_puts("BROWSER_FORMS_READY\n");
    } else if(state->probe_phase==1) {
        if(strcmp(value,"hello")) return 0;
        if(fs->generation!=form_probe.generation || state->html_request.request!=form_probe.request || state->body_frames!=form_probe.frames) return -1;
        x86os_puts("BROWSER_FORMS_EDIT_ONLY_OK\n");
    } else if(state->probe_phase==2) {
        if(!state->probe_wheel_down || !state->probe_wheel_up) return 0;
        if(strcmp(value,"hello") || fs->generation!=form_probe.generation || state->painted_scroll!=state->scroll_y ||
            form_probe.limit_refusals!=1 || fs->units[query]!=5 || !fs->dirty[query]) return -1;
        x86os_puts("BROWSER_FORMS_MAXLENGTH_STATE_OK\n");
        x86os_puts("BROWSER_FORMS_WHEEL_STATE_OK\n");
    } else if(state->probe_phase==3) {
        if(client->width==form_probe.width) return 0;
        if(strcmp(value,"hello") || fs->focus!=query || fs->generation!=form_probe.generation ||
            scenes[state->active].width!=client->width-BROWSER_SCROLLBAR_WIDTH) return -1;
        forms_probe_geometry(state); x86os_puts("BROWSER_FORMS_REFLOW_STATE_OK\n");
    } else if(state->probe_phase==4) {
        if(form_probe.rejected<3) return 0;
        if(form_probe.rejected!=3 || strcmp(value,"hello")) return -1;
        x86os_puts("BROWSER_FORMS_REJECTED_OK\n");
    } else if(state->probe_phase==5) {
        if(!form_probe.resets) return 0;
        if(strcmp(value,"start") || fs->dirty[query] || fs->units[query]!=5) return -1;
        x86os_puts("BROWSER_FORMS_RESET_OK\n");
    } else if(state->probe_phase==6) {
        if(strcmp(value,"hello")) return 0;
        x86os_puts("BROWSER_FORMS_SEND_READY\n");
    } else if(state->probe_phase==7) {
        if(strcmp(documents[state->active].title,"Forms result")) return 0;
        if(m->control_count || fs->generation<=form_probe.generation || fs->focus!=BROWSER_FORM_NONE) return -1;
        form_probe.generation=fs->generation; x86os_puts("BROWSER_FORMS_RESULT_OK\n");
        state->parse_mode=1; if(navigate(state,client,"/htdocs/browser-forms-test.html")) return -1;
    } else if(state->probe_phase==8) {
        if(state->parser_failures==form_probe.failures) return 0;
        if(state->parser_failures!=form_probe.failures+1 || strcmp(documents[state->active].title,"Forms result")) return -1;
        x86os_puts("BROWSER_FORMS_FAILURE_CONTAINED_OK\n");
        state->parse_mode=0; if(navigate(state,client,"/htdocs/browser-forms-test.html")) return -1;
    } else if(state->probe_phase==9) {
        if(query==BROWSER_FORM_NONE) return 0;
        if(strcmp(value,"start") || fs->generation<=form_probe.generation || fs->focus!=BROWSER_FORM_NONE) return -1;
        x86os_puts("BROWSER_FORMS_RECOVERY_OK\n"); state->exit_requested=1;
    }
    ++state->probe_phase; return 0;
}
static int public_probe_step(browser_state_t *state,reist_gui_surface_client_t *client) {
    if(!state->loaded || state->pending || state->parse_pending || state->resource_loading ||
        state->reflow_pending || state->child_pid || state->follow_redirect) return 0;
    if(state->image_next<documents[state->active].image_count) return 0;
    if(!state->probe_phase) {
        x86os_puts("BROWSER_PUBLIC_READY\n"); state->probe_phase=1; return 0;
    }
    if(state->probe_phase==1) {
        /* Host starts the bounded HTTP fixture before sending this real key. */
        if(!state->address_focused) return 0;
        state->probe_phase=2;
        return navigate(state,client,"http://10.0.2.2:18084/large");
    }
    const reist_html_document_t *d=&documents[state->active];
    const browser_scene_t *s=&scenes[state->active];
    uint32_t wanted=state->probe_phase==2 ? 36 : 64, found=0;
    const char label[]="caf\xc3\xa9 \xe2\x82\xac";
    for(uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i];
        if(r->kind==1 && r->height==wanted && r->length==sizeof(label)-1 &&
            !memcmp(d->text+r->offset,label,sizeof(label)-1)) found=1;
    }
    if(!found || state->parser_failures || state->painted_scroll!=state->scroll_y) return -1;
    if(state->probe_phase==2) {
        if(state->active_length<=65536 || state->active_encoding!=BROWSER_ENCODING_WINDOWS1252 ||
            strcmp(state->active_url,"http://10.0.2.2:18084/large")) return -1;
        if(d->image_count!=1 || !image_cache[state->active][0].decoded ||
            !s->image_urls[0][0] || bounded_length(s->image_urls[0],BROWSER_RESOURCE_URL_CAPACITY)<7800 ||
            workspace->resources[state->active].count!=2) return -1;
        x86os_puts("BROWSER_PUBLIC_LARGE_ENCODING_RASTER_OK\n");
        state->probe_phase=3;
        return navigate(state,client,"http://10.0.2.2:18084/redirect");
    }
    if(strcmp(state->active_url,"http://10.0.2.2:18084/done")) return -1;
    x86os_puts("BROWSER_PUBLIC_REDIRECT_RASTER_OK\n"); state->exit_requested=1; return 0;
}
static const char *initial_target(int argc, char **argv, uint32_t *probe) {
    const char *target = "/htdocs/index.html";
    for (int index = 1; index < argc; ++index) {
        if (text_equal(argv[index], "--browser-probe")) *probe = 1U;
        else if (text_equal(argv[index], "--browser-input-probe")) *probe = 3U;
        else if (text_equal(argv[index], "--browser-forms-probe")) *probe = 4U;
        else if (text_equal(argv[index], "--browser-public-probe")) *probe = 5U;
        else if (!text_prefix(argv[index], "--reist-surface="))
            target = argv[index];
    }
    return *probe==4 ? "/htdocs/browser-forms-test.html" : *probe ? "/htdocs/browser-test.html" : target;
}

int main(int argc, char **argv) {
    x86os_puts("BROWSER_BUILD html5-css-worker-20260905-r1\n");
    x86os_ipc_handle_t endpoint = 0U;
    if (reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("browser: compositor endpoint missing\n");
        return 2;
    }
    reist_gui_surface_client_t client;
    if (reist_gui_surface_client_init(&client, endpoint) != 0) return 1;
    int status = -9;
    for (uint32_t attempt = 0U; attempt < BROWSER_CREATE_ATTEMPTS; ++attempt) {
        status = reist_gui_surface_client_create(
            &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL, 800U, 600U);
        if (status == 0 || (status != -9 && status != -13)) break;
        (void)x86os_sleep_ms(1U);
    }
    if (status != 0 || reist_gui_surface_client_ack_configure(
            &client, client.configured_serial) != 0 ||
        reist_gui_surface_client_enable_scroll(&client) != 0 ||
        reist_gui_surface_client_set_title(&client, "REIST Web") != 0) {
        (void)x86os_ipc_release(endpoint);
        return 1;
    }


    /* Reserve the fixed private document/image/font workspace (at most 36 MiB).
     * Admission leaves process memory reserves and the executable limit intact. */
    workspace = x86os_malloc(sizeof(*workspace));
    if (workspace == 0 || browser_image_workspace(workspace->arena, sizeof(workspace->arena)) != 0) {
        if (workspace) x86os_free(workspace);
        (void)reist_gui_surface_client_destroy(&client);
        (void)x86os_ipc_release(endpoint);
        x86os_puts("browser: image workspace admission failed\n"); return 1;
    }
    memset(scenes,0,sizeof(scenes));
    memset(workspace->forms,0,sizeof(workspace->forms));
    workspace->forms[0].focus=workspace->forms[0].capture=BROWSER_FORM_NONE;
    workspace->forms[1].focus=workspace->forms[1].capture=BROWSER_FORM_NONE;
    workspace->forms_redraw=1;
    size_t font_length=(size_t)((uintptr_t)browser_font_end-(uintptr_t)browser_font_data);
    if (font_length>REIST_GUI_FONT_MAX_FILE_BYTES ||
        reist_gui_font_open_psf2(&workspace->font,browser_font_data,font_length,workspace->font_map,262144,'?')) {
        (void)reist_gui_surface_client_destroy(&client); (void)x86os_ipc_release(endpoint);
        x86os_free(workspace); workspace=0; return 1;
    }
    x86os_puts("BROWSER_FONT_READY\n");
    static browser_state_t state;
    make_temporary_path(&state);
    state.armed_link = UINT32_MAX;
    const char *target = initial_target(argc, argv, &state.probe);
    timing_enabled=state.probe;
    state.redraw = state.chrome_redraw = state.status_redraw = 1U;
    if (state.probe==1) x86os_puts("BROWSER_PROBE_SELECTOR_READY\n");
    (void)navigate(&state, &client, target);
    uint32_t probe_deadline = x86os_uptime_ms() + 30000U;
    while (!state.exit_requested) {
        uint32_t processed = 0U;
        for (; processed < BROWSER_EVENT_BATCH_LIMIT; ++processed) {
            reist_gui_surface_message_t message;
            status = reist_gui_surface_client_receive(&client, &message, 0U);
            if (status == -11) break;
            if (status != 0) { browser_runtime_failure(&state,"surface-receive",status); break; }
            if (message.type == REIST_GUI_SURFACE_CLOSE) {
                x86os_puts("BROWSER_SURFACE_CLOSE\n"); state.exit_requested = 1U; break;
            }
            if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
                int configured=reist_gui_surface_client_accept_configure(&client, &message);
                if (configured != 0) { browser_runtime_failure(&state,"surface-configure",configured); break; }
                state.scrollbar.state.captured = 0U; state.armed_link = UINT32_MAX;
                if (state.loaded) {
                    state.reflow_pending=1;
                }
                set_scroll(&state, &client, state.scroll_y);
                state.redraw = state.chrome_redraw = state.status_redraw = 1U;
            } else if (message.type == REIST_GUI_SURFACE_INPUT && message.input.type == REIST_GUI_SURFACE_INPUT_KEYBOARD && message.input.pressed) {
                if (state.probe == 3U) {
                    x86os_puts("BROWSER_INPUT_KEY ordinal="); x86os_print_number((int)++state.input_keys);
                    x86os_puts(" code="); x86os_print_number((int)message.input.key); x86os_puts("\n");
                }
                if (state.probe == 3U && state.probe_phase == 1U) {
                    if (message.input.key == 8U || message.input.key == 127U) ++state.input_backspace;
                    if (message.input.key == 0x104U) ++state.input_left;
                    if (message.input.key == 0x105U) ++state.input_right;
                }
                if(state.probe==6U && message.input.key=='x') {
                    ++model_probe.pending; model_probe.kind=1U;
                }
                handle_keyboard(&state, &client, message.input.key);
                if (state.probe==6U && message.input.key==BROWSER_KEY_ESCAPE) {
                    x86os_puts("BROWSER_MODEL_ESCAPE focused="); x86os_print_number((int)state.address_focused);
                    x86os_puts(" exit="); x86os_print_number((int)state.exit_requested); x86os_puts("\n");
                }
                if(state.probe==4) { x86os_puts("BROWSER_FORMS_KEY ordinal="); x86os_print_number((int)++state.input_keys);
                    x86os_puts(" code="); x86os_print_number((int)message.input.key); x86os_puts("\n"); }
            }
            else if (message.type == REIST_GUI_SURFACE_INPUT) handle_pointer(&state, &client, &message.input);
            if (state.exit_requested) break;
        }
        if (state.exit_requested) break;
        int rendered=render(&state, &client);
        if (rendered != 0) { browser_runtime_failure(&state,"render",rendered); status = -5; break; }
        if (state.probe) {
            int probe_status=state.probe==6 ? model_probe_step(&state,&client) : state.probe==5 ? public_probe_step(&state,&client) : state.probe==4 ? forms_probe_step(&state,&client) : state.probe==3 ? input_probe_step(&state) : state.probe==2 ? resource_probe_step(&state,&client) : state.loaded ? probe_step(&state,&client) : 0;
            if (probe_status || (int32_t)(x86os_uptime_ms() - probe_deadline) >= 0) {
                timing_dump();
                if (state.probe==6U) {
                    x86os_puts("BROWSER_MODEL_FAILURE result="); x86os_print_number(probe_status);
                    x86os_puts(" expired="); x86os_print_number((int32_t)(x86os_uptime_ms()-probe_deadline)>=0);
                    x86os_puts(" ordinal="); x86os_print_number((int)model_probe.ordinal);
                    x86os_puts(" pending="); x86os_print_number((int)model_probe.pending); x86os_puts("\n");
                }
                x86os_puts("BROWSER_PROBE_STATE phase="); x86os_print_number((int)state.probe_phase);
                x86os_puts(" loaded="); x86os_print_number((int)state.loaded);
                x86os_puts(" child="); x86os_print_number(state.child_pid);
                x86os_puts(" pending="); x86os_print_number((int)state.parse_pending);
                x86os_puts(" status="); x86os_puts(state.status); x86os_puts("\n");
                if (state.probe == 3U) {
                    x86os_puts("BROWSER_INPUT_STATE address="); x86os_puts(state.address);
                    x86os_puts(" cursor="); x86os_print_number((int)state.address_cursor);
                    x86os_puts(" backspace="); x86os_print_number((int)state.input_backspace);
                    x86os_puts(" left="); x86os_print_number((int)state.input_left);
                    x86os_puts(" right="); x86os_print_number((int)state.input_right); x86os_puts("\n");
                }
                x86os_puts("BROWSER_PROBE_FAIL interaction\n"); status = -5; break;
            }
        }
        uint32_t progress=state.load_progress;
        if (!state.exit_requested) service_loads(&state, &client);
        finish_load_turn(&state,processed,progress);
    }
    timing_dump();
    cancel_fetch(&state);
    uint32_t reap_deadline = state.child_reap_deadline;
    while (state.child_pid > 0 && (int32_t)(x86os_uptime_ms() - reap_deadline) < 0) {
        x86os_process_info_t info;
        if (child_info(&state, &info) != 0) break;
        if (info.state == X86OS_PROCESS_ZOMBIE) {
            int child_status;
            if (x86os_wait(state.child_pid, &child_status) == state.child_pid) state.child_pid = 0;
            break;
        }
        (void)x86os_sleep_ms(1U);
    }
    if (state.child_pid == 0) cleanup_fetch_channel(&state);
    int resource_cleanup=0;
    if(resource_probe_files&1U) resource_cleanup|=x86os_unlink(resource_probe_html);
    if(resource_probe_files&2U) resource_cleanup|=x86os_unlink(resource_probe_css);
    if(state.probe==2 && !resource_cleanup && !state.child_pid) x86os_puts("BROWSER_RESOURCES_CLEANUP_OK\n");
    if (state.css_endpoint) { (void)x86os_ipc_close(state.css_endpoint); state.css_endpoint=0; }
    int destroyed = reist_gui_surface_client_destroy(&client);
    int released = state.buffer_id ? x86os_display_surface_buffer_destroy(state.buffer_id, state.buffer_generation) : 0;
    (void)x86os_ipc_release(endpoint);
    x86os_free(workspace);
    workspace = 0;
    if (state.child_pid != 0 || destroyed != 0 || released != 0 || resource_cleanup != 0) {
        x86os_puts("BROWSER_CLEANUP_STATE child="); x86os_print_number(state.child_pid);
        x86os_puts(" surface="); x86os_print_number(destroyed);
        x86os_puts(" buffer="); x86os_print_number(released); x86os_puts("\n");
        x86os_puts("BROWSER_PROBE_FAIL cleanup\n"); return 1;
    }
    x86os_puts("BROWSER_CLOSE_OK\n");
    return browser_runtime_result(&state,status);
}
