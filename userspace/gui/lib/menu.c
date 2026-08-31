/**
 * @file userspace/gui/lib/menu.c
 * @brief Heap-free implementation of the public REIST GUI menu controller.
 *
 * Validation, pure geometry, bounded damage accumulation and event state
 * transitions are kept separate. Public entry points validate before
 * mutation; internal `*_unchecked` helpers are reached only after that
 * boundary or from already validated calculations.
 */
#include "reist/gui/menu.h"

#include <limits.h>
#include <stddef.h>

#define REIST_GUI_MENU_BORDER 2U

_Static_assert(offsetof(reist_gui_menu_layout_t, popup_direction) ==
                   REIST_GUI_MENU_LAYOUT_V1_SIZE,
               "version-1 menu layout prefix changed");

static uint32_t reserved_zero(const uint32_t *reserved, uint32_t count) {
    if (reserved == 0) return 0U;
    for (uint32_t index = 0U; index < count; ++index) {
        if (reserved[index] != 0U) return 0U;
    }
    return 1U;
}

static int bounded_label_length(const char *label, uint32_t *length) {
    if (label == 0 || length == 0) return REIST_GUI_MENU_EINVAL;
    for (uint32_t index = 0U; index < REIST_GUI_MENU_LABEL_LIMIT; ++index) {
        if (label[index] == '\0') {
            if (index == 0U) return REIST_GUI_MENU_EINVAL;
            *length = index;
            return 0;
        }
    }
    return REIST_GUI_MENU_EOVERFLOW;
}

static uint32_t point_in_rect(reist_gui_rect_t rect, int32_t x, int32_t y) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return x >= rect.x && y >= rect.y &&
           (int64_t)x < right && (int64_t)y < bottom;
}

static uint32_t item_row_height(const reist_gui_menu_layout_t *layout) {
    return layout->font_height + layout->item_padding_y * 2U;
}

static uint32_t layout_popup_direction(
    const reist_gui_menu_layout_t *layout) {
    return layout->struct_size >= sizeof(*layout)
        ? layout->popup_direction : REIST_GUI_MENU_POPUP_BELOW;
}

/* Validate every arithmetic and surface bound before geometry is computed. */
static int validate_layout(const reist_gui_menu_layout_t *layout) {
    if (layout == 0 || layout->version != REIST_GUI_MENU_API_VERSION ||
        layout->struct_size < REIST_GUI_MENU_LAYOUT_V1_SIZE ||
        !reserved_zero(layout->reserved, 4U) ||
        layout->surface_width == 0U || layout->surface_height == 0U ||
        layout->surface_width > INT32_MAX ||
        layout->surface_height > INT32_MAX ||
        layout->font_width == 0U || layout->font_width > 64U ||
        layout->font_height == 0U || layout->font_height > 64U ||
        layout->title_padding_x > 64U ||
        layout->item_padding_x > 64U || layout->item_padding_y > 32U ||
        layout->damage_margin > 16U ||
        layout->bar.x < 0 || layout->bar.y < 0 ||
        layout->bar.width == 0U || layout->bar.height == 0U ||
        layout->bar.height < layout->font_height)
        return REIST_GUI_MENU_EINVAL;
    if (layout_popup_direction(layout) > REIST_GUI_MENU_POPUP_ABOVE)
        return REIST_GUI_MENU_EINVAL;
    int64_t right = (int64_t)layout->bar.x + layout->bar.width;
    int64_t bottom = (int64_t)layout->bar.y + layout->bar.height;
    if (right > layout->surface_width || bottom > layout->surface_height)
        return REIST_GUI_MENU_EINVAL;
    if (layout->font_height > UINT32_MAX - layout->item_padding_y * 2U)
        return REIST_GUI_MENU_EOVERFLOW;
    return 0;
}

