#ifndef __WMWM__CONTAINER_TREE_H__
#define __WMWM__CONTAINER_TREE_H__
#include <stdint.h>   // for uint16_t
#include "stdbool.h"  // for bool
#include "tree.h"     // for tree_t
#include "wmwm.h"     // for client_t

typedef tree_t wtree_t;

/* Tiling modes */
typedef enum tiling_modes {
	TILING_HORIZONTAL,
	TILING_VERTICAL,
	TILING_FLOATING
} tiling_t;

/* general container types */
typedef enum container_types {
	CONTAINER_TILING,
	CONTAINER_CLIENT
} container_type;

/* container * static local helper functions in window_tree.c */
typedef struct container {
	container_type type;
	union {
		struct {
			tiling_t tile;
			uint16_t tiles;
		};
		client_t *client;
	};
} container_t;

/* create new node with client/tiling "container" */
wtree_t* wtree_new_client(client_t *client);
wtree_t* wtree_new_tiling(tiling_t tile);

/* free node and its data */
void wtree_free(wtree_t *node);

/* get client from node */
client_t *wtree_client(wtree_t *node);

/* check if node is ... */
bool wtree_is_client(wtree_t *node);
bool wtree_is_tiling(wtree_t *node);
bool wtree_is_singleton(wtree_t* child);

/* get number of tiles/children */
uint16_t wtree_get_tiles(wtree_t *node);

/* get tiling_t of node/parent */
tiling_t wtree_tiling(wtree_t *node);
tiling_t wtree_parent_tiling(wtree_t *node);

/* set tiling_t of node/parent */
void wtree_set_tiling(wtree_t *node, tiling_t tiling);
void wtree_set_parent_tiling(wtree_t *node, tiling_t tiling);

/* add/append sibling/children */
void wtree_add_sibling(wtree_t *current, wtree_t *node);
void wtree_append_sibling(wtree_t *current, wtree_t *node);
void wtree_append_child(wtree_t *parent, wtree_t *node);

/* put new tiling node between client and client->parent */
void wtree_inter_tile(wtree_t *client, tiling_t mode);

/* add tiling-node as sibling to current and client-node as child to tiling-node */
void wtree_add_tile_sibling(wtree_t *current, wtree_t *node, tiling_t tiling);

/* unlink node from tree, fix siblings and parent */
void wtree_remove(wtree_t *node);

/* for each client-node below node, do action(client), pre-order*/
void wtree_traverse_clients(wtree_t *node, void(*action)(client_t *));
/* find node below _node_ that has compare(client) == true, pre-order */
client_t *wtree_find_client(wtree_t *node, bool(*compare)(client_t*, void *), void *arg);

/* print tree to dot file */
void wtree_print_tree(wtree_t *cur);

#endif
