#ifndef __WMWM__TREE_H__
#define __WMWM__TREE_H__

/* n-ary Tree implementation
 *
 * Each node has exactly one parent, two siblings (prev, next), one child
 * and a pointer to arbitrary data.
 *
 * child is always the first (prev == NULL) in the list of siblings
 */

typedef struct tree_item tree_t;

struct tree_item {
	void   *data;   /* arbitary data */

	tree_t *parent; /* parent node */
	tree_t *prev;   /* previous sibling */
	tree_t *next;   /* next sibling */
	tree_t *child;  /* child */ /* only for workspace and tiling! */
};


// helper functions
static tree_t *tree_parent(tree_t *node) { return node->parent; }
static tree_t *tree_child(tree_t *node) { return node->child; }
static void *tree_data(tree_t *node) { return node->data; }

// create new node
tree_t *tree_new(tree_t *parent, tree_t *prev, tree_t *next, tree_t *child,
		void *data);

// remove node from tree, fix ancestors, take child with it
void tree_extract(tree_t *node);

// insert _node_ as sibling before _old_
void tree_insert(tree_t *next, tree_t *node);

void tree_replace(tree_t *from, tree_t *to);

// insert _node_ as sibling after _old_
void tree_add(tree_t *next, tree_t *node);

#endif /* __WMWM__TREE_H__ */
