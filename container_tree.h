#ifndef __WMWM__CONTAINER_TREE_H__
#define __WMWM__CONTAINER_TREE_H__

#include "stdbool.h"
#include "tree.h"

typedef tree_t ctree_t;

#include "wmwm.h"

/* Tiling modes */
typedef enum tiling_modes {
	TILING_HORIZONTAL,
	TILING_VERTICAL,
	TILING_FLOATING
} tiling_mode_t;


/* dumb alias */

/* general container types */
/****************************************************************/
/* Possible container types */
typedef enum container_types {
	CONTAINER_TILING,
	CONTAINER_CLIENT
} container_type;

/* container */
typedef struct container {
	container_type type;
	union {
		tiling_mode_t  tile;
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

/* client-ctree-node *****************
   - MUST NOT HAVE child
   - MUST HAVE a parent
   - MAY HAVE siblings

 * tiling-ctree-node *****************

   if mode == floating
   - MUST HAVE client-ctree-node child
   - MUST HAVE a parent

   if mode == tiling
   - MUST HAVE children
   - MUST HAVE a parent if not root-node
   - MAY HAVE siblings

*/


bool ctree_is_client(ctree_t *node);
bool ctree_is_tiling(ctree_t *node);

/* create new node with client/tiling "container" */
ctree_t* ctree_new_client(client_t *client);
ctree_t* ctree_new_tiling(tiling_mode_t tile);

/* free node and its data */
void ctree_free(ctree_t *node);

/* unlink node from tree, no children handling */
void ctree_remove(ctree_t *node);

/* count children of parent */
int ctree_count_children(ctree_t* parent);

/* add _add_ after _current_ node */
void ctree_add_sibling(ctree_t *current, ctree_t *node);
void ctree_append_sibling(ctree_t *current, ctree_t *node);
void ctree_append_child(ctree_t *parent, ctree_t *node);

void ctree_traverse_clients(ctree_t *node, void(*action)(client_t *));

client_t *ctree_find_client(ctree_t *node, bool(*compare)(client_t*, void *), void *arg);

#endif
