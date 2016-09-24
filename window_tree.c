#include "window_tree.h"
#include <stdlib.h>
#include <assert.h>

#include <stdio.h>


#ifdef DEBUG
#define PDEBUG(Args...) \
	do { fprintf(stderr, "ct: "); fprintf(stderr, ##Args); } while(0)
#define D(x) x
#else
#define PDEBUG(Args...)
#define D(x)
#endif

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
	tmp->x_scale = 0;
	tmp->y_scale = 0;
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
	PDEBUG("node+: %p (%d)\n", node, (wtree_data(node)->tiles));
}

static void wtree_minus(wtree_t *node)
{
	assert(wtree_data(node)->tiles != 0);
	--(wtree_data(node)->tiles);
	PDEBUG("node-: %p (%d)\n", node, (wtree_data(node)->tiles));
}


client_t *wtree_client(wtree_t *node)
{
	return wtree_data(node)->client;
}

bool wtree_is_client(wtree_t *node)
{
	return (wtree_data(node)->type == CONTAINER_CLIENT);
}

wtree_t* wtree_new_client(client_t *client)
{
	wtree_t *tmp;
	container_t *cont;
	if ((cont = container_new_client(client)) == NULL)
		return NULL;
	tmp = tree_new(NULL, NULL, NULL, NULL, cont);
	return tmp;
}

bool wtree_is_tiling(wtree_t *node)
{
	assert(node != NULL);

	return (wtree_data(node)->type == CONTAINER_TILING);
}

uint16_t wtree_get_tiles(wtree_t *node)
{
	return wtree_data(node)->tiles;
}

tiling_t wtree_tiling(wtree_t *node)
{
	assert(node != NULL);
	return wtree_data(node)->tile;

}

void wtree_set_tiling(wtree_t *node, tiling_t tiling)
{
	wtree_data(node)->tile = tiling;
}

tiling_t wtree_parent_tiling(wtree_t *node)
{
	assert(node != NULL);
	assert(node->parent != NULL);

	return wtree_data(node->parent)->tile;
}

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

void wtree_free(wtree_t *node)
{
	assert(node != NULL);
	assert(node->data != NULL);

	free(node->data);
	free(node);
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

void wtree_print_tree_r(FILE *file, wtree_t *cur, int *i)
{
	if (cur == NULL)
		return;

	char *num = calloc(10, 1);

	if (wtree_is_tiling(cur)) {
		if (wtree_tiling(cur) == TILING_VERTICAL)
			snprintf(num, 10, "V%02d\0", *i);
		else
			snprintf(num, 10, "H%02d\0", *i);
		fprintf(file, "%lu [label=\"%s\" shape=triangle];\n", cur, num);
	} else {
		snprintf(num, 10, "%02d\0", *i);
		fprintf(file, "%lu [label=\"%s\" shape=circle];\n", cur, num);
	}

	if (cur->parent) {
		fprintf(file, "%lu:n -> %lu:s;\n", cur, cur->parent);
	}
	if (cur->next) {
		fprintf(file, "%lu:e -> %lu:w;\n", cur, cur->next);
		fprintf(file, "{ rank = same; %lu ; %lu }\n;", cur, cur->next);
	}
	if (cur->prev) {
		fprintf(file, "%lu:w -> %lu:e;\n", cur, cur->prev);
	}
	if (cur->child) {
		fprintf(file, "%lu:s -> %lu:n;\n", cur, cur->child);
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

void wtree_print_tree(wtree_t *cur)
{

	FILE *file = fopen("/tmp/graph.dot", "w");
	int i = 0;

	fprintf(file, "digraph G {\nnodesep=1.2;\n");
	wtree_print_tree_r(file, cur, &i);

	fprintf(file, "}\n");
	fsync(file);
	fclose(file);
}

/* add _node_ after _current_ node */
void wtree_add_sibling(wtree_t *current, wtree_t *node)
{
	tree_add(current, node);
	wtree_plus(node->parent);
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

	PDEBUG("0x%x - 0x%x\n", tiler, client);
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

void tree_show_node(char* str, tree_t *node)
{
	if (node == NULL)
		fprintf(stderr, "(%s) node: %p\n", str, NULL);
	else {
		fprintf(stderr, "(%s) node: %p\n - parent: %p\n - child: %p\n - prev: %p\n - next: %p\n",
			str, node, node->parent, node->child, node->prev, node->next);
	}
}


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


void wtree_foreach_sibling(wtree_t *node, void(*action)(client_t *))
{
	while (node != NULL) {
		action(wtree_client(node));
		node = node->next;
	}
}

/* pre-order */
#if 0
void wtree_traverse_clients_p(wtree_t *node, void(*action)(client_t *), void *arg)
{
	if (node == NULL)
		return;

	if (wtree_is_client(node))
		action(wtree_client(node), arg);

	if (node->next != NULL)
		wtree_traverse_clients(node->next, action);
	if (node->child != NULL)
		wtree_traverse_clients(node->child, action);
}
#endif

/* pre-order */
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