/* Traverse the immutable object graph under the public fixed capacities. */
static int validate_model(const reist_gui_menu_model_t *model,
                          const reist_gui_menu_layout_t *layout) {
    if (model == 0 || model->version != REIST_GUI_MENU_API_VERSION ||
        model->struct_size < sizeof(*model) ||
        !reserved_zero(model->reserved, 4U) || model->menus == 0 ||
        model->menu_count == 0U ||
        model->menu_count > REIST_GUI_MENU_MAX_MENUS)
        return REIST_GUI_MENU_EINVAL;

    uint64_t title_total = 0U;
    uint32_t row_height = item_row_height(layout);
    uint64_t available_height =
        layout_popup_direction(layout) == REIST_GUI_MENU_POPUP_ABOVE
            ? (uint64_t)(uint32_t)layout->bar.y
            : layout->surface_height -
                ((uint64_t)(uint32_t)layout->bar.y + layout->bar.height);
    for (uint32_t menu_index = 0U;
         menu_index < model->menu_count; ++menu_index) {
        const reist_gui_menu_t *menu = &model->menus[menu_index];
        uint32_t label_length = 0U;
        if (bounded_label_length(menu->label, &label_length) != 0 ||
            menu->items == 0 || menu->item_count == 0U ||
            menu->item_count > REIST_GUI_MENU_MAX_ITEMS ||
            menu->flags != 0U || menu->reserved != 0U)
            return REIST_GUI_MENU_EINVAL;
        title_total += (uint64_t)label_length * layout->font_width +
                       layout->title_padding_x * 2U;
        uint64_t popup_width =
            (uint64_t)label_length * layout->font_width +
            layout->title_padding_x * 2U;
        uint64_t popup_height = REIST_GUI_MENU_BORDER * 2U +
                                (uint64_t)menu->item_count * row_height;
        if (popup_height > available_height)
            return REIST_GUI_MENU_EOVERFLOW;
        for (uint32_t item_index = 0U;
             item_index < menu->item_count; ++item_index) {
            const reist_gui_menu_item_t *item = &menu->items[item_index];
            if (bounded_label_length(item->label, &label_length) != 0 ||
                (item->flags & ~REIST_GUI_MENU_ITEM_DISABLED) != 0U ||
                item->reserved != 0U)
                return REIST_GUI_MENU_EINVAL;
            uint64_t item_width =
                (uint64_t)label_length * layout->font_width +
                layout->item_padding_x * 2U + layout->font_width * 2U;
            item_width += REIST_GUI_MENU_BORDER * 2U;
            if (item_width > popup_width) popup_width = item_width;
        }
        if (popup_width > layout->surface_width)
            return REIST_GUI_MENU_EOVERFLOW;
    }
    if (title_total > layout->bar.width)
        return REIST_GUI_MENU_EOVERFLOW;
    return 0;
}

/* A capture is either completely valid for its kind or rejected fail-closed. */
static int validate_state(const reist_gui_menu_model_t *model,
                          const reist_gui_menu_state_t *state) {
    if (state == 0 || state->version != REIST_GUI_MENU_API_VERSION ||
        state->struct_size < sizeof(*state) ||
        !reserved_zero(state->reserved, 4U) ||
        state->capture_kind > REIST_GUI_MENU_CAPTURE_DISMISS)
        return REIST_GUI_MENU_EINVAL;
    if (state->open_menu != REIST_GUI_MENU_NO_INDEX &&
        state->open_menu >= model->menu_count)
        return REIST_GUI_MENU_EINVAL;
    if (state->hot_item != REIST_GUI_MENU_NO_INDEX &&
        (state->open_menu == REIST_GUI_MENU_NO_INDEX ||
         state->hot_item >= model->menus[state->open_menu].item_count))
        return REIST_GUI_MENU_EINVAL;
    if (state->capture_kind == REIST_GUI_MENU_CAPTURE_NONE)
        return state->capture_menu == REIST_GUI_MENU_NO_INDEX &&
               state->capture_item == REIST_GUI_MENU_NO_INDEX
            ? REIST_GUI_MENU_OK : REIST_GUI_MENU_EINVAL;
    if (state->capture_kind == REIST_GUI_MENU_CAPTURE_DISMISS)
        return state->capture_menu == REIST_GUI_MENU_NO_INDEX &&
               state->capture_item == REIST_GUI_MENU_NO_INDEX
            ? REIST_GUI_MENU_OK : REIST_GUI_MENU_EINVAL;
    if (state->capture_menu >= model->menu_count)
        return REIST_GUI_MENU_EINVAL;
    if (state->capture_kind == REIST_GUI_MENU_CAPTURE_TITLE &&
        state->capture_item != REIST_GUI_MENU_NO_INDEX)
        return REIST_GUI_MENU_EINVAL;
    if (state->capture_kind == REIST_GUI_MENU_CAPTURE_ITEM &&
        (state->open_menu != state->capture_menu ||
         state->capture_item >=
            model->menus[state->capture_menu].item_count))
        return REIST_GUI_MENU_EINVAL;
    return 0;
}

