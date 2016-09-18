#ifndef __WMWM_LIST_H__
#define __WMWM_LIST_H__

typedef struct list_item list_t;

struct list_item {
	void *data;
	list_t *prev;
	list_t *next;
};

/*
 * Move element in item to the head of list mainlist.
 */
void list_to_head(list_t **mainlist, list_t *item);

/*
 * Create space for a new item and add it to the head of mainlist.
 *
 * Returns item or NULL if out of memory.
 */
list_t *list_add(list_t **mainlist);

/*
 * Remove item from list mainlist.
 */
void list_remove(list_t **mainlist, list_t *item);

/*
 * Free any data in current item and then delete item. Optionally
 * update number of items in list if stored != NULL.
 */
void list_erase(list_t **list, int *stored, list_t *item);

/*
 * Delete all items in list. Optionally update number of items in list
 * if stored != NULL.
 */
void list_erase_all(list_t **list, int *stored);

/*
 * Print all items in mainlist on stdout.
 */
void list_print(list_t *mainlist);

#endif /* __WMWM_LIST_H__ */
