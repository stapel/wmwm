#include "tree.h"
#include <stdlib.h>

#include <assert.h>

tree_t* tree_new(tree_t *parent, tree_t *prev, tree_t *next, tree_t *child,
		void *data)
{
	tree_t *tmp;

	if ((tmp = calloc(1, sizeof(tree_t))) == NULL)
		return NULL;

	tmp->parent = parent;
	tmp->prev = prev;
	tmp->next = next;
	tmp->child = child;
	tmp->data = data;
	return tmp;
}

// insert sibling _node_ right before _old_
void tree_insert(tree_t *old, tree_t *node)
{
	assert(old != NULL); assert(node != NULL);

	if (old->prev)
		old->prev->next = node;
	else
		// old is first in list, set new parent->child
		old->parent->child = node;

	node->prev = old->prev;
	old->prev = node;
	node->next = old;
	node->parent = old->parent;

}

// insert sibling _node_ right after _old_
void tree_add(tree_t *old, tree_t *node)
{
	assert(old != NULL); assert(node != NULL);

	// old next
	if (old->next != NULL) {
		old->next->prev = node;
	}

	node->parent = old->parent;
	node->next = old->next;
	node->prev = old;

	old->next = node;
}

void tree_replace(tree_t *node, tree_t *news)
{
	assert(node != NULL);
	if (node->prev)
		node->prev->next = news;

	if (node->next)
		node->next->prev = news;

	if (node->parent && node->parent->child == node)
		node->parent->child = news;

	news->parent = node->parent;
	news->next = node->next;
	news->prev = node->prev;

	node->parent = NULL;
	node->next = NULL;
	node->prev = NULL;
}

// remove node from ancestors, keep children
void tree_extract(tree_t *node)
{
	assert(node != NULL);

	// relink siblings
	if (node->next)
		node->next->prev = node->prev;

	if (node->prev)
		// update previous sibling
		node->prev->next = node->next;
	else
		// we were first node, update parent
		node->parent->child = node->next;
	node->next   = NULL;
	node->prev 	 = NULL;
	node->parent = NULL;
}
