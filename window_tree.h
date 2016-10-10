#ifndef __WMWM__CONTAINER_TREE_H__
#define __WMWM__CONTAINER_TREE_H__
#include <stdint.h>   // for uint16_t
#include "stdbool.h"  // for bool
#include "tree.h"     // for tree_t
#include "wmwm.h"     // for client_t

/* Rules:
 *
 * a client-node SHALL be child of a tiling-node
 * a tiling-node SHALL have one or more children
 * a workspace-node SHALL have one or no child
 * a client-node or a tiling node MAY have siblings
 * a tiling-node SHALL know the number of non-float client-node children
 *

 --> when there is a fundamental change in children of tiling-nodes (0->1 or 1 -> 0), that should be propagated to the top node, as long there is a fundamental change on the way: Geometry neu setzen?

 */

typedef tree_t wtree_t;

/* Tiling modes */
typedef enum tiling_modes {
	TILING_HORIZONTAL,
	TILING_VERTICAL
} tiling_t;

/* general container types */
typedef enum container_types {
	CONTAINER_WORKSPACE,
	CONTAINER_TILING,
	CONTAINER_CLIENT
} container_type;

/* container * static local helper functions in window_tree.c */
// XXX order?
typedef struct container { // (24b on x86_64)
	container_type type; // (4b on x86_64)
	union {
		// CONTAINER_WORKSPACE (16b on x86_64)
		struct {
			client_t *focuswin;
			xcb_rectangle_t sgeo;
		};
		// CONTAINER_TILING (14->16b on x86_64)
		struct {
			xcb_rectangle_t tgeo;
			tiling_t tile;
			uint16_t tiles;
		};
		// CONTAINER_CLIENT (13->16b on x86_64)
		struct {
			client_t *client;
			float favor; // XXX, window size in relation to others
			bool floating;
		};
	};
} container_t;

/* create new node with client/tiling "container" */
wtree_t* wtree_new_client(client_t *client, bool floating);
wtree_t* wtree_new_tiling(tiling_t tile);
wtree_t* wtree_new_workspace(xcb_rectangle_t geo);




client_t *wtree_focuswin(wtree_t *node);
void wtree_set_focuswin(wtree_t *node, client_t *client);



/* free node and its data */
void wtree_free(wtree_t *node);

/* get client from node */
client_t *wtree_client(wtree_t *node);

/* check if node is ... */
bool wtree_is_client(wtree_t *node);
bool wtree_is_tiling(wtree_t *node);
bool wtree_is_workspace(wtree_t *node);
bool wtree_is_floating(wtree_t *node);

wtree_t *wtree_next(wtree_t *node);

bool wtree_toggle_floating(wtree_t *node);
// bool wtree_is_singleton(wtree_t* child); tree.h

/* get number of tiles/children */
uint16_t wtree_tiles(wtree_t *node);

/* get tiling_t of node/parent */
tiling_t wtree_tiling(wtree_t *node);
tiling_t wtree_parent_tiling(wtree_t *node);

/* set tiling_t of node/parent */
void wtree_set_tiling(wtree_t *node, tiling_t tiling);
void wtree_set_parent_tiling(wtree_t *node, tiling_t tiling);

/* swap from with to */
static void wtree_swap(wtree_t *from, wtree_t *to) { tree_swap(from, to); }

/* add/append sibling/children */
void wtree_add_sibling(wtree_t *current, wtree_t *node);
void wtree_append_sibling(wtree_t *current, wtree_t *node);
void wtree_append_child(wtree_t *parent, wtree_t *node);

/* put new tiling node between client and client->parent */
void wtree_inter_tile(wtree_t *client, tiling_t mode);

/* add tiling-node as sibling to current and client-node as child to tiling-node */
void wtree_add_tile_sibling(wtree_t *current, wtree_t *node, tiling_t tiling);
void wtree_append_tile_child(wtree_t *current, wtree_t *node, tiling_t tiling);

/* unlink node from tree, fix siblings and parent */
void wtree_remove(wtree_t *node);

/* for each client-node below node, do action(client), pre-order*/
void wtree_traverse_clients(wtree_t *node, void(*action)(client_t *));
/* find node below _node_ that has compare(client) == true, pre-order */
client_t *wtree_find_client(wtree_t *node, bool(*compare)(client_t*, void *), void *arg);

/* print tree to dot file */
void wtree_print_tree(wtree_t *cur);

#endif