void reist_gui_menu_state_initialize(reist_gui_menu_state_t *state) {
    if (state == 0) return;
    *state = (reist_gui_menu_state_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(*state),
        .open_menu = REIST_GUI_MENU_NO_INDEX,
        .hot_item = REIST_GUI_MENU_NO_INDEX,
        .capture_kind = REIST_GUI_MENU_CAPTURE_NONE,
        .capture_menu = REIST_GUI_MENU_NO_INDEX,
        .capture_item = REIST_GUI_MENU_NO_INDEX,
    };
}

void reist_gui_menu_event_initialize(reist_gui_menu_event_t *event) {
    if (event == 0) return;
    *event = (reist_gui_menu_event_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(*event),
    };
}

void reist_gui_menu_result_initialize(reist_gui_menu_result_t *result) {
    if (result == 0) return;
    *result = (reist_gui_menu_result_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = sizeof(*result),
        .target = REIST_GUI_MENU_NO_INDEX,
    };
}

int reist_gui_menu_validate(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            const reist_gui_menu_state_t *state) {
    int status = validate_layout(layout);
    if (status != 0) return status;
    status = validate_model(model, layout);
    if (status != 0) return status;
    return validate_state(model, state);
}

static int title_rect_unchecked(const reist_gui_menu_model_t *model,
                                const reist_gui_menu_layout_t *layout,
                                uint32_t menu_index,
                                reist_gui_rect_t *rect) {
    if (menu_index >= model->menu_count || rect == 0)
        return REIST_GUI_MENU_EINVAL;
    uint64_t x = (uint32_t)layout->bar.x;
    for (uint32_t index = 0U; index <= menu_index; ++index) {
        uint32_t length = 0U;
        int status = bounded_label_length(model->menus[index].label, &length);
        if (status != 0) return status;
        uint64_t width = (uint64_t)length * layout->font_width +
                         layout->title_padding_x * 2U;
        if (index == menu_index) {
            *rect = (reist_gui_rect_t){
                (int32_t)x, layout->bar.y, (uint32_t)width,
                layout->bar.height
            };
            return 0;
        }
        x += width;
    }
    return REIST_GUI_MENU_EINVAL;
}

int reist_gui_menu_title_rect(const reist_gui_menu_model_t *model,
                              const reist_gui_menu_layout_t *layout,
                              uint32_t menu_index,
                              reist_gui_rect_t *rect) {
    reist_gui_menu_state_t state;
    reist_gui_menu_state_initialize(&state);
    int status = reist_gui_menu_validate(model, layout, &state);
    if (status != 0) return status;
    return title_rect_unchecked(model, layout, menu_index, rect);
}

static int popup_rect_unchecked(const reist_gui_menu_model_t *model,
                                const reist_gui_menu_layout_t *layout,
                                uint32_t menu_index,
                                reist_gui_rect_t *rect) {
    if (menu_index >= model->menu_count || rect == 0)
        return REIST_GUI_MENU_EINVAL;
    reist_gui_rect_t title;
    int status = title_rect_unchecked(model, layout, menu_index, &title);
    if (status != 0) return status;
    uint64_t width = title.width;
    const reist_gui_menu_t *menu = &model->menus[menu_index];
    for (uint32_t index = 0U; index < menu->item_count; ++index) {
        uint32_t length = 0U;
        status = bounded_label_length(menu->items[index].label, &length);
        if (status != 0) return status;
        uint64_t item_width = (uint64_t)length * layout->font_width +
            layout->item_padding_x * 2U + layout->font_width * 2U +
            REIST_GUI_MENU_BORDER * 2U;
        if (item_width > width) width = item_width;
    }
    if (width > layout->surface_width)
        return REIST_GUI_MENU_EOVERFLOW;
    int64_t x = title.x;
    if (x + (int64_t)width > layout->surface_width)
        x = (int64_t)layout->surface_width - width;
    uint64_t height = REIST_GUI_MENU_BORDER * 2U +
        (uint64_t)menu->item_count * item_row_height(layout);
    int32_t y = layout_popup_direction(layout) ==
            REIST_GUI_MENU_POPUP_ABOVE
        ? layout->bar.y - (int32_t)height
        : layout->bar.y + (int32_t)layout->bar.height;
    *rect = (reist_gui_rect_t){
        (int32_t)x, y, (uint32_t)width, (uint32_t)height,
    };
    return 0;
}

