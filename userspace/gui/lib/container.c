#include "reist/gui/container.h"

#include <stddef.h>

static void clear_bytes(void *value, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static uint32_t words_zero(const uint32_t *words, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (words[index] != 0U) return 0U;
    return 1U;
}

static uint32_t name_valid(const char *name) {
    if (name == 0) return 0U;
    for (uint32_t index = 0U; index < REIST_GUI_TREE_NAME_LIMIT; ++index)
        if (name[index] == '\0') return index != 0U;
    return 0U;
}

static uint32_t rect_valid(reist_gui_rect_t rect) {
    if (rect.width == 0U || rect.height == 0U) return 0U;
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return right >= INT32_MIN && right <= INT32_MAX &&
           bottom >= INT32_MIN && bottom <= INT32_MAX;
}

static uint32_t flags_valid(uint32_t flags) {
    return (flags & ~(REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED)) == 0U;
}

static uint32_t prior_index(const reist_gui_tree_model_t *model,
                            uint32_t id, uint32_t before) {
    for (uint32_t index = 0U; index < before; ++index)
        if (model->nodes[index].id == id) return index;
    return REIST_GUI_TREE_NO_INDEX;
}

int reist_gui_tree_validate(const reist_gui_tree_model_t *model) {
    if (model == 0 || model->version != REIST_GUI_TREE_API_VERSION ||
        model->struct_size != sizeof(*model) || model->nodes == 0 ||
        model->node_count == 0U ||
        model->node_count > REIST_GUI_TREE_CAPACITY ||
        !words_zero(model->reserved, 4U)) return REIST_GUI_TREE_EINVAL;
    for (uint32_t index = 0U; index < model->node_count; ++index) {
        const reist_gui_node_t *node = &model->nodes[index];
        if (node->id == REIST_GUI_TREE_NO_ID ||
            node->role < REIST_GUI_NODE_CONTAINER ||
            node->role > REIST_GUI_NODE_CONTROL || !name_valid(node->name) ||
            !rect_valid(node->bounds) || !flags_valid(node->flags) ||
            !words_zero(node->reserved, 4U)) return REIST_GUI_TREE_EINVAL;
        if ((node->role == REIST_GUI_NODE_CONTAINER && node->target_id != 0U) ||
            (node->role == REIST_GUI_NODE_CONTROL && node->target_id == 0U))
            return REIST_GUI_TREE_EINVAL;
        if (prior_index(model, node->id, index) != REIST_GUI_TREE_NO_INDEX)
            return REIST_GUI_TREE_EINVAL;
        if (node->role == REIST_GUI_NODE_CONTROL) {
            for (uint32_t other = 0U; other < index; ++other)
                if (model->nodes[other].role == REIST_GUI_NODE_CONTROL &&
                    model->nodes[other].target_id == node->target_id)
                    return REIST_GUI_TREE_EINVAL;
        }
        if (index == 0U) {
            if (node->parent_id != REIST_GUI_TREE_NO_ID ||
                node->role != REIST_GUI_NODE_CONTAINER ||
                node->bounds.x != 0 || node->bounds.y != 0)
                return REIST_GUI_TREE_EINVAL;
        } else {
            uint32_t parent = prior_index(model, node->parent_id, index);
            if (parent == REIST_GUI_TREE_NO_INDEX ||
                model->nodes[parent].role != REIST_GUI_NODE_CONTAINER)
                return REIST_GUI_TREE_EINVAL;
        }
    }
    return REIST_GUI_TREE_OK;
}

int reist_gui_tree_index(const reist_gui_tree_model_t *model,
                         uint32_t node_id, uint32_t *index_out) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    if (node_id == REIST_GUI_TREE_NO_ID || index_out == 0)
        return REIST_GUI_TREE_EINVAL;
    for (uint32_t index = 0U; index < model->node_count; ++index) {
        if (model->nodes[index].id == node_id) {
            *index_out = index;
            return REIST_GUI_TREE_OK;
        }
    }
    return REIST_GUI_TREE_EINVAL;
}

