#ifndef __WMWM__TREE_H__
#define __WMWM__TREE_H__

/* n-ary Tree implementation
 *
 * Each node has exactly one parent, two siblings (prev, next), one child
 * and a pointer to arbitrary data.
 *
 */

typedef struct tree_item tree_t;

struct tree_item {
	void   *data;   /* arbitary data */

	tree_t *parent; /* parent node */
	tree_t *prev;   /* previous sibling */
	tree_t *next;   /* next sibling */
	tree_t *child;  /* child */ /* only for workspace and tiling! */
};

tree_t* tree_new(tree_t *parent, tree_t *prev, tree_t *next, tree_t *child,
		void *data);
tree_t* tree_add_sibling(tree_t *item, void *data);
tree_t* tree_new_child(tree_t *item, void *data);

#endif /* __WMWM__TREE_H__ */
