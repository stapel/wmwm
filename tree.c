#include "tree.h"
#include <stdlib.h>

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
