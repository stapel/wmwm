#include "window_tree.h"
#include <assert.h>    // for assert
#include <inttypes.h>  // for PRIuPTR
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uintptr_t, uint16_t
#include <stdio.h>     // for NULL, fprintf, snprintf, FILE, fclose, fopen
#include <stdlib.h>    // for calloc, free

#ifdef DEBUG
#define PDEBUG(Args...) \
	do { fprintf(stderr, "ct: "); fprintf(stderr, ##Args); } while(0)
#define D(x) x
#else
#define PDEBUG(Args...)
#define D(x)
#endif


// container helper functions, local only
static container_t* container_new()
{
	container_t *tmp = calloc(1, sizeof(container_t));
	return tmp;
}

static container_t* container_new_client(client_t *client)
{
	container_t *tmp = calloc(1, sizeof(container_t));
	if (tmp == NULL) return NULL;
	tmp->type = CONTAINER_CLIENT;
	tmp->client = client;
	return tmp;
}

static container_t* container_new_tiling(tiling_t tile)
{
	container_t *tmp = calloc(1, sizeof(container_t));
	if (tmp == NULL) return NULL;
	tmp->type = CONTAINER_TILING;
	tmp->tile = tile;
	tmp->tiles = 0;
	return tmp;
}

static container_t* wtree_data(wtree_t *node)
{
	return ((container_t*)(node->data));
}

/* increase count of clients in node */
static void wtree_plus(wtree_t *node)
{
	++(wtree_data(node)->tiles);
	PDEBUG("node+: %p (%d)\n", (void*)node, (wtree_data(node)->tiles));
}

static void wtree_minus(wtree_t *node)
{
	assert(wtree_data(node)->tiles != 0);
	--(wtree_data(node)->tiles);
	PDEBUG("node-: %p (%d)\n", (void*)node, (wtree_data(node)->tiles));
}

/***************************************************************/
// memory handling functions


// only child
bool wtree_is_singleton(wtree_t* child)
{
	assert(child != NULL);
	assert(child->parent != NULL);

	return (child->parent->child == child && child->next == NULL);
}


// client constructor
wtree_t* wtree_new_client(client_t *client)
{
	wtree_t *tmp;
	container_t *cont;
	if ((cont = container_new_client(client)) == NULL)
		return NULL;
	tmp = tree_new(NULL, NULL, NULL, NULL, cont);
	return tmp;
}

// tiling node constructor
wtree_t* wtree_new_tiling(tiling_t tile)
{
	wtree_t *tmp;
	container_t *cont;
	if ((cont = container_new_tiling(tile)) == NULL)
		return NULL;
	cont->type = CONTAINER_TILING;
	cont->tile = tile;
	tmp = tree_new(NULL, NULL, NULL, NULL, cont);
	return tmp;
}

// deconstructor, free node and its data
void wtree_free(wtree_t *node)
{
	assert(node != NULL);
	assert(node->data != NULL);

	free(node->data);
	free(node);
}

/*******************************************************/
// container handling functions

// return client of node
client_t *wtree_client(wtree_t *node)
{
	return wtree_data(node)->client;
}

// is node a client node
bool wtree_is_client(wtree_t *node)
{
	assert(node != NULL);
	return (wtree_data(node)->type == CONTAINER_CLIENT);
}

// is node a tiling node
bool wtree_is_tiling(wtree_t *node)
{
	assert(node != NULL);
	return (wtree_data(node)->type == CONTAINER_TILING);
}

// return number of children
uint16_t wtree_get_tiles(wtree_t *node)
{
	return wtree_data(node)->tiles;
}

// return mode of tiling
tiling_t wtree_tiling(wtree_t *node)
{
	assert(node != NULL);
	return wtree_data(node)->tile;

}

// tiling of parent
tiling_t wtree_parent_tiling(wtree_t *node)
{
	assert(node != NULL);
	assert(node->parent != NULL);

	return wtree_data(node->parent)->tile;
}

// set tiling mode
void wtree_set_tiling(wtree_t *node, tiling_t tiling)
{
	assert(node != NULL);
	wtree_data(node)->tile = tiling;
}

void wtree_set_parent_tiling(wtree_t *node, tiling_t tiling)
{
	assert(node != NULL);
	assert(node->parent != NULL);
	wtree_data(node->parent)->tile = tiling;
}



/*************************************************************/
// node action functions

// add _node_ after _current_ node
void wtree_add_sibling(wtree_t *current, wtree_t *node)
{
	tree_add(current, node);
	wtree_plus(node->parent);
}

// append sibling node to current
void wtree_append_sibling(wtree_t *current, wtree_t *node)
{

	assert((node->next != node->prev) || node->next == NULL);
	assert((current->next != current->prev) || current->next == NULL);

	while (current->next != NULL)
		current = current->next;

	tree_add(current, node);
	wtree_plus(node->parent);

	assert((node->next != node->prev) || node->next == NULL);
	assert((current->next != current->prev) || current->next == NULL);
}

// append child node to parent
void wtree_append_child(wtree_t *parent, wtree_t *node)
{
	assert(wtree_is_tiling(parent));

	// other vars
	// XXX: Make sure no struct-member is forgotten
	if (parent->child == NULL) {
		parent->child = node;
		wtree_plus(parent);
	} else
		wtree_append_sibling(parent->child, node);
	node->parent = parent;

	assert((node->next != node->prev) || node->next == NULL);
	assert((parent->next != parent->prev) || parent->next == NULL);
}

// interject tiler between client and client->parent
/* replace current client-node with tiler, add cl-node to that */
void wtree_inter_tile(wtree_t *client, tiling_t mode)
{
	wtree_t *tiler = wtree_new_tiling(mode);

	// replace client with tiler
	tree_replace(client, tiler);
	// add client to tiler
	wtree_append_child(tiler, client);
}

/* add tiling-node as sibling to current and client-node as child to tiling-node
 * current -> tiling-node (tmp) -> client-node (node) */

void wtree_add_tile_sibling(wtree_t *current, wtree_t *node,
		tiling_t tiling)
{
	wtree_t *tiler = wtree_new_tiling(tiling);
	// add node to tiler
	wtree_append_child(tiler, node);
	// add tiler as sibling to current
	wtree_add_sibling(current, tiler);
}

/* unlink node from tree, no children handling */
void wtree_remove(wtree_t *node)
{
	assert(node != NULL);
	tree_t *parent = tree_parent(node);

	// extract node from tree
	tree_extract(node);

	// update parent node
	if (parent && wtree_is_tiling(parent)) {
		// decrement child counter
		wtree_minus(parent);
		// in case of non-root-parent tile, remove it
		if (tree_child(parent) == NULL && tree_parent(parent)) {
			wtree_remove(parent);
			wtree_free(parent);
		}
	}
}

#if 0
void tree_show_node(char* str, tree_t *node)
{
	if (node == NULL)
		fprintf(stderr, "(%s) node: %p\n", str, NULL);
	else {
		fprintf(stderr, "(%s) node: %p\n - parent: %p\n - child: %p\n - prev: %p\n - next: %p\n",
			str, node, node->parent, node->child, node->prev, node->next);
	}
}
#endif

#if 0
wtree_t *wtree_next(wtree_t *node)
{
	// allways tiling
	if (node->child)
		return (wtree_next(node->child));

	if (node->next) {
		if(wtree_is_tiling(node->next))
			return (wtree_next(node->next));
		if (wtree_is_client(node->next))
			return node->next;
	}
	return node;
}
#endif

// recursive general function to apply _action_ on each client beneath client
// *pre-order*
void wtree_traverse_clients(wtree_t *node, void(*action)(client_t *))
{
	if (node == NULL)
		return;

	assert((node->next != node->prev) || node->next == NULL);

	if (wtree_is_client(node))
		action(wtree_client(node));

	if (node->next != NULL)
		wtree_traverse_clients(node->next, action);
	if (node->child != NULL)
		wtree_traverse_clients(node->child, action);
}

// recursive function to search for a client_t that fulfils _compare_
/* pre-order */
client_t *wtree_find_client(wtree_t *node, bool(*compare)(client_t*, void *), void *arg)
{

	if (node == NULL)
		return NULL;

	if (wtree_is_client(node) && compare(wtree_client(node), arg))
		return wtree_client(node);

	client_t *client;
	if ((client = wtree_find_client(node->next, compare, arg)))
		return client;
	if ((client = wtree_find_client(node->child, compare, arg)))
		return client;

	return NULL;
}

/*******************************************************/
// recursive helper function to print tree
static void wtree_print_tree_r(FILE *file, wtree_t *cur, int *i)
{
	if (cur == NULL)
		return;

	char *num = calloc(10, 1);

	if (wtree_is_tiling(cur)) {
		switch (wtree_tiling(cur)) {
			case TILING_VERTICAL:
				snprintf(num, 10, "V%d", *i);
				break;
			case TILING_HORIZONTAL:
				snprintf(num, 10, "H%d", *i);
				break;
			case TILING_FLOATING:
				snprintf(num, 10, "F%d", *i);
				break;
			default:
				snprintf(num, 10, "X%d", *i);
				break;
		}
		fprintf(file, "%"PRIuPTR" [label=\"%s\" shape=triangle];\n",
				(uintptr_t)cur, num);
	} else {
		snprintf(num, 10, "%d", *i);
		fprintf(file, "%"PRIuPTR" [label=\"%s\" shape=circle];\n",
			   	(uintptr_t)cur, num);
	}

	if (cur->parent) {
		fprintf(file, "%"PRIuPTR":n -> %"PRIuPTR":s;\n",
				(uintptr_t)cur, (uintptr_t)cur->parent);
	}
	if (cur->next) {
		fprintf(file, "%"PRIuPTR":e -> %"PRIuPTR":w;\n",
				(uintptr_t)cur, (uintptr_t)cur->next);
		fprintf(file, "{ rank = same; %"PRIuPTR" ; %"PRIuPTR" }\n;",
				(uintptr_t)cur, (uintptr_t)cur->next);
	}
	if (cur->prev) {
		fprintf(file, "%"PRIuPTR" -> %"PRIuPTR":e;\n",
				(uintptr_t)cur, (uintptr_t)cur->prev);
	}
	if (cur->child) {
		fprintf(file, "%"PRIuPTR":s -> %"PRIuPTR":n;\n",
				(uintptr_t)cur, (uintptr_t)cur->child);
	}
	free(num);

	if (cur->next) {
		++(*i);
		wtree_print_tree_r(file, cur->next, i);
	}
	if (cur->child) {
		++(*i);
		wtree_print_tree_r(file, cur->child, i);
	}
}

// print tree
void wtree_print_tree(wtree_t *cur)
{

	FILE *file = fopen("/tmp/graph.dot", "w");
	int i = 0;

	fprintf(file, "digraph G {\nnodesep=1.2;\n");

	wtree_print_tree_r(file, cur, &i);

	fprintf(file, "}\n");
	fclose(file);
}


