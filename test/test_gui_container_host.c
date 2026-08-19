#include <assert.h>

#include "reist/gui/container.h"

static const reist_gui_node_t nodes[] = {
    {1U, 0U, REIST_GUI_NODE_CONTAINER, 0U, "Root",
     {0, 0, 200U, 120U},
     REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED, {0U, 0U, 0U, 0U}},
    {2U, 1U, REIST_GUI_NODE_CONTAINER, 0U, "Page",
     {10, 10, 160U, 90U},
     REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED, {0U, 0U, 0U, 0U}},
    {3U, 2U, REIST_GUI_NODE_CONTAINER, 0U, "Group",
     {20, 15, 80U, 40U},
     REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED, {0U, 0U, 0U, 0U}},
    {4U, 3U, REIST_GUI_NODE_CONTROL, 77U, "Nested button",
     {50, 10, 60U, 20U},
     REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED, {0U, 0U, 0U, 0U}},
};

static const reist_gui_tree_model_t model = {
    REIST_GUI_TREE_API_VERSION, sizeof(reist_gui_tree_model_t),
    nodes, 4U, {0U, 0U, 0U, 0U}
};

static void test_nested_geometry_and_parent_clipping(void) {
    assert(reist_gui_tree_validate(&model) == 0);
    reist_gui_node_geometry_t geometry;
    assert(reist_gui_tree_geometry(&model, 4U, &geometry) == 0);
    assert(geometry.bounds.x == 80 && geometry.bounds.y == 35);
    assert(geometry.bounds.width == 60U && geometry.bounds.height == 20U);
    assert(geometry.clip.x == 80 && geometry.clip.y == 35);
    assert(geometry.clip.width == 30U && geometry.clip.height == 20U);

    uint32_t node = 0U;
    uint32_t target = 0U;
    assert(reist_gui_tree_hit_test(&model, 85, 40, &node, &target) == 0);
    assert(node == 4U && target == 77U);
    reist_gui_event_route_t route;
    reist_gui_event_route_initialize(&route);
    assert(reist_gui_tree_route(&model, 85, 40, &route) == 0);
    assert(route.depth == 4U && route.target_node_id == 4U &&
           route.target_id == 77U);
    assert(route.path[0] == 1U && route.path[1] == 2U &&
           route.path[2] == 3U && route.path[3] == 4U);
    assert(reist_gui_tree_hit_test(&model, 115, 40, &node, &target) == 0);
    assert(node == 0U && target == 0U);
}

static void test_child_iteration_and_invalid_parent_order(void) {
    uint32_t child = REIST_GUI_TREE_NO_INDEX;
    assert(reist_gui_tree_first_child(&model, 1U, &child) == 0);
    assert(child == 1U);
    assert(reist_gui_tree_first_child(&model, 2U, &child) == 0);
    assert(child == 2U);
    assert(reist_gui_tree_next_sibling(&model, child, &child) == 0);
    assert(child == REIST_GUI_TREE_NO_INDEX);

    reist_gui_node_t invalid_nodes[2] = {nodes[0], nodes[1]};
    invalid_nodes[1].parent_id = 99U;
    reist_gui_tree_model_t invalid = model;
    invalid.nodes = invalid_nodes;
    invalid.node_count = 2U;
    assert(reist_gui_tree_validate(&invalid) == REIST_GUI_TREE_EINVAL);

    reist_gui_node_t duplicate_nodes[5] = {
        nodes[0], nodes[1], nodes[2], nodes[3], nodes[3]};
    duplicate_nodes[4].id = 5U;
    reist_gui_tree_model_t duplicate = model;
    duplicate.nodes = duplicate_nodes;
    duplicate.node_count = 5U;
    assert(reist_gui_tree_validate(&duplicate) == REIST_GUI_TREE_EINVAL);
}

int main(void) {
    test_nested_geometry_and_parent_clipping();
    test_child_iteration_and_invalid_parent_order();
    return 0;
}