static reist_gui_rect_t intersect(reist_gui_rect_t left,
                                  reist_gui_rect_t right) {
    int64_t left_right = (int64_t)left.x + left.width;
    int64_t left_bottom = (int64_t)left.y + left.height;
    int64_t right_right = (int64_t)right.x + right.width;
    int64_t right_bottom = (int64_t)right.y + right.height;
    int32_t x = left.x > right.x ? left.x : right.x;
    int32_t y = left.y > right.y ? left.y : right.y;
    int64_t end_x = left_right < right_right ? left_right : right_right;
    int64_t end_y = left_bottom < right_bottom ? left_bottom : right_bottom;
    return (reist_gui_rect_t){x, y,
        end_x > x ? (uint32_t)(end_x - x) : 0U,
        end_y > y ? (uint32_t)(end_y - y) : 0U};
}

static int geometry_index(const reist_gui_tree_model_t *model,
                          uint32_t index,
                          reist_gui_node_geometry_t *geometry_out) {
    uint32_t ancestry[REIST_GUI_TREE_CAPACITY];
    uint32_t depth = 0U;
    uint32_t current = index;
    while (depth < model->node_count) {
        ancestry[depth++] = current;
        if (current == 0U) break;
        current = prior_index(model, model->nodes[current].parent_id, current);
        if (current == REIST_GUI_TREE_NO_INDEX) return REIST_GUI_TREE_EINVAL;
    }
    if (depth == model->node_count && ancestry[depth - 1U] != 0U)
        return REIST_GUI_TREE_ECAPACITY;
    reist_gui_rect_t absolute = model->nodes[0].bounds;
    reist_gui_rect_t clip = absolute;
    for (uint32_t position = depth - 1U; position != 0U; --position) {
        const reist_gui_node_t *node = &model->nodes[ancestry[position - 1U]];
        int64_t x = (int64_t)absolute.x + node->bounds.x;
        int64_t y = (int64_t)absolute.y + node->bounds.y;
        if (x < INT32_MIN || x > INT32_MAX ||
            y < INT32_MIN || y > INT32_MAX) return REIST_GUI_TREE_EINVAL;
        absolute = (reist_gui_rect_t){
            (int32_t)x, (int32_t)y, node->bounds.width, node->bounds.height};
        clip = intersect(clip, absolute);
    }
    geometry_out->bounds = absolute;
    geometry_out->clip = clip;
    return REIST_GUI_TREE_OK;
}

int reist_gui_tree_geometry(const reist_gui_tree_model_t *model,
                            uint32_t node_id,
                            reist_gui_node_geometry_t *geometry_out) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    uint32_t index;
    if (geometry_out == 0 ||
        reist_gui_tree_index(model, node_id, &index) != REIST_GUI_TREE_OK)
        return REIST_GUI_TREE_EINVAL;
    return geometry_index(model, index, geometry_out);
}

int reist_gui_tree_first_child(const reist_gui_tree_model_t *model,
                               uint32_t container_id,
                               uint32_t *child_index_out) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    uint32_t container;
    if (child_index_out == 0 || reist_gui_tree_index(
            model, container_id, &container) != REIST_GUI_TREE_OK ||
        model->nodes[container].role != REIST_GUI_NODE_CONTAINER)
        return REIST_GUI_TREE_EINVAL;
    for (uint32_t index = container + 1U; index < model->node_count; ++index) {
        if (model->nodes[index].parent_id == container_id) {
            *child_index_out = index;
            return REIST_GUI_TREE_OK;
        }
    }
    *child_index_out = REIST_GUI_TREE_NO_INDEX;
    return REIST_GUI_TREE_OK;
}

int reist_gui_tree_next_sibling(const reist_gui_tree_model_t *model,
                                uint32_t child_index,
                                uint32_t *sibling_index_out) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    if (sibling_index_out == 0 || child_index == 0U ||
        child_index >= model->node_count) return REIST_GUI_TREE_EINVAL;
    uint32_t parent = model->nodes[child_index].parent_id;
    for (uint32_t index = child_index + 1U;
         index < model->node_count; ++index) {
        if (model->nodes[index].parent_id == parent) {
            *sibling_index_out = index;
            return REIST_GUI_TREE_OK;
        }
    }
    *sibling_index_out = REIST_GUI_TREE_NO_INDEX;
    return REIST_GUI_TREE_OK;
}