int reist_gui_menu_popup_rect(const reist_gui_menu_model_t *model,
                              const reist_gui_menu_layout_t *layout,
                              uint32_t menu_index,
                              reist_gui_rect_t *rect) {
    reist_gui_menu_state_t state;
    reist_gui_menu_state_initialize(&state);
    int status = reist_gui_menu_validate(model, layout, &state);
    if (status != 0) return status;
    return popup_rect_unchecked(model, layout, menu_index, rect);
}

static int item_rect_unchecked(const reist_gui_menu_model_t *model,
                               const reist_gui_menu_layout_t *layout,
                               uint32_t menu_index, uint32_t item_index,
                               reist_gui_rect_t *rect) {
    if (menu_index >= model->menu_count ||
        item_index >= model->menus[menu_index].item_count || rect == 0)
        return REIST_GUI_MENU_EINVAL;
    reist_gui_rect_t popup;
    int status = popup_rect_unchecked(model, layout, menu_index, &popup);
    if (status != 0) return status;
    uint32_t row_height = item_row_height(layout);
    *rect = (reist_gui_rect_t){
        popup.x + (int32_t)REIST_GUI_MENU_BORDER,
        popup.y + (int32_t)REIST_GUI_MENU_BORDER +
            (int32_t)(item_index * row_height),
        popup.width - REIST_GUI_MENU_BORDER * 2U,
        row_height,
    };
    return 0;
}

int reist_gui_menu_item_rect(const reist_gui_menu_model_t *model,
                             const reist_gui_menu_layout_t *layout,
                             uint32_t menu_index, uint32_t item_index,
                             reist_gui_rect_t *rect) {
    reist_gui_menu_state_t state;
    reist_gui_menu_state_initialize(&state);
    int status = reist_gui_menu_validate(model, layout, &state);
    if (status != 0) return status;
    return item_rect_unchecked(
        model, layout, menu_index, item_index, rect);
}

static void reset_result(reist_gui_menu_result_t *result) {
    uint32_t size = result->struct_size;
    *result = (reist_gui_menu_result_t){
        .version = REIST_GUI_MENU_API_VERSION,
        .struct_size = size,
        .target = REIST_GUI_MENU_NO_INDEX,
    };
}

/* Include shadows and bevels while retaining local-surface clipping. */
static reist_gui_rect_t expanded_damage(
    const reist_gui_menu_layout_t *layout, reist_gui_rect_t rect) {
    int64_t left = (int64_t)rect.x - layout->damage_margin;
    int64_t top = (int64_t)rect.y - layout->damage_margin;
    int64_t right = (int64_t)rect.x + rect.width + layout->damage_margin;
    int64_t bottom = (int64_t)rect.y + rect.height + layout->damage_margin;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > layout->surface_width) right = layout->surface_width;
    if (bottom > layout->surface_height) bottom = layout->surface_height;
    return (reist_gui_rect_t){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)
    };
}

static void append_damage(const reist_gui_menu_layout_t *layout,
                          reist_gui_menu_result_t *result,
                          reist_gui_rect_t rect) {
    if (rect.width == 0U || rect.height == 0U || result->full_redraw) return;
    if (result->damage_count >= REIST_GUI_MENU_DAMAGE_CAPACITY) {
        /* Fixed-capacity overflow degrades to one explicit full redraw. */
        result->damage[0] = (reist_gui_rect_t){
            0, 0, layout->surface_width, layout->surface_height
        };
        result->damage_count = 1U;
        result->full_redraw = 1U;
        return;
    }
    result->damage[result->damage_count++] = rect;
}

static void add_damage(const reist_gui_menu_layout_t *layout,
                       reist_gui_menu_result_t *result,
                       reist_gui_rect_t rect) {
    append_damage(layout, result, expanded_damage(layout, rect));
}

static void add_menu_damage(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            uint32_t menu_index,
                            reist_gui_menu_result_t *result) {
    reist_gui_rect_t rect;
    if (title_rect_unchecked(model, layout, menu_index, &rect) == 0)
        add_damage(layout, result, rect);
    if (popup_rect_unchecked(model, layout, menu_index, &rect) == 0)
        add_damage(layout, result, rect);
}

