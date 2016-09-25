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



/* dumb alias */

/* general container types */
/****************************************************************/
/* Possible container types */
typedef enum container_types {
	CONTAINER_TILING,
	CONTAINER_CLIENT
} container_type;



// XXX tiling: fix union and stuff
/* container */
typedef struct container {
	container_type type;
	union {
		struct {
			tiling_t tile;
			uint16_t  tiles;
			uint16_t x_scale;
			uint16_t y_scale;
		};
		client_t    *client;
	};
} container_t;

/* client-wtree-node *****************
   - MUST NOT HAVE child
   - MUST HAVE a parent
   - MAY HAVE siblings

 * tiling-wtree-node *****************

   if mode == floating
   - MUST HAVE client-wtree-node child
   - MUST HAVE a parent (except root)

   if mode == tiling
   - MUST HAVE children
   - MUST HAVE a parent if not root-node
   - MAY HAVE siblings

*/



/* create new node with client/tiling "container" */
wtree_t* wtree_new_client(client_t *client);
wtree_t* wtree_new_tiling(tiling_t tile);
/* free node and its data */
void wtree_free(wtree_t *node);


client_t *wtree_client(wtree_t *node);

bool wtree_is_client(wtree_t *node);
bool wtree_is_tiling(wtree_t *node);
bool wtree_is_singleton(wtree_t* child);

uint16_t wtree_get_tiles(wtree_t *node);
tiling_t wtree_tiling(wtree_t *node);
tiling_t wtree_parent_tiling(wtree_t *node);
void wtree_set_tiling(wtree_t *node, tiling_t tiling);
void wtree_set_parent_tiling(wtree_t *node, tiling_t tiling);

void wtree_add_sibling(wtree_t *current, wtree_t *node);
void wtree_append_sibling(wtree_t *current, wtree_t *node);
void wtree_append_child(wtree_t *parent, wtree_t *node);
void wtree_inter_tile(wtree_t *client, tiling_t mode);
void wtree_add_tile_sibling(wtree_t *current, wtree_t *node,
		tiling_t tiling);
void wtree_remove(wtree_t *node);

void wtree_traverse_clients(wtree_t *node, void(*action)(client_t *));
client_t *wtree_find_client(wtree_t *node, bool(*compare)(client_t*, void *), void *arg);

/* print tree */
void wtree_print_tree(wtree_t *cur);


#endif