static uint32_t point_inside(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return rect.width != 0U && rect.height != 0U && x >= rect.x && y >= rect.y &&
           (uint64_t)(uint32_t)(x - rect.x) < rect.width &&
           (uint64_t)(uint32_t)(y - rect.y) < rect.height;
}

static uint32_t ancestry_interactive(const reist_gui_tree_model_t *model,
                                     uint32_t index) {
    uint32_t current = index;
    for (uint32_t depth = 0U; depth < model->node_count; ++depth) {
        const reist_gui_node_t *node = &model->nodes[current];
        if ((node->flags & (REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED)) !=
            (REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED)) return 0U;
        if (current == 0U) return 1U;
        current = prior_index(model, node->parent_id, current);
        if (current == REIST_GUI_TREE_NO_INDEX) return 0U;
    }
    return 0U;
}

int reist_gui_tree_hit_test(const reist_gui_tree_model_t *model,
                            int32_t x, int32_t y,
                            uint32_t *node_id_out,
                            uint32_t *target_id_out) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    if (node_id_out == 0 || target_id_out == 0)
        return REIST_GUI_TREE_EINVAL;
    for (uint32_t step = 0U; step < model->node_count; ++step) {
        uint32_t index = model->node_count - 1U - step;
        const reist_gui_node_t *node = &model->nodes[index];
        if (node->role != REIST_GUI_NODE_CONTROL ||
            !ancestry_interactive(model, index)) continue;
        reist_gui_node_geometry_t geometry;
        if (geometry_index(model, index, &geometry) == REIST_GUI_TREE_OK &&
            point_inside(geometry.clip, x, y)) {
            *node_id_out = node->id;
            *target_id_out = node->target_id;
            return REIST_GUI_TREE_OK;
        }
    }
    *node_id_out = REIST_GUI_TREE_NO_ID;
    *target_id_out = REIST_GUI_TREE_NO_ID;
    return REIST_GUI_TREE_OK;
}

void reist_gui_event_route_initialize(reist_gui_event_route_t *route) {
    if (route == 0) return;
    clear_bytes(route, sizeof(*route));
    route->version = REIST_GUI_TREE_API_VERSION;
    route->struct_size = sizeof(*route);
}

int reist_gui_tree_route(const reist_gui_tree_model_t *model,
                         int32_t x, int32_t y,
                         reist_gui_event_route_t *route) {
    int status = reist_gui_tree_validate(model);
    if (status != REIST_GUI_TREE_OK) return status;
    if (route == 0 || route->version != REIST_GUI_TREE_API_VERSION ||
        route->struct_size != sizeof(*route) ||
        !words_zero(route->reserved, 4U) ||
        route->depth > REIST_GUI_TREE_CAPACITY)
        return REIST_GUI_TREE_EINVAL;
    uint32_t node_id;
    uint32_t target_id;
    status = reist_gui_tree_hit_test(
        model, x, y, &node_id, &target_id);
    if (status != REIST_GUI_TREE_OK) return status;
    route->depth = 0U;
    route->target_node_id = node_id;
    route->target_id = target_id;
    if (node_id == REIST_GUI_TREE_NO_ID) return REIST_GUI_TREE_OK;
    uint32_t current;
    if (reist_gui_tree_index(model, node_id, &current) != REIST_GUI_TREE_OK)
        return REIST_GUI_TREE_EINVAL;
    uint32_t reverse[REIST_GUI_TREE_CAPACITY];
    uint32_t depth = 0U;
    while (depth < model->node_count) {
        reverse[depth++] = model->nodes[current].id;
        if (current == 0U) break;
        current = prior_index(model, model->nodes[current].parent_id, current);
        if (current == REIST_GUI_TREE_NO_INDEX) return REIST_GUI_TREE_EINVAL;
    }
    if (reverse[depth - 1U] != model->nodes[0].id)
        return REIST_GUI_TREE_ECAPACITY;
    route->depth = depth;
    for (uint32_t index = 0U; index < depth; ++index)
        route->path[index] = reverse[depth - 1U - index];
    for (uint32_t index = depth; index < REIST_GUI_TREE_CAPACITY; ++index)
        route->path[index] = 0U;
    return REIST_GUI_TREE_OK;
}
