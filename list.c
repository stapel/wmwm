#include <stdlib.h>
#include <stdio.h>
#include "list.h"

#ifdef DEBUG
#define PDEBUG(Args...) \
  do { fprintf(stderr, "mcwm: "); fprintf(stderr, ##Args); } while(0)
#define D(x) x
#else
#define PDEBUG(Args...)
#define D(x)
#endif

/*
 * Move element in item to the head of list mainlist.
 */
void movetohead(item_t **mainlist, item_t *item)
{
	if (NULL == item || NULL == mainlist || NULL == *mainlist) {
		return;
	}

	if (*mainlist == item) {
		/* item is NULL or we're already at head. Do nothing. */
		return;
	}

	/* Braid together the list where we are now. */
	if (item->prev) {
		item->prev->next = item->next;
	}

	if (item->next) {
		item->next->prev = item->prev;
	}

	/* Now we'at head, so no one before us. */
	item->prev = NULL;

	/* Old head is our next. */
	item->next = *mainlist;

	/* Old head needs to know about us. */
	item->next->prev = item;

	/* Remember the new head. */
	*mainlist = item;
}

/*
 * Create space for a new item and add it to the head of mainlist.
 *
 * Returns item or NULL if out of memory.
 */
item_t *additem(item_t **mainlist)
{
	item_t *item;

	if (NULL == (item = (item_t *) calloc(1, sizeof(item_t)))) {
		return NULL;
	}

	if (NULL == *mainlist) {
		/* First in the list. */
		item->prev = NULL;
		item->next = NULL;
	} else {
		/* Add to beginning of list. */
		item->next = *mainlist;
		item->next->prev = item;
		item->prev = NULL;
	}

	*mainlist = item;

	return item;
}

void delitem(item_t **mainlist, item_t *item)
{
	if (NULL == mainlist || NULL == *mainlist || NULL == item) {
		return;
	}
	item_t *ml = *mainlist;

	if (item == *mainlist) {
		/* First entry was removed. Remember the next one instead. */
		*mainlist = ml->next;
	} else {
		item->prev->next = item->next;

		if (item->next) {
			/* This is not the last item in the list. */
			item->next->prev = item->prev;
		}
	}

	free(item);
}

void freeitem(item_t **list, int *stored, item_t *item)
{
	if (NULL == list || NULL == *list || NULL == item) {
		return;
	}

	if (item->data) {
		free(item->data);
		item->data = NULL;
	}

	delitem(list, item);

	if (stored) {
		(*stored)--;
	}
}

/*
 * Delete all elements in list and free memory resources.
 */
void delallitems(item_t **list, int *stored)
{
	item_t *item;
	item_t *next;

	for (item = *list; item; item = next) {
		next = item->next;
		if (item->data) {
			free(item->data);
			item->data = NULL;
		}
		delitem(list, item);
	}

	if (stored) {
		(*stored) = 0;
	}
}

void listitems(item_t *mainlist)
{
	item_t *item;
	int i;

	for (item = mainlist, i = 1; item; item = item->next, i++) {
		printf("item #%d (stored at %p).\n", i, (void *) item);
	}
}

#if 0

void listall(item_t *mainlist)
{
	item_t *item;
	int i;

	printf("Listing all:\n");

	for (item = mainlist, i = 1; item; item = item->next, i++) {
		printf("%d at %p: %s.\n", i, (void *) item, (char *) item->data);
		printf("  prev: %p\n", item->prev);
		printf("  next: %p\n", item->next);
	}
}

int main(void)
{
	item_t *mainlist = NULL;
	item_t *item1;
	item_t *item2;
	item_t *item3;
	item_t *item4;
	item_t *item;
	item_t *nextitem;
	int i;
	char *foo1 = "1";
	char *foo2 = "2";
	char *foo3 = "3";
	char *foo4 = "4";

	item1 = additem(&mainlist);
	if (NULL == item1) {
		printf("Couldn't allocate.\n");
		exit(1);
	}
	item1->data = foo1;
	printf("Current elements:\n");
	listall(mainlist);

	item2 = additem(&mainlist);
	if (NULL == item2) {
		printf("Couldn't allocate.\n");
		exit(1);
	}
	item2->data = foo2;
	printf("Current elements:\n");
	listall(mainlist);

	item3 = additem(&mainlist);
	if (NULL == item3) {
		printf("Couldn't allocate.\n");
		exit(1);
	}
	item3->data = foo3;
	printf("Current elements:\n");
	listall(mainlist);

	item4 = additem(&mainlist);
	if (NULL == item4) {
		printf("Couldn't allocate.\n");
		exit(1);
	}
	item4->data = foo4;
	printf("Current elements:\n");
	listall(mainlist);

	printf
		("----------------------------------------------------------------------\n");

	printf("Moving item3 to be after item2\n");
	movetonext(&mainlist, item2, item3);
	printf("Current elements:\n");
	listall(mainlist);

	printf
		("----------------------------------------------------------------------\n");

	printf("Moving head! item4 to be after item2\n");
	movetonext(&mainlist, item2, item4);
	printf("Current elements:\n");
	listall(mainlist);

	printf
		("----------------------------------------------------------------------\n");

	printf("Moving tail! item1 to be after item2\n");
	movetonext(&mainlist, item2, item1);
	printf("Current elements:\n");
	listall(mainlist);

	printf
		("----------------------------------------------------------------------\n");

	printf("Moving head to be after tail.\n");
	movetonext(&mainlist, item3, item2);
	printf("Current elements:\n");
	listall(mainlist);

	printf("Moving all the items after each other.\n");
	/* item3 is tail. work backwards. */
	for (item = mainlist, i = 1; item; item = item->next, i++) {
		for (nextitem = item2; nextitem; nextitem = nextitem->prev) {
			movetonext(&mainlist, nextitem, item);
			printf("Current elements:\n");
			listall(mainlist);
		}
	}

	printf
		("----------------------------------------------------------------------\n");

#if 0
	movetohead(&mainlist, item2);
	printf("Current elements:\n");
	listall(mainlist);

	printf
		("----------------------------------------------------------------------\n");
#endif

	printf("Deleting item stored at %p\n", item3);
	delitem(&mainlist, item3);
	printf("Current elements:\n");
	listall(mainlist);

	puts("");

	delitem(&mainlist, item2);
	printf("Current elements:\n");
	listall(mainlist);

	puts("");

	delitem(&mainlist, item1);
	printf("Current elements:\n");
	listall(mainlist);

	puts("");

	printf
		("----------------------------------------------------------------------\n");

	exit(0);
}
#endif