static void add_item_damage(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            uint32_t menu_index, uint32_t item_index,
                            reist_gui_menu_result_t *result) {
    if (item_index == REIST_GUI_MENU_NO_INDEX) return;
    reist_gui_rect_t rect;
    if (item_rect_unchecked(
            model, layout, menu_index, item_index, &rect) == 0)
        /* Hot and pressed pixels are fully contained by the item row.  An
         * outer margin would expose lower scene layers to an otherwise local
         * hover update and defeat opaque-overlay composition. */
        append_damage(layout, result, rect);
}

/* Invalidate both old and new ownership before publishing the state change. */
static void set_open_menu(const reist_gui_menu_model_t *model,
                          const reist_gui_menu_layout_t *layout,
                          reist_gui_menu_state_t *state,
                          uint32_t menu_index,
                          reist_gui_menu_result_t *result) {
    if (state->open_menu == menu_index) return;
    if (state->open_menu != REIST_GUI_MENU_NO_INDEX)
        add_menu_damage(model, layout, state->open_menu, result);
    state->open_menu = menu_index;
    state->hot_item = REIST_GUI_MENU_NO_INDEX;
    if (menu_index != REIST_GUI_MENU_NO_INDEX)
        add_menu_damage(model, layout, menu_index, result);
}

static void set_hot_item(const reist_gui_menu_model_t *model,
                         const reist_gui_menu_layout_t *layout,
                         reist_gui_menu_state_t *state,
                         uint32_t item_index,
                         reist_gui_menu_result_t *result) {
    if (state->hot_item == item_index) return;
    if (state->open_menu != REIST_GUI_MENU_NO_INDEX) {
        add_item_damage(
            model, layout, state->open_menu, state->hot_item, result);
        add_item_damage(
            model, layout, state->open_menu, item_index, result);
    }
    state->hot_item = item_index;
}

static uint32_t title_at(const reist_gui_menu_model_t *model,
                         const reist_gui_menu_layout_t *layout,
                         int32_t x, int32_t y) {
    for (uint32_t index = 0U; index < model->menu_count; ++index) {
        reist_gui_rect_t rect;
        if (title_rect_unchecked(model, layout, index, &rect) == 0 &&
            point_in_rect(rect, x, y)) return index;
    }
    return REIST_GUI_MENU_NO_INDEX;
}

static uint32_t item_at(const reist_gui_menu_model_t *model,
                        const reist_gui_menu_layout_t *layout,
                        uint32_t menu_index, int32_t x, int32_t y) {
    if (menu_index == REIST_GUI_MENU_NO_INDEX) return REIST_GUI_MENU_NO_INDEX;
    for (uint32_t index = 0U;
         index < model->menus[menu_index].item_count; ++index) {
        reist_gui_rect_t rect;
        if (item_rect_unchecked(
                model, layout, menu_index, index, &rect) == 0 &&
            point_in_rect(rect, x, y)) return index;
    }
    return REIST_GUI_MENU_NO_INDEX;
}

static uint32_t next_enabled_item(const reist_gui_menu_t *menu,
                                  uint32_t current, int32_t direction) {
    if (menu == 0 || menu->item_count == 0U)
        return REIST_GUI_MENU_NO_INDEX;
    uint32_t next = current;
    if (next == REIST_GUI_MENU_NO_INDEX)
        next = direction < 0 ? 0U : menu->item_count - 1U;
    for (uint32_t attempt = 0U; attempt < menu->item_count; ++attempt) {
        next = direction < 0
            ? (next + menu->item_count - 1U) % menu->item_count
            : (next + 1U) % menu->item_count;
        if ((menu->items[next].flags & REIST_GUI_MENU_ITEM_DISABLED) == 0U)
            return next;
    }
    return REIST_GUI_MENU_NO_INDEX;
}

static void activate_item(const reist_gui_menu_model_t *model,
                          const reist_gui_menu_layout_t *layout,
                          reist_gui_menu_state_t *state,
                          uint32_t menu_index, uint32_t item_index,
                          reist_gui_menu_result_t *result) {
    const reist_gui_menu_item_t *item =
        &model->menus[menu_index].items[item_index];
    if ((item->flags & REIST_GUI_MENU_ITEM_DISABLED) != 0U) return;
    result->activated = 1U;
    result->action = item->action;
    result->target = item->target;
    set_open_menu(
        model, layout, state, REIST_GUI_MENU_NO_INDEX, result);
}

