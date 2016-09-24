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

	if (node->prev == NULL && node->next == NULL) {
		/* no siblings, remove from parent */
		if (node->parent == NULL)
			return;
		assert(node->parent->child == node);
		node->parent->child = NULL;
	} else {
		if (node->prev && node->next) {
			/* next and prev */
			node->prev->next = node->next;
			node->next->prev = node->prev;
		} else {
			/* only one direct sibling */
			if (node->prev)
				node->prev->next = NULL;
			else {
				node->next->prev = NULL;
				/* next sibling is now head child */
				node->parent->child = node->next;
			}
		}
		/* finally set own sibling ptrs */
		node->next = NULL;
		node->prev = NULL;
	}

	if (wtree_is_tiling(node->parent)) {
		wtree_minus(node->parent);
		// remove parent-tiler if empty and not root
		if (node->parent->child == NULL && node->parent->parent)
			wtree_remove(node->parent);
	}
	node->parent = NULL;
}


//void wtree_set_child(tree_t *current, containter_t *

int wtree_count_children(wtree_t* parent)
{
	assert(parent != NULL);

	wtree_t *child = parent->child;
	int n = 0;

	while (child != NULL) {
		child = child->next;
		++n;
	}
	return n;
}

/* add _node_ after _current_ node */
void wtree_add_sibling(wtree_t *current, wtree_t *node)
{
	if (current->next == NULL) {
		/* set next-link on current node */
		current->next = node;
		node->prev = current;
	} else {
		/* set links on new node */
		node->prev = current;
		node->next = current->next;
		/* update original nodes */
		node->prev->next = node;
		node->next->prev = node;
	}
	node->parent = current->parent;
	wtree_plus(node->parent);
	assert((node->next != node->prev) || node->next == NULL);
	assert((current->next != current->prev) || current->next == NULL);
}

/* replace current client-node with tiler, add cl-node to that */
void wtree_replace_tile(wtree_t *tiler, wtree_t *client)
{
	assert(tiler != client);
	assert(tiler->child != client);

	PDEBUG("0x%x - 0x%x\n", tiler->next, tiler->prev);
	show_node("replace tiler :", tiler);
	show_node("replace client:", client);
	assert((tiler->next != tiler->prev) || tiler->next == NULL);
	assert((client->next != client->prev) || tiler->next == NULL);

	tiler->parent = client->parent;
	// check if parent knows us
	if (client->parent->child == client)
		client->parent->child = tiler;
	// update siblings
	if (client->prev)
		client->prev->next = tiler;
	if (client->next)
		client->next->prev = tiler;

	// copy sibling positions
	tiler->prev = client->prev;
	tiler->next = client->next;
	// unlink client from siblings
	client->next = client->prev = NULL;
	// add client as child
	PDEBUG("0x%x - 0x%x\n", tiler, client);
	show_node("replace rb tiler :", tiler);
	show_node("replace rb client:", client);
	wtree_append_child(tiler, client);

	assert((tiler->next != tiler->prev) || tiler->next == NULL);
	assert((client->next != client->prev) || tiler->next == NULL);
}

/* add tiling-node as sibling to current and client-node as child to tiling-node
 * current -> tiling-node (tmp) -> client-node (node) */
void wtree_add_tile_sibling(wtree_t *current, wtree_t *node,
		tiling_t tiling)
{
	wtree_t *tmp = wtree_new_tiling(tiling);

	tmp->child = node;  // add child to tiler
	node->parent = tmp; // make tiler parent to child
	wtree_plus(tmp);    // increment child count of new tiler

	wtree_add_sibling(current, tmp);

	assert((node->next != node->prev) || node->next == NULL);
	assert((current->next != current->prev) || current->next == NULL);
	assert((tmp->next != tmp->prev) || tmp->next == NULL);
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

	tree_show_node("#node#b", node);
	tree_show_node("#cur #b", current);
	// XXX tiling: zero other vars (node->next?)
	node->next = NULL;
	current->next = node;
	node->prev = current;
	node->parent = current->parent;
	wtree_plus(node->parent);

	tree_show_node("#node#a", node);
	tree_show_node("#cur #a", current);

	assert((node->next != node->prev) || node->next == NULL);
	assert((current->next != current->prev) || current->next == NULL);
}

void wtree_append_child(wtree_t *parent, wtree_t *node)
{
	assert(wtree_is_tiling(parent));

	tree_show_node("#node#b", node);
	tree_show_node("#par #b", parent);
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
