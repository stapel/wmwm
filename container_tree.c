#include "container_tree.h"
#include <stdlib.h>
#include <assert.h>

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

static container_t* container_new_tiling(tiling_mode_t tile)
{
	container_t *tmp = calloc(1, sizeof(container_t));
	if (tmp == NULL) return NULL;
	tmp->type = CONTAINER_TILING;
	tmp->tile = tile;
	return tmp;
}

client_t *ctree_client(ctree_t *node)
{
	return (((container_t*)(node->data))->client);
}

bool ctree_is_client(ctree_t *node)
{
	return (((container_t*)(node->data))->type == CONTAINER_CLIENT);
}

ctree_t* ctree_new_client(client_t *client)
{
	ctree_t *tmp;
	container_t *cont;
	if ((cont = container_new()) == NULL)
		return NULL;

	cont->type = CONTAINER_CLIENT;
	cont->client = client;

	tmp = tree_new(NULL, NULL, NULL, NULL, cont);
	return tmp;
}

bool ctree_is_tiling(ctree_t *node)
{
	return ((container_t*)(node->data))->type == CONTAINER_TILING;
}

ctree_t* ctree_new_tiling(tiling_mode_t tile)
{
	ctree_t *tmp;
	container_t *cont;
	if ((cont = container_new()) == NULL)
		return NULL;
	cont->type = CONTAINER_TILING;
	cont->tile = tile;
	tmp = tree_new(NULL, NULL, NULL, NULL, cont);
	return tmp;
}

void ctree_free(ctree_t *node)
{
	assert(node != NULL);
	assert(node->data != NULL);

	free(node->data);
	free(node);
}

/* unlink node from tree, no children handling */
void ctree_remove(ctree_t *node)
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
	node->parent = NULL;
}


//void ctree_set_child(tree_t *current, containter_t *

int ctree_count_children(ctree_t* parent)
{
	assert(parent != NULL);

	ctree_t *child = parent->child;
	int n = 0;

	while (child != NULL) {
		child = child->next;
		++n;
	}
	return n;
}

/* add _add_ after _current_ node */
void ctree_add_sibling(ctree_t *current, ctree_t *node)
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
}

void ctree_append_sibling(ctree_t *current, ctree_t *node)
{
	while (current->next != NULL)
		current = current->next;

	current->next = node;
	node->prev = current->next;
}

void ctree_append_child(ctree_t *parent, ctree_t *node)
{
	assert(ctree_is_tiling(parent));

	if (parent->child == NULL)
		parent->child = node;
	else
		ctree_append_sibling(parent->child, node);

	node->parent = parent;
}


/* pre-order */
void ctree_traverse_clients(ctree_t *node, void(*action)(client_t *))
{
	if (node == NULL)
		return;

	if (ctree_is_client(node))
		action(ctree_client(node));

	if (node->next != NULL)
		ctree_traverse_clients(node->next, action);
	if (node->child != NULL)
		ctree_traverse_clients(node->child, action);
}


/* pre-order */
client_t *ctree_find_client(ctree_t *node, bool(*compare)(client_t*, void *), void *arg)
{

	if (node == NULL)
		return NULL;

	if (ctree_is_client(node) && compare(ctree_client(node), arg))
		return ctree_client(node);

	client_t *client;
	if ((client = ctree_find_client(node->next, compare, arg)))
		return client;

	if ((client = ctree_find_client(node->child, compare, arg)))
		return client;

	return NULL;
}
