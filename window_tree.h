#ifndef __WMWM__CONTAINER_TREE_H__
#define __WMWM__CONTAINER_TREE_H__

#include "stdbool.h"
#include "tree.h"

typedef tree_t wtree_t;

#include "wmwm.h"

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

/****************************************************************/

/* Die tatsächlich in wmwm.c benutzten Funktionenen: */
/****************************************************************/
/*
 * einen Client nach dem aktuellem node einfügen
   Dabei gegebenenfalls nachfolger beachten
 * einen node rausnehmen (prüfen ob children etc), aufräumen
 *

*/

/* einen containter? (client) nach aktuellem node (container?) einfügen */
//add_sibling

/* sibling verschieben */
//move_sibling

/****************************************************************/
/* Die eigentlich wichtigen Funktionen sollten container-tree sein */
/****************************************************************/

/* client-wtree-node *****************
   - MUST NOT HAVE child
   - MUST HAVE a parent
   - MAY HAVE siblings

 * tiling-wtree-node *****************

   if mode == floating
   - MUST HAVE client-wtree-node child
   - MUST HAVE a parent

   if mode == tiling
   - MUST HAVE children
   - MUST HAVE a parent if not root-node
   - MAY HAVE siblings

*/


uint16_t wtree_get_tiles(wtree_t *node);
client_t *wtree_client(wtree_t *node);

bool wtree_is_client(wtree_t *node);
bool wtree_is_tiling(wtree_t *node);

/* create new node with client/tiling "container" */
wtree_t* wtree_new_client(client_t *client);
wtree_t* wtree_new_tiling(tiling_t tile);
void wtree_replace_tile(wtree_t *tiler, wtree_t *client);


tiling_t wtree_tiling(wtree_t *node);
tiling_t wtree_parent_tiling(wtree_t *node);

void wtree_set_tiling(wtree_t *node, tiling_t tiling);


/* free node and its data */
void wtree_free(wtree_t *node);

/* unlink node from tree, no children handling */
void wtree_remove(wtree_t *node);

/* count children of parent */
int wtree_count_children(wtree_t* parent);

/* add _add_ after _current_ node */
void wtree_add_sibling(wtree_t *current, wtree_t *node);
void wtree_add_tile_sibling(wtree_t *current, wtree_t *node,
		tiling_t tiling);
void wtree_append_sibling(wtree_t *current, wtree_t *node);
void wtree_append_child(wtree_t *parent, wtree_t *node);

void wtree_traverse_clients(wtree_t *node, void(*action)(client_t *));
void wtree_traverse_clients_p(wtree_t *node, void(*action)(client_t *), void *arg);

client_t *wtree_find_client(wtree_t *node, bool(*compare)(client_t*, void *), void *arg);
client_t *wtree_find_first_client(wtree_t *node);

#endif