static void dispatch_motion(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            reist_gui_menu_state_t *state,
                            const reist_gui_menu_event_t *event,
                            reist_gui_menu_result_t *result) {
    if (state->open_menu == REIST_GUI_MENU_NO_INDEX &&
        state->capture_kind == REIST_GUI_MENU_CAPTURE_NONE) return;
    result->consumed = 1U;
    if (state->open_menu == REIST_GUI_MENU_NO_INDEX) return;
    uint32_t title = title_at(model, layout, event->x, event->y);
    if (title != REIST_GUI_MENU_NO_INDEX && title != state->open_menu) {
        set_open_menu(model, layout, state, title, result);
        if (state->capture_kind == REIST_GUI_MENU_CAPTURE_TITLE)
            state->capture_menu = title;
        return;
    }
    uint32_t item = item_at(
        model, layout, state->open_menu, event->x, event->y);
    if (item != REIST_GUI_MENU_NO_INDEX &&
        (model->menus[state->open_menu].items[item].flags &
         REIST_GUI_MENU_ITEM_DISABLED) != 0U)
        item = REIST_GUI_MENU_NO_INDEX;
    set_hot_item(model, layout, state, item, result);
}

static void dispatch_press(const reist_gui_menu_model_t *model,
                           const reist_gui_menu_layout_t *layout,
                           reist_gui_menu_state_t *state,
                           const reist_gui_menu_event_t *event,
                           reist_gui_menu_result_t *result) {
    uint32_t title = title_at(model, layout, event->x, event->y);
    if (title != REIST_GUI_MENU_NO_INDEX) {
        result->consumed = 1U;
        if (state->open_menu == title)
            set_open_menu(
                model, layout, state, REIST_GUI_MENU_NO_INDEX, result);
        else
            set_open_menu(model, layout, state, title, result);
        /* Capture prevents the matching release reaching an underlying UI. */
        state->capture_kind = REIST_GUI_MENU_CAPTURE_TITLE;
        state->capture_menu = title;
        state->capture_item = REIST_GUI_MENU_NO_INDEX;
        return;
    }
    if (state->open_menu == REIST_GUI_MENU_NO_INDEX) return;
    result->consumed = 1U;
    uint32_t item = item_at(
        model, layout, state->open_menu, event->x, event->y);
    if (item != REIST_GUI_MENU_NO_INDEX) {
        uint32_t previous_hot = state->hot_item;
        state->capture_kind = REIST_GUI_MENU_CAPTURE_ITEM;
        state->capture_menu = state->open_menu;
        state->capture_item = item;
        uint32_t enabled_item =
            (model->menus[state->open_menu].items[item].flags &
             REIST_GUI_MENU_ITEM_DISABLED) == 0U
                ? item : REIST_GUI_MENU_NO_INDEX;
        set_hot_item(model, layout, state, enabled_item, result);
        if (previous_hot == enabled_item)
            add_item_damage(
                model, layout, state->open_menu, enabled_item, result);
        return;
    }
    set_open_menu(
        model, layout, state, REIST_GUI_MENU_NO_INDEX, result);
    state->capture_kind = REIST_GUI_MENU_CAPTURE_DISMISS;
    state->capture_menu = REIST_GUI_MENU_NO_INDEX;
    state->capture_item = REIST_GUI_MENU_NO_INDEX;
}

static void dispatch_release(const reist_gui_menu_model_t *model,
                             const reist_gui_menu_layout_t *layout,
                             reist_gui_menu_state_t *state,
                             const reist_gui_menu_event_t *event,
                             reist_gui_menu_result_t *result) {
    /* Snapshot capture before activation closes and resets the open popup. */
    uint32_t capture = state->capture_kind;
    uint32_t menu = state->capture_menu;
    uint32_t item = state->capture_item;
    if (capture == REIST_GUI_MENU_CAPTURE_NONE &&
        state->open_menu == REIST_GUI_MENU_NO_INDEX) return;
    result->consumed = 1U;
    if (capture == REIST_GUI_MENU_CAPTURE_ITEM &&
        state->open_menu == menu &&
        item_at(model, layout, menu, event->x, event->y) == item) {
        activate_item(model, layout, state, menu, item, result);
    } else if (capture == REIST_GUI_MENU_CAPTURE_ITEM &&
               state->open_menu != REIST_GUI_MENU_NO_INDEX) {
        add_item_damage(
            model, layout, state->open_menu, item, result);
    }
    state->capture_kind = REIST_GUI_MENU_CAPTURE_NONE;
    state->capture_menu = REIST_GUI_MENU_NO_INDEX;
    state->capture_item = REIST_GUI_MENU_NO_INDEX;
}

