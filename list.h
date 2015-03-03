typedef struct item item_t;
struct item {
	void *data;
	item_t *prev;
	item_t *next;
};

/*
 * Move element in item to the head of list mainlist.
 */
void movetohead(item_t **mainlist, item_t *item);

/*
 * Create space for a new item and add it to the head of mainlist.
 *
 * Returns item or NULL if out of memory.
 */
item_t *additem(item_t **mainlist);

/*
 * Delete item from list mainlist.
 */
void delitem(item_t **mainlist, item_t *item);

/*
 * Free any data in current item and then delete item. Optionally
 * update number of items in list if stored != NULL.
 */
void freeitem(item_t **list, int *stored, item_t *item);

/*
 * Delete all items in list. Optionally update number of items in list
 * if stored != NULL.
 */
void delallitems(item_t **list, int *stored);

/*
 * Print all items in mainlist on stdout.
 */
void listitems(item_t *mainlist);

