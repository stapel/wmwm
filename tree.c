#include "tree.h"
#include <stdlib.h>

#include <assert.h>

#include <stdio.h>
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
	// update parent and siblings
	if (node->prev)
		node->prev->next = news;
	if (node->next)
		node->next->prev = news;
	if (node->parent && node->parent->child == node)
		node->parent->child = news;

	// update nodes
	news->parent = node->parent;
	news->next = node->next;
	news->prev = node->prev;

	node->parent = NULL;
	node->next = NULL;
	node->prev = NULL;
}

// XXX That's wrong
// swap nodes, keep children
void tree_swap(tree_t *from, tree_t *to)
{
	assert(from != NULL);
   	assert(to != NULL);
	assert(from->parent != to && to->parent != from);

	tree_t tmp_from = *from;
	tree_t tmp_to   = *to;

	// check if nodes are adjascent
	if (from->next == to) {
		// update nodes
		from->next = to->next;
		to->next = from;
		to->prev = from->prev;
		from->prev = to;

		// update unsuspecting neighbors
		if (from->next)
			from->next->prev = from;
		if (to->prev)
			to->prev->next = to;

		// update parents
		if (from->parent && from->parent->child == from)
			from->parent->child = to;
	} else if (to->next == from) {
		to->next = from->next;
		from->next = to;

		from->prev = to->prev;
		to->prev = from;

		// update unsuspecting neighbors
		if (to->next)
			to->next->prev = to;
		if (from->prev)
			from->prev->next = from;

		// update onisan
		if (to->parent && to->parent->child == to)
			to->parent->child = from;
	} else {
		// update neighbors
		if (from->next)
			from->next->prev = to;
		if (from->prev)
			from->prev->next = to;
		if (to->next)
			to->next->prev = from;
		if (to->prev)
			to->prev->next = from;

		// update nodes
		from->next = tmp_to.next;
		from->prev = tmp_to.prev;

		to->next = tmp_from.next;
		to->prev = tmp_from.prev;
	}
	// update parent infroamtino of nodes
	from->parent = tmp_to.parent;
	to->parent   = tmp_from.parent;
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


tree_t *tree_walk_up_left(tree_t *node)
{
	if (node->parent == NULL)
		return NULL;

	if (node->parent->next)
		return node->parent->next;
	return tree_walk_up_left(node->parent);
}

// Labyrinth-walk, go down, keep on the left wall
tree_t *tree_walk_down_right(tree_t *node)
{
	if (node == NULL)
		return NULL;

	if (node->child)
		return node->child;

	if (node->next)
		return node->next;

	return tree_walk_up_left(node);

}