static void dispatch_key(const reist_gui_menu_model_t *model,
                         const reist_gui_menu_layout_t *layout,
                         reist_gui_menu_state_t *state,
                         const reist_gui_menu_event_t *event,
                         reist_gui_menu_result_t *result) {
    if (state->open_menu == REIST_GUI_MENU_NO_INDEX) return;
    result->consumed = 1U;
    if (event->key == REIST_GUI_MENU_KEY_ESCAPE) {
        set_open_menu(
            model, layout, state, REIST_GUI_MENU_NO_INDEX, result);
        return;
    }
    if (event->key == REIST_GUI_MENU_KEY_LEFT ||
        event->key == REIST_GUI_MENU_KEY_RIGHT) {
        uint32_t next = event->key == REIST_GUI_MENU_KEY_LEFT
            ? (state->open_menu + model->menu_count - 1U) % model->menu_count
            : (state->open_menu + 1U) % model->menu_count;
        set_open_menu(model, layout, state, next, result);
        state->hot_item = next_enabled_item(
            &model->menus[next], REIST_GUI_MENU_NO_INDEX, 1);
        return;
    }
    if (event->key == REIST_GUI_MENU_KEY_UP ||
        event->key == REIST_GUI_MENU_KEY_DOWN) {
        set_hot_item(
            model, layout, state,
            next_enabled_item(
                &model->menus[state->open_menu], state->hot_item,
                event->key == REIST_GUI_MENU_KEY_UP ? -1 : 1), result);
        return;
    }
    if (event->key == REIST_GUI_MENU_KEY_ENTER) {
        uint32_t item = state->hot_item;
        if (item == REIST_GUI_MENU_NO_INDEX) {
            item = next_enabled_item(
                &model->menus[state->open_menu],
                REIST_GUI_MENU_NO_INDEX, 1);
        }
        if (item != REIST_GUI_MENU_NO_INDEX)
            activate_item(
                model, layout, state, state->open_menu, item, result);
    }
}

int reist_gui_menu_dispatch(const reist_gui_menu_model_t *model,
                            const reist_gui_menu_layout_t *layout,
                            reist_gui_menu_state_t *state,
                            const reist_gui_menu_event_t *event,
                            reist_gui_menu_result_t *result) {
    if (event == 0 || result == 0 ||
        event->version != REIST_GUI_MENU_API_VERSION ||
        event->struct_size < sizeof(*event) ||
        !reserved_zero(event->reserved, 4U) ||
        result->version != REIST_GUI_MENU_API_VERSION ||
        result->struct_size < sizeof(*result) ||
        !reserved_zero(result->reserved, 4U))
        return REIST_GUI_MENU_EINVAL;
    int status = reist_gui_menu_validate(model, layout, state);
    if (status != 0) return status;
    reset_result(result);

    if (event->type == REIST_GUI_MENU_EVENT_POINTER_MOTION) {
        if (event->button != 0U || event->pressed != 0U ||
            event->key != 0U) return REIST_GUI_MENU_EINVAL;
        dispatch_motion(model, layout, state, event, result);
    } else if (event->type == REIST_GUI_MENU_EVENT_POINTER_BUTTON) {
        if (event->button != REIST_GUI_MENU_BUTTON_LEFT ||
            event->pressed > 1U || event->key != 0U)
            return REIST_GUI_MENU_EINVAL;
        if (event->pressed)
            dispatch_press(model, layout, state, event, result);
        else
            dispatch_release(model, layout, state, event, result);
    } else if (event->type == REIST_GUI_MENU_EVENT_KEYBOARD) {
        if (event->x != 0 || event->y != 0 || event->button != 0U ||
            event->pressed != 0U ||
            event->key < REIST_GUI_MENU_KEY_LEFT ||
            event->key > REIST_GUI_MENU_KEY_ESCAPE)
            return REIST_GUI_MENU_EINVAL;
        dispatch_key(model, layout, state, event, result);
    } else {
        return REIST_GUI_MENU_EINVAL;
    }
    return validate_state(model, state);
}
