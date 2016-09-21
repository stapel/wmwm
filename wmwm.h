#ifndef __WMWM__WMWM_H__
#define __WMWM__WMWM_H__

#include <stdbool.h>        // for bool
#include <stdint.h>         // for uint16_t, int16_t, uint8_t
#include <xcb/randr.h>      // for xcb_randr_output_t
#include <xcb/xcb_icccm.h>  // for xcb_size_hints_t
#include <xcb/xproto.h>     // for xcb_drawable_t, xcb_rectangle_t, xcb_colo...
#include "list.h"           // for list_t
#include "tree.h"

/* Number of workspaces. */
#define WORKSPACES 10u

/* Types. */
/* All our key shortcuts. */
typedef enum {
	KEY_LEFT,
	KEY_DOWN,
	KEY_UP,
	KEY_RIGHT,
	KEY_VERTICAL,
	KEY_RAISE_LOWER,
	KEY_TERMINAL,
	KEY_MENU,
	KEY_MAXIMIZE,
	KEY_NEXT,
	KEY_WS1,
	KEY_WS2,
	KEY_WS3,
	KEY_WS4,
	KEY_WS5,
	KEY_WS6,
	KEY_WS7,
	KEY_WS8,
	KEY_WS9,
	KEY_WS10,
	KEY_TOPLEFT,
	KEY_TOPRIGHT,
	KEY_BOTTOMLEFT,
	KEY_BOTTOMRIGHT,
	KEY_KILL,
	KEY_PREVSCR,
	KEY_NEXTSCR,
	KEY_ICONIFY,
	KEY_MAX
} key_enum_t;

typedef struct monitor {
	xcb_randr_output_t id;

	char *name;

	int16_t x;					/* X and Y. */
	int16_t y;
	uint16_t width;				/* Width in pixels. */
	uint16_t height;			/* Height in pixels. */

	list_t *item;				/* Pointer to our place in output list. */
} monitor_t;

/* Everything we know about a window. */
typedef struct client {
	xcb_drawable_t id;				/* ID of this window. */
	xcb_drawable_t frame;			/* ID of parent frame window. */

	bool usercoord;					/* X,Y was set by -geom. */

	xcb_rectangle_t geometry;		/* current frame geometry */
	xcb_rectangle_t geometry_last;	/* geometry from before maximizing */

	xcb_size_hints_t hints;			/* WM_NORMAL_HINTS */

	xcb_colormap_t colormap;		/* current colormap */

	bool take_focus;				/* allow taking focus */
	bool allow_focus;				/* allow setting the input-focus to this window */
	bool use_delete;				/* use delete_window client message to kill a window */
	bool ewmh_state_set;			/* is _NET_WM_STATE set? */

	bool vertmaxed;					/* Vertically maximized, borders */
	bool fullscreen;				/* Fullscreen, i.e. without border */
	bool hidden;					/* Currently hidden */
	int killed;						/* number of times we sent delete_window message */

	bool ignore_unmap;				/* unmap_notification we shall ignore */

	monitor_t *monitor;				/* The physical output this window is on. */
	/* XXX tiling: set after create_client */

	/* XXX tiling: make this an array to have multitags */
	tree_t *wsitem;	/* Pointer to our place in workspaces
									   window tree. */
	uint32_t ws;

} client_t;

/* Window configuration data. */
typedef struct winconf {
	int16_t x;
	int16_t y;
	uint16_t width;
	uint16_t height;
	uint8_t stackmode;
	xcb_window_t sibling;
	uint16_t borderwidth;
} winconf_t;


/* New types for tiling-branch */
typedef struct workspaces {
	client_t *focuswin; /* last focused window */
	tree_t   *root;     /* root node for workdesk */
} workspace_t;

#endif /* __WMWM__WMWM_H__ */
