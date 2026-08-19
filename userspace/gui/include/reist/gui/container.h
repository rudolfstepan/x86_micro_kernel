/**
 * @file reist/gui/container.h
 * @brief Fixed-capacity nested widget/container hierarchy.
 *
 * The caller owns one immutable preorder node array for its complete lifetime.
 * Node zero is the root container. Every later node names an earlier container
 * as parent, so malformed cycles and orphaned children fail before use without
 * recursion. A child rectangle is local to its parent content origin.
 * Containers may contain controls and other containers to arbitrary depth up
 * to REIST_GUI_TREE_CAPACITY. Resolved clips are intersected with every parent
 * container, preventing a descendant from painting or receiving hits outside
 * its ancestry.
 *
 * The tree describes composition only. Concrete control state and rendering
 * remain in their respective APIs; target_id connects a leaf to that state.
 */
#ifndef REIST_GUI_CONTAINER_H
#define REIST_GUI_CONTAINER_H

#include <stdint.h>

#include "reist/gui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_TREE_API_VERSION 1U
#define REIST_GUI_TREE_CAPACITY 32U
#define REIST_GUI_TREE_NAME_LIMIT 48U
#define REIST_GUI_TREE_NO_ID 0U
#define REIST_GUI_TREE_NO_INDEX UINT32_MAX

enum reist_gui_tree_status {
    REIST_GUI_TREE_OK = 0,
    REIST_GUI_TREE_EINVAL = -1,
    REIST_GUI_TREE_ECAPACITY = -2
};

enum reist_gui_node_role {
    REIST_GUI_NODE_CONTAINER = 1U,
    REIST_GUI_NODE_CONTROL
};

enum reist_gui_node_flags {
    REIST_GUI_NODE_VISIBLE = 1U << 0,
    REIST_GUI_NODE_ENABLED = 1U << 1
};

/** One node in caller-defined preorder. */
typedef struct reist_gui_node {
    uint32_t id;        /**< Stable nonzero node identity. */
    uint32_t parent_id; /**< Zero only for root; otherwise a prior container. */
    uint32_t role;      /**< Container or control leaf. */
    uint32_t target_id; /**< External control/page ID; zero for containers. */
    const char *name;   /**< Nonempty semantic name, NUL within limit. */
    reist_gui_rect_t bounds; /**< Parent-local geometry. */
    uint32_t flags;
    uint32_t reserved[4];
} reist_gui_node_t;

typedef struct reist_gui_tree_model {
    uint32_t version;
    uint32_t struct_size;
    const reist_gui_node_t *nodes;
    uint32_t node_count;
    uint32_t reserved[4];
} reist_gui_tree_model_t;

/** Resolved absolute geometry and ancestry clip for one node. */
typedef struct reist_gui_node_geometry {
    reist_gui_rect_t bounds;
    reist_gui_rect_t clip;
} reist_gui_node_geometry_t;

/**
 * Addressed event route from root to one hit-tested leaf.
 *
 * Applications visit path[0..depth-2] for capture, path[depth-1] once as the
 * target, then path[depth-2..0] for bubbling. A handler may stop propagation;
 * the library never broadcasts to unrelated siblings.
 */
typedef struct reist_gui_event_route {
    uint32_t version;
    uint32_t struct_size;
    uint32_t depth;
    uint32_t target_node_id;
    uint32_t target_id;
    uint32_t path[REIST_GUI_TREE_CAPACITY];
    uint32_t reserved[4];
} reist_gui_event_route_t;

/** Initialize a route before reuse. @param[out] route NULL-safe destination. */
void reist_gui_event_route_initialize(reist_gui_event_route_t *route);

/** Validate the complete bounded hierarchy. @param[in] model Tree model.
 * @return OK or an error before publishing geometry. */
int reist_gui_tree_validate(const reist_gui_tree_model_t *model);
/** Resolve a stable node ID. @param[in] model Valid tree. @param[in] node_id ID.
 * @param[out] index_out Unchanged on failure. @return OK or EINVAL. */
int reist_gui_tree_index(const reist_gui_tree_model_t *model,
                         uint32_t node_id, uint32_t *index_out);
/** Resolve absolute bounds and ancestry clip. @param[in] model Valid tree.
 * @param[in] node_id Node ID. @param[out] geometry_out Result.
 * @return OK or a validation/capacity error. */
int reist_gui_tree_geometry(const reist_gui_tree_model_t *model,
                            uint32_t node_id,
                            reist_gui_node_geometry_t *geometry_out);
/** Find the first direct child. @param[in] model Valid tree.
 * @param[in] container_id Parent ID. @param[out] child_index_out Index or NO_INDEX.
 * @return OK or EINVAL. */
int reist_gui_tree_first_child(const reist_gui_tree_model_t *model,
                               uint32_t container_id,
                               uint32_t *child_index_out);
/** Find the next direct sibling. @param[in] model Valid tree.
 * @param[in] child_index Current child. @param[out] sibling_index_out Index or NO_INDEX.
 * @return OK or EINVAL. */
int reist_gui_tree_next_sibling(const reist_gui_tree_model_t *model,
                                uint32_t child_index,
                                uint32_t *sibling_index_out);
/** Hit-test the frontmost enabled leaf. @param[in] model Valid tree.
 * @param[in] x Local x. @param[in] y Local y. @param[out] node_id_out Node or zero.
 * @param[out] target_id_out External target or zero. @return OK or EINVAL. */
int reist_gui_tree_hit_test(const reist_gui_tree_model_t *model,
                            int32_t x, int32_t y,
                            uint32_t *node_id_out,
                            uint32_t *target_id_out);
/** Build one root-to-target route. @param[in] model Valid tree.
 * @param[in] x Local x. @param[in] y Local y.
 * @param[in,out] route Fresh initialized route. @return OK or an error. */
int reist_gui_tree_route(const reist_gui_tree_model_t *model,
                         int32_t x, int32_t y,
                         reist_gui_event_route_t *route);

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_CONTAINER_H */
