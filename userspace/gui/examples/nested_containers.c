/** Buildable installed-SDK example for nested caller-owned containers. */
#include <reist/gui/container.h>

int main(void) {
    static const reist_gui_node_t nodes[] = {
        {1U, 0U, REIST_GUI_NODE_CONTAINER, 0U, "Window",
         {0, 0, 320U, 200U},
         REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED,
         {0U, 0U, 0U, 0U}},
        {2U, 1U, REIST_GUI_NODE_CONTAINER, 0U, "Settings page",
         {8, 8, 304U, 184U},
         REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED,
         {0U, 0U, 0U, 0U}},
        {3U, 2U, REIST_GUI_NODE_CONTROL, 100U, "Apply button",
         {16, 140, 96U, 28U},
         REIST_GUI_NODE_VISIBLE | REIST_GUI_NODE_ENABLED,
         {0U, 0U, 0U, 0U}},
    };
    static const reist_gui_tree_model_t tree = {
        REIST_GUI_TREE_API_VERSION, sizeof(reist_gui_tree_model_t),
        nodes, 3U, {0U, 0U, 0U, 0U}
    };
    return reist_gui_tree_validate(&tree) == 0 ? 0 : 1;
}
