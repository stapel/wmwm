/*
 * mcwm, a small window manager for the X Window System using the X
 * protocol C Binding libraries.
 *
 * For 'user' configurable stuff, see config.h.
 *
 * MC, mc at the domain hack.org
 * http://hack.org/mc/
 *
 * Copyright (c) 2010, 2011, 2012 Michael Cardell Widerkrantz, mc at
 * the domain hack.org.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* XXX THINGS TODO XXX
 - sometimes windows get lost
 - LVDS is seen as clone of VGA-0? look at special-log
 - DestroyNotify after UnmapNotify produces wrong read? (see special log)
 - TODO Maybe it segfaults when an unmapped windows is being killed ?? TODO
 - Error handling
 - (windows in hashmap, dont fiddle with pointers)
 - key handling, automate a little further, it looks really ugly
# - there isnt allways a focus once I enter a virtual desktop (esp mplayer)
 -  we dont switch screens
 - maximize to v/h not fullscreen - only use fullscreen via hint from app
 - save rect for each v/h/fs maximizations and update
 - register events for client? (so keep client struct for window?)
 - now it stops working sometimes, no reaction in keys
   (with no windows available)
 - synthetic unmap notify handling
 - ctrl-meta-x maximize (v + h)
 - ctrl-meta-z fullscreen
 - initial atoms (esp. _NET_WM_STATE_FULLSCREEN etc.)
 - maximize and fitonscreen share similar code
 - unmaxed size is not allways fitting
 - resizing in constriants!
 - put resizing fixups (local vars) into a seperate function
 - !BAD see 5755
 - hide() is now used generally, remove allow_icons stuff



 XXX
 - all clients have the same border color

 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb_event.h>

#include <xcb/xinput.h>
#include <xcb/xcb_keysyms.h>

#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_ewmh.h>

#include <X11/keysym.h>

#include "list.h"

/* Check here for user configurable parts: */
#include "config.h"

#ifdef DEBUG
#define PDEBUG(Args...) \
	do { fprintf(stderr, "mcwm: "); fprintf(stderr, ##Args); } while(0)
#define D(x) x
#else
#define PDEBUG(Args...)
#define D(x)
#endif

#define destroy(x) do { free(x); x = NULL; } while (0)


/* Internal Constants. */


typedef enum {
	mode_nothing, /* We're currently doing nothing special */
	mode_move,    /* We're currently moving a window with the mouse. */
	mode_resize,  /* We're currently resizing a window with the mouse. */
	mode_tab      /* We're currently tabbing around the window list,
				looking for a new window to focus on. */
} wm_mode_t;

/* Number of workspaces. */
#define WORKSPACES 10u

/* Value in WM hint which means this window is fixed on all workspaces. */
#define NET_WM_FIXED 0xffffffff

/* This means we didn't get any window hint at all. */
#define MCWM_NOWS 0xfffffffe



/* Default Client Events
 *
 * Only listen for property_notify on the client,
 * the other events come in via substructure-requests/notifications via
 * frame window
 * */
#define DEFAULT_WINDOW_EVENTS XCB_EVENT_MASK_PROPERTY_CHANGE


/* Frame events
 *
 * Enter events
 * Substructure notify (unmap/destroy notify etc.)
 * Substructure Redirect (configure requests etc.)
*/
#define DEFAULT_FRAME_EVENTS (XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY)

/* Frame events for hidden frames
 *
 * only listen for destroy notifcations and the like
 * (have to substructure redirect because it's mine mwhahaaa)
 */
#define HIDDEN_FRAME_EVENTS (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY)

/* What we listen to on the root window */
#define DEFAULT_ROOT_WINDOW_EVENTS (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY)

//XCB_EVENT_MASK_PROPERTY_CHANGE
// - Substructure redirect (map request -> new win)



static inline bool is_null(void *ptr)
{
	return (ptr == (void*)NULL);
}


/* Types. */

/* All our key shortcuts. */
typedef enum {
	KEY_F,
	KEY_H,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_V,
	KEY_R,
	KEY_RET,
	KEY_M,
	KEY_X,
	KEY_TAB,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,
	KEY_Y,
	KEY_U,
	KEY_B,
	KEY_N,
	KEY_END,
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

	item_t *item;			/* Pointer to our place in output list. */
} monitor_t;

/* Everything we know about a window. */
// XXX original border size
typedef struct client {
	xcb_drawable_t id;			/* ID of this window. */
	xcb_drawable_t frame;		/* ID of parent frame window. */

	bool usercoord;				/* X,Y was set by -geom. */

	int16_t x;					/* X coordinate. */
	int16_t y;					/* Y coordinate. */
	uint16_t width;				/* Width in pixels. */
	uint16_t height;			/* Height in pixels. */

	xcb_rectangle_t origsize;	/* Original size if we're currently maxed. */

	uint16_t min_width, min_height;	/* Hints from application. */
	uint16_t max_width, max_height;
	int32_t width_inc, height_inc;
	int32_t base_width, base_height;
	uint16_t aspect_num, aspect_den;

	/* Those will be updated on property-notify */
	bool allow_focus;			/* */
	bool use_delete;
	bool ewmh_state_set;

	bool vertmaxed;				/* Vertically maximized? XXX not fullscreen at all */
	bool maxed;					/* Totally maximized? XXX aka fullscreen*/
	bool fixed;					/* Visible on all workspaces? */

	int killed;

	int ignore_unmap;

	monitor_t *monitor;	/* The physical output this window is on. */
	item_t *winitem;		/* Pointer to our place in global windows list. */
	item_t *wsitem[WORKSPACES];	/* Pointer to our place in every
										 * workspace window list. */
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



/* Globals */

int sigcode;					/* Signal code. Non-zero if we've been
								 * interruped by a signal. */
xcb_connection_t *conn;			/* Connection to X server. */
xcb_screen_t *screen;			/* Our current screen.  */
int screen_number;

xcb_timestamp_t	current_time;	/* latest timestamp XXX */

xcb_ewmh_connection_t *ewmh;		/* EWMH Connection */

//xcb_atom_t ewmh_1_4_NET_WM_STATE_FOCUSED;

int randrbase;					/* Beginning of RANDR extension events. */
int shapebase;					/* Beginning of SHAPE extension events. */
const unsigned workspaces = WORKSPACES;

uint32_t curws = 0;				/* Current workspace. */



uint16_t mode_x = 0;
uint16_t mode_y = 0;


client_t *focuswin = NULL;		/* Current focus window. */
client_t *lastfocuswin = NULL;	/* Last focused window. NOTE! Only
								 * used to communicate between
								 * start and end of tabbing
								 * mode. */

item_t *winlist = NULL;			/* Global list of all client windows. */
item_t *monlist = NULL;			/* List of all physical monitor outputs. */

wm_mode_t MCWM_mode = mode_nothing;		/* Internal mode, such as move or resize */

/*
 * Workspace list: Every workspace has a list of all visible
 * windows.
 */
item_t *wslist[WORKSPACES] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};


/* Shortcut key type and initializiation. */
struct keys {
	xcb_keysym_t keysym;
	xcb_keycode_t keycode;
} keys[KEY_MAX] = {
	{ USERKEY_FIX, 0},
	{ USERKEY_MOVE_LEFT, 0},
	{ USERKEY_MOVE_DOWN, 0},
	{ USERKEY_MOVE_UP, 0},
	{ USERKEY_MOVE_RIGHT, 0},
	{ USERKEY_MAXVERT, 0},
	{ USERKEY_RAISE, 0},
	{ USERKEY_TERMINAL, 0},
	{ USERKEY_MENU, 0},
	{ USERKEY_MAX, 0},
	{ USERKEY_CHANGE, 0},
	{ USERKEY_WS1, 0},
	{ USERKEY_WS2, 0},
	{ USERKEY_WS3, 0},
	{ USERKEY_WS4, 0},
	{ USERKEY_WS5, 0},
	{ USERKEY_WS6, 0},
	{ USERKEY_WS7, 0},
	{ USERKEY_WS8, 0},
	{ USERKEY_WS9, 0},
	{ USERKEY_WS10, 0},
	{ USERKEY_TOPLEFT, 0},
	{ USERKEY_TOPRIGHT, 0},
	{ USERKEY_BOTLEFT, 0},
	{ USERKEY_BOTRIGHT, 0},
	{ USERKEY_DELETE, 0},
	{ USERKEY_PREVSCREEN, 0},
	{ USERKEY_NEXTSCREEN, 0},
	{ USERKEY_ICONIFY, 0},
};
/* All keycodes generating our MODKEY mask. */
struct modkeycodes {
	xcb_keycode_t *keycodes;
	unsigned len;
} modkeys = { NULL, 0};


/* Global configuration. */
struct conf {
	int borderwidth;			/* Do we draw borders? If so, how large? */
	char *terminal;				/* Path to terminal to start. */
	char *menu;					/* Path to menu to start. */
	uint32_t focuscol;			/* Focused border colour. */
	uint32_t unfocuscol;		/* Unfocused border colour.  */
	uint32_t fixedcol;			/* Fixed windows border colour. */
	bool allowicons;			/* Allow windows to be unmapped. */
} conf;

/* elemental atoms not in ewmh */
// JUST USE XCB_WM_NAME_ etc pp?
struct icccm {
	xcb_atom_t wm_delete_window;	/* WM_DELETE_WINDOW event to close windows.  */
	xcb_atom_t wm_change_state;
	xcb_atom_t wm_state;
	xcb_atom_t wm_protocols;		/* WM_PROTOCOLS.  */
} icccm;

static xcb_atom_t ewmh_allowed_actions[2] = { XCB_ATOM_NONE, XCB_ATOM_NONE };

/* Functions declerations. */

/* print out X error to stderr */
static void print_x_error(xcb_generic_error_t *e);

/* Handlers */
static void handle_error_event(xcb_generic_event_t*);
static void handle_map_request(xcb_generic_event_t*);
static void handle_button_press(xcb_generic_event_t*);
static void handle_motion_notify(xcb_generic_event_t*);
static void handle_button_release(xcb_generic_event_t*);
static void handle_key_press(xcb_generic_event_t*);
static void handle_key_release(xcb_generic_event_t*);
static void handle_enter_notify(xcb_generic_event_t*);
static void handle_configure_notify(xcb_generic_event_t*);
static void handle_configure_request(xcb_generic_event_t*);
static void handle_client_message(xcb_generic_event_t*);
static void handle_circulate_request(xcb_generic_event_t*);
static void handle_mapping_notify(xcb_generic_event_t*);
static void handle_unmap_notify(xcb_generic_event_t*);
static void handle_destroy_notify(xcb_generic_event_t*);
static void handle_property_notify(xcb_generic_event_t*);
#if 0
static void handle_focus_in(xcb_generic_event_t*);
#endif

// RESPONSE_TYPE_MASK is uint_8t (and is only 0x1f, so little waste)
static void (*handler[XCB_EVENT_RESPONSE_TYPE_MASK]) (xcb_generic_event_t*) = {
	// 0 is error, maybe I should just use 0 instead of define XCB_NONE
	[0]						= handle_error_event,
	[XCB_MAP_REQUEST]		= handle_map_request,
	[XCB_BUTTON_PRESS]		= handle_button_press,
	[XCB_MOTION_NOTIFY]		= handle_motion_notify,
	[XCB_BUTTON_RELEASE]	= handle_button_release,
	[XCB_KEY_PRESS]			= handle_key_press,
	[XCB_KEY_RELEASE]		= handle_key_release,
	[XCB_ENTER_NOTIFY]		= handle_enter_notify,
	[XCB_CONFIGURE_NOTIFY]	= handle_configure_notify,
	[XCB_CONFIGURE_REQUEST] = handle_configure_request,
	[XCB_CLIENT_MESSAGE]	= handle_client_message,
	[XCB_CIRCULATE_REQUEST]	= handle_circulate_request,
	[XCB_MAPPING_NOTIFY]	= handle_mapping_notify,
	[XCB_UNMAP_NOTIFY]		= handle_unmap_notify,
	[XCB_DESTROY_NOTIFY]	= handle_destroy_notify,
#if 0
	[XCB_FOCUS_IN]			= handle_focus_in,
#endif
	[XCB_PROPERTY_NOTIFY]	= handle_property_notify
};
static void finishtabbing(void);
static struct modkeycodes getmodkeys(xcb_mod_mask_t modmask);
static void cleanup(int code);
static void arrangewindows(void);


static bool ewmh_is_fullscreen(client_t*);

static void ewmh_set_workspace(xcb_drawable_t win, uint32_t ws);
static int32_t ewmh_get_workspace(xcb_drawable_t win);
static void ewmh_update_client_list();

static void addtoworkspace(client_t *client, uint32_t ws);
static void delfromworkspace(client_t *client, uint32_t ws);
static void changeworkspace(uint32_t ws);
static void fixwindow(client_t *client, bool setcolour);
static uint32_t getcolor(const char *colstr);
static void remove_client(client_t *client);
//static void forgetwin(xcb_window_t win);
static void fitonscreen(client_t *client);
static void new_win(xcb_window_t win);
static client_t *setup_win(xcb_window_t win);
static void set_shape(client_t* client);
static void set_frame_extents(xcb_window_t win, int width);


static xcb_keycode_t keysymtokeycode(xcb_keysym_t keysym,
									 xcb_key_symbols_t * keysyms);

static bool setup_keys(void);
static bool setup_screen(void);
static bool setup_icccm(void);
static bool setup_ewmh(void);
static int setup_randr(void);
static void getrandr(void);
static void getoutputs(xcb_randr_output_t * outputs, int len,
					   xcb_timestamp_t timestamp);
void arrbymon(monitor_t *monitor);
static monitor_t *findmonitor(xcb_randr_output_t id);
static monitor_t *findclones(xcb_randr_output_t id, int16_t x, int16_t y);
static monitor_t *findmonbycoord(int16_t x, int16_t y);
static void delmonitor(monitor_t *mon);
static monitor_t *addmonitor(xcb_randr_output_t id, char *name,
								  uint32_t x, uint32_t y, uint16_t width,
								  uint16_t height);

static void raisewindow(xcb_drawable_t win);
static void raiseorlower(client_t *client);
static void movelim(client_t *client);
static void movewindow(xcb_drawable_t win, uint16_t x, uint16_t y);
static client_t *findclient(xcb_drawable_t win);
static client_t *findclientp(xcb_drawable_t win);
static void focusnext(void);
static void setunfocus();
static void setfocus(client_t *client);
/* XXX */
static void set_input_focus(xcb_window_t win);
static int start(char *program);
static void resizelim(client_t *client);
static void moveresize(xcb_drawable_t win, uint16_t x, uint16_t y,
					   uint16_t width, uint16_t height);
static void resize(xcb_drawable_t win, uint16_t width, uint16_t height);
static void resizestep(client_t *client, char direction);
static void mousemove(client_t *client, int rel_x, int rel_y);
static void mouseresize(client_t *client, int rel_x, int rel_y);
static void movestep(client_t *client, char direction);
static void setborders(xcb_drawable_t win, int width);
static void unmax(client_t *client);
static void maximize(client_t *client);
static void maxvert(client_t *client);
static void reparent(client_t *client);
static void hide(client_t *client);
static void show(client_t *client);
static void deletewin(client_t*);


/* horrible naming TMP/TODO */
static void hidden_event_mask(client_t *client);
static void default_event_mask(client_t *client);

static bool getpointer(xcb_drawable_t win, int16_t * x, int16_t * y);
static bool getgeom(xcb_drawable_t win, int16_t * x, int16_t * y,
					uint16_t * width, uint16_t * height);
static void topleft(void);
static void topright(void);
static void botleft(void);
static void botright(void);
static void prevscreen(void);
static void nextscreen(void);
static void configwin(xcb_window_t win, uint16_t old_mask, winconf_t wc);
static void events(void);
static void printhelp(void);
static void sigcatch(int sig);
static xcb_atom_t get_atom(char *atom_name);

#if DEBUG
static char* get_atomname(xcb_atom_t atom);
#endif


static void get_mondim(monitor_t* monitor, xcb_rectangle_t* sp);

#if 0
static int get_wm_name(xcb_window_t, char**, int*);
static int get_wm_name_icccm(xcb_window_t, char**, int*);
static int get_wm_name_ewmh(xcb_window_t, char**, int*);
#endif

/* Function bodies. */


// XXX this is just a little precaution and encapsulation
static void			set_mode(wm_mode_t modus)	{ MCWM_mode = modus; }
static wm_mode_t	get_mode(void)				{ return MCWM_mode; }
static bool			is_mode(wm_mode_t modus)	{ return (get_mode() == modus); }

static xcb_timestamp_t get_timestamp() { return current_time; }
static void set_timestamp(xcb_timestamp_t t) { current_time = t; }
static void update_timestamp(xcb_timestamp_t t) { if (t != XCB_TIME_CURRENT_TIME) current_time = t; }



/*
 * Update client's window's atoms
 */
static void ewmh_update_state(client_t* client)
{
	xcb_atom_t atoms[4];
	uint32_t i = 0;

	if (! client)
		return;

	if (client->maxed)
		atoms[i++] = ewmh->_NET_WM_STATE_FULLSCREEN;
	if (client->vertmaxed)
		atoms[i++] = ewmh->_NET_WM_STATE_MAXIMIZED_VERT;
	if (client->fixed)
		atoms[i++] = ewmh->_NET_WM_STATE_STICKY;
//	if (client == focuswin)
//		atoms[i++] = ewmh_1_4_NET_WM_STATE_FOCUSED;

	if (i > 0) {
		xcb_ewmh_set_wm_state(ewmh, client->id, i, atoms);
		client->ewmh_state_set = true;
	} else if (client->ewmh_state_set) {
		xcb_delete_property(conn, client->id, ewmh->_NET_WM_STATE);
		client->ewmh_state_set = false;
	}
}

/*
 * MODKEY was released after tabbing around the
 * workspace window ring. This means this mode is
 * finished and we have found a new focus window.
 *
 * We need to move first the window we used to focus
 * on to the head of the window list and then move the
 * new focus to the head of the list as well. The list
 * should always start with the window we're focusing
 * on.
 */
/* XXX THIS DOES NOT REALLY WORK ALLWAYS FIX, this is really bad */
void finishtabbing(void)
{
	PDEBUG("Finish tabbing!\n");
	set_mode(mode_nothing);

	if (!is_null(lastfocuswin) && !is_null(focuswin)) {
		movetohead(&wslist[curws], lastfocuswin->wsitem[curws]);
		lastfocuswin = NULL;
	}
}

/*
 * Find out what keycode modmask is bound to. Returns a struct. If the
 * len in the struct is 0 something went wrong.
 */
struct modkeycodes getmodkeys(xcb_mod_mask_t modmask)
{
	xcb_get_modifier_mapping_cookie_t cookie;
	xcb_get_modifier_mapping_reply_t *reply;
	xcb_keycode_t *modmap;
	struct modkeycodes keycodes = {
		NULL,
		0
	};
	int mask;
	unsigned i;
	const xcb_mod_mask_t masks[8] = { XCB_MOD_MASK_SHIFT,
		XCB_MOD_MASK_LOCK,
		XCB_MOD_MASK_CONTROL,
		XCB_MOD_MASK_1,
		XCB_MOD_MASK_2,
		XCB_MOD_MASK_3,
		XCB_MOD_MASK_4,
		XCB_MOD_MASK_5
	};

	cookie = xcb_get_modifier_mapping_unchecked(conn);

	reply = xcb_get_modifier_mapping_reply(conn, cookie, NULL);
	if (is_null(reply)) {
		return keycodes;
	}

	keycodes.keycodes = calloc(reply->keycodes_per_modifier,
			sizeof(xcb_keycode_t));

	if (is_null(keycodes.keycodes)) {
		fprintf(stderr, "Out of memory.\n");
		destroy(reply);
		return keycodes;
	}

	modmap = xcb_get_modifier_mapping_keycodes(reply);

	/*
	 * The modmap now contains keycodes.
	 *
	 * The number of keycodes in the list is 8 *
	 * keycodes_per_modifier. The keycodes are divided into eight
	 * sets, with each set containing keycodes_per_modifier elements.
	 *
	 * Each set corresponds to a modifier in masks[] in the order
	 * specified above.
	 *
	 * The keycodes_per_modifier value is chosen arbitrarily by the
	 * server. Zeroes are used to fill in unused elements within each
	 * set.
	 */
	for (mask = 0; mask < 8; mask++) {
		if (masks[mask] == modmask) {
			for (i = 0; i < reply->keycodes_per_modifier; i++) {
				if (0 != modmap[mask * reply->keycodes_per_modifier + i]) {
					keycodes.keycodes[i]
						= modmap[mask * reply->keycodes_per_modifier + i];
					keycodes.len++;
				}
			}
			PDEBUG("Got %d keycodes.\n", keycodes.len);
		}
	}							/* for mask */

	destroy(reply);

	return keycodes;
}

/*
 * Set keyboard focus to follow mouse pointer. Then exit.
 *
 * We don't need to bother mapping all windows we know about. They
 * should all be in the X server's Save Set and should be mapped
 * automagically.
 */
void cleanup(int code)
{
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, get_timestamp());
	if (ewmh) {
		/* TODO * delete atoms */
		xcb_ewmh_connection_wipe(ewmh);
	}
	xcb_disconnect(conn);
	exit(code);
}

/*
 * Rearrange windows to fit new screen size.
 */
void arrangewindows(void)
{
	item_t *item;
	client_t *client;

	/*
	 * Go through all windows. If they don't fit on the new screen,
	 * move them around and resize them as necessary.
	 */
	for (item = winlist; item; item = item->next) {
		client = item->data;
		fitonscreen(client);
	}
}
/*
 * set _NET_CLIENT_LIST
 *
 * in order of first mapping
 * (btw. we don't have stacking information of all windows
 * so we don't set _NET_CLIENT_LIST_STACKING
 */
void ewmh_update_client_list()
{
	item_t *item;
	client_t *client;
	xcb_window_t *window_list;

	uint32_t windows = 0;

	/* count windows */
	for (item = winlist; item; item = item->next, windows++);

	/* leave if no windows */
	if (windows == 0) {
		xcb_ewmh_set_client_list(ewmh, screen_number, 0, NULL);
		return;
	}

	/* create window array */
	window_list = calloc(windows, sizeof(xcb_window_t));
	if (is_null(window_list)) {
		xcb_ewmh_set_client_list(ewmh, screen_number, 0, NULL);
		return;
	}

	/* fill window array */
	int id = windows;
	for (item = winlist; item; item = item->next) {
		client = item->data;
		window_list[--id] = client->id;
	}
	xcb_ewmh_set_client_list(ewmh, screen_number, windows, window_list);
	free(window_list);
}

/* Set the EWMH hint that window win belongs on workspace ws. */
void ewmh_set_workspace(xcb_drawable_t win, uint32_t ws)
{
	PDEBUG("Changing _NET_WM_DESKTOP on window 0x%x to %d\n", win, ws);
	xcb_ewmh_set_wm_desktop(ewmh, win, ws);
}

/*
 * Check if window has _NET_WM_STATE_FULLSCREEN atom
 */
bool ewmh_is_fullscreen(client_t* client)
{
	if (is_null(client))
		return false;

	xcb_ewmh_get_atoms_reply_t atoms;

	if (0 == xcb_ewmh_get_wm_state_reply(ewmh,
			xcb_ewmh_get_wm_state_unchecked(ewmh, client->id),
			&atoms, NULL)) {
		return false;
	}

	bool ret = false;
	for (uint32_t i = 0; i < atoms.atoms_len; i++) {
		if (atoms.atoms[i] == ewmh->_NET_WM_STATE_FULLSCREEN) {
			ret = true;
			break;
		}
	}

	xcb_ewmh_get_atoms_reply_wipe(&atoms);
	return ret;

}

/*
 * Get EWWM hint so we might know what workspace window win should be
 * visible on.
 *
 * Returns either workspace, NET_WM_FIXED if this window should be
 * visible on all workspaces or MCWM_NOWS if we didn't find any hints.
 */
int32_t ewmh_get_workspace(xcb_drawable_t win)
{
	xcb_get_property_cookie_t cookie;
	uint32_t ws;

	cookie = xcb_ewmh_get_wm_desktop_unchecked(ewmh, win);

	if (! xcb_ewmh_get_wm_desktop_reply(ewmh, cookie, &ws, NULL)) {
		fprintf(stderr, "mcwm: Couldn't get properties for win %d\n", win);
		return MCWM_NOWS;
	}
	return ws;
}

/* Add a window, specified by client, to workspace ws. */
void addtoworkspace(client_t *client, uint32_t ws)
{
	item_t *item;

	if ((item = additem(&wslist[ws])) == NULL) {
		PDEBUG("addtoworkspace: Out of memory.\n");
		return;
	}

	/* Remember our place in the workspace window list. */
	client->wsitem[ws] = item;

	/* Remember the data. */
	item->data = client;

	/*
	 * Set window hint property so we can survive a crash.
	 *
	 * Fixed windows have their own special WM hint. We don't want to
	 * mess with that.
	 */
	if (! client->fixed) {
		ewmh_set_workspace(client->id, ws);
	}
}

/* Delete window client from workspace ws. */
void delfromworkspace(client_t *client, uint32_t ws)
{
	delitem(&wslist[ws], client->wsitem[ws]);

	/* Reset our place in the workspace window list. */
	client->wsitem[ws] = NULL;
}

/* Change current workspace to ws. */
void changeworkspace(uint32_t ws)
{
	item_t *item;
	client_t *client;

	if (ws == curws) {
		return;
	}

	PDEBUG("Changing from workspace #%d to #%d\n", curws, ws);

	/*
	 * We lose our focus if the window we focus isn't fixed. An
	 * EnterNotify event will set focus later.
	 */
	if (focuswin && !focuswin->fixed) {
		setunfocus();
		focuswin = NULL;
	}

	/* Apply hidden window event mask, this ensures no invalid enter events */
	for (item = wslist[curws]; item; item = item->next) {
		client = item->data;
		if (! client->fixed) {
			hidden_event_mask(client);
		}
	}
	/* Go through list of current ws. Unmap everything that isn't fixed. */
	for (item = wslist[curws]; item; item = item->next) {
		client = item->data;

		if (! client->fixed) {
			/*
			 * This is an ordinary window. Just unmap it. Note that
			 * this will generate an unnecessary UnmapNotify event
			 * which we will try to handle later.
			 */
			hide(client);
		}
	}

	client = NULL;
	/* Go through list of new ws. Map everything that isn't fixed. */
	for (item = wslist[ws]; item; item = item->next) {
		client = item->data;

		PDEBUG("changeworkspace. map phase. ws #%d, client-fixed: %d\n",
				ws, client->fixed);

		/* Fixed windows are already mapped. Map everything else. */
		if (! client->fixed) {
			show(client);
		}
	}

	/* reenable enter events */
	for (item = wslist[ws]; item; item = item->next) {
		client = item->data;
		if (! client->fixed) {
			default_event_mask(client);
		}
	}
	xcb_flush(conn);
	set_input_focus(XCB_WINDOW_NONE);

	xcb_ewmh_set_current_desktop(ewmh, screen_number, ws);
	curws = ws;
}

/*
 * Fix or unfix a window client from all workspaces. If setcolour is
 * set, also change back to ordinary focus colour when unfixing.
 */
void fixwindow(client_t *client, bool setcolour)
{
	if (is_null(client)) {
		return;
	}

	if (client->fixed) {
		client->fixed = false;
		ewmh_set_workspace(client->id, curws);

		if (setcolour) {
			/* Set border color to ordinary focus colour. */
			uint32_t values[1] = { conf.focuscol };
			xcb_change_window_attributes(conn, client->frame,
					XCB_CW_BORDER_PIXEL, values);
		}

		/* Delete from all workspace lists except current. */
		for (uint32_t ws = 0; ws < WORKSPACES; ws++) {
			if (ws != curws) {
				delfromworkspace(client, ws);
			}
		}
	} else {
		/*
		 * First raise the window. If we're going to another desktop
		 * we don't want this fixed window to be occluded behind
		 * something else.
		 */
		raisewindow(client->frame);

		client->fixed = true;
		ewmh_set_workspace(client->id, NET_WM_FIXED);

		/* Add window to all workspace lists. */
		for (uint32_t ws = 0; ws < WORKSPACES; ws++) {
			if (ws != curws) {
				addtoworkspace(client, ws);
			}
		}

		if (setcolour) {
			/* Set border color to fixed colour. */
			uint32_t values[1] = { conf.fixedcol };
			xcb_change_window_attributes(conn, client->frame,
					XCB_CW_BORDER_PIXEL, values);
		}
	}

	ewmh_update_state(client);
}

/*
 * Get the pixel values of a named colour colstr.
 *
 * Returns pixel values.
 */
uint32_t getcolor(const char *colstr)
{
	xcb_alloc_named_color_reply_t *col_reply;
	xcb_colormap_t colormap;
	xcb_generic_error_t *error;
	xcb_alloc_named_color_cookie_t colcookie;

	uint32_t color;

	if (! colstr)
		return 0;

	if (strlen(colstr) > 1 && *colstr == '#') {
		return (uint32_t)strtoul((colstr + 1), NULL, 16);
	}
	colormap = screen->default_colormap;
	colcookie = xcb_alloc_named_color(conn, colormap, strlen(colstr),
			colstr);
	col_reply = xcb_alloc_named_color_reply(conn, colcookie, &error);

	if (error || col_reply == NULL) {
		fprintf(stderr, "mcwm: Couldn't get pixel value for colour %s. Exiting.\n",
				colstr);
		print_x_error(error);
		destroy(error);
		cleanup(1);
	}
	color = col_reply->pixel;
	destroy(col_reply);
	return color;
}


/* Reparent client window to root and set state ti WITHDRAWN */
void withdraw_client(client_t* client)
{
	long data[] = { XCB_ICCCM_WM_STATE_WITHDRAWN, XCB_NONE };
	xcb_generic_error_t *error;
	xcb_void_cookie_t vc;

/*  Remove it as well
	mask = XCB_CW_EVENT_MASK;
	values[0] = 0;
	xcb_change_window_attributes(conn, client->id, mask, values);
*/

	PDEBUG("Reparenting 0x%x to 0x%x\n", client->id, screen->root);
	vc = xcb_reparent_window_checked(conn, client->id, screen->root, 0, 0);
	error = xcb_request_check(conn, vc);
	if (! error) {
		PDEBUG(" and set window to withdrawn state");
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->id,
			icccm.wm_state, icccm.wm_state, 32, 2, data);
	} else {
		PDEBUG("Could not reparent 0x%x back to root\n", client->id);
		print_x_error(error);
		destroy(error);
	}
	/* XXX check out the error-code */
	xcb_destroy_window(conn, client->frame);
}

/* Forget everything about client client. */
void remove_client(client_t *client)
{
	if (is_null(client)) {
		PDEBUG("remove_client: client was NULL\n");
		return;
	}

	PDEBUG("remove_client: forgeting about win 0x%x\n", client->id);

	if (focuswin == client) focuswin = NULL;
	if (lastfocuswin == client) lastfocuswin = NULL;

	/*
	 * Delete this client from whatever workspace lists it belongs to.
	 * Note that it's OK to be on several workspaces at once even if
	 * you're not fixed.
	 */
	for (uint32_t ws = 0; ws < WORKSPACES; ws++) {
		if (client->wsitem[ws]) {
			delfromworkspace(client, ws);
		}
	}

	/* Remove from global window list. */
	freeitem(&winlist, NULL, client->winitem);
	ewmh_update_client_list();
}

/*
 * Fit client on physical screen, moving and resizing as necessary.
 */
void fitonscreen(client_t *client)
{
	bool willmove = false;
	bool willresize = false;

	xcb_rectangle_t mon;

	client->vertmaxed = false;

	get_mondim(client->monitor, &mon);

/*	PDEBUG("Window at %d, %d has dimensions %d x %d.\n", client->x, client->y,
			client->width, client->height);
	if (client->width == mon.width && client->height == mon.height) {
		client->maxed = true;
	}
*/
	// XXX this is for remaximization after screen switch
	// this is hacky!
	//
	if (ewmh_is_fullscreen(client)) {
		PDEBUG(
			"window 0x%x allready has _NET_WM_STATE_FULLSCREEN on startup!\n",
			client->id);
		if (! client->maxed)
			maximize(client);
		goto endfit;
//		client->maxed = true;
	} else if (client->maxed) {
		client->x = mon.x;
		client->y = mon.y;
		client->width = mon.width;
		client->height = mon.height;
		willmove = willresize = true;
		setborders(client->frame, 0);
		set_frame_extents(client->id, 0);
		goto endfit;

//		client->maxed = false;
	}

	PDEBUG("Is window outside monitor?\n");
	PDEBUG("x: %d between %d and %d?\n", client->x, mon.x, mon.x + mon.width);
	PDEBUG("y: %d between %d and %d?\n", client->y, mon.y, mon.y + mon.height);

	/* Is it outside the physical monitor? */
	if (client->x > mon.x + mon.width) {
		client->x = mon.x + mon.width - client->width;
		willmove = true;
	}
	if (client->y > mon.y + mon.height) {
		client->y = mon.y + mon.height - client->height;
		willmove = true;
	}

	if (client->x < mon.x) {
		client->x = mon.x;
		willmove = true;
	}
	if (client->y < mon.y) {
		client->y = mon.y;
		willmove = true;
	}

	/* Is it smaller than it wants to  be? */
	if (0 != client->min_height && client->height < client->min_height) {
		client->height = client->min_height;
		willresize = true;
	}

	if (0 != client->min_width && client->width < client->min_width) {
		client->width = client->min_width;
		willresize = true;
	}

	/*
	 * If the window is larger than our screen, just place it in the
	 * corner and resize.
	 */
	if (client->width + conf.borderwidth * 2 > mon.width) {
		client->x = mon.x;
		client->width = mon.width - conf.borderwidth * 2;;
		willmove = true;
		willresize = true;
	} else if (client->x + client->width + conf.borderwidth * 2
			> mon.x + mon.width) {
		client->x = mon.x + mon.width - (client->width + conf.borderwidth * 2);
		willmove = true;
	}

	if (client->height + conf.borderwidth * 2 > mon.height) {
		client->y = mon.y;
		client->height = mon.height - conf.borderwidth * 2;
		willmove = true;
		willresize = true;
	} else if (client->y + client->height + conf.borderwidth * 2
			> mon.y + mon.height) {
		client->y = mon.y + mon.height - (client->height + conf.borderwidth
				* 2);
		willmove = true;
	}
endfit:

	if (willmove && willresize) {
		moveresize(client->frame, client->x, client->y,
				client->width, client->height);
		resize(client->id, client->width, client->height);
	} else {
		if (willmove) {
			movewindow(client->frame, client->x, client->y);
		}

		if (willresize) {
			resize(client->frame, client->width, client->height);
			resize(client->id, client->width, client->height);
		}
	}
}

/*
 * Set position, geometry and attributes of a new window and show it
 * on the screen.
 */
void new_win(xcb_window_t win)
{
	if (findclient(win)) {
		/*
		 * We know this window from before. It's trying to map itself
		 * on the current workspace, but since it's unmapped it
		 * probably belongs on another workspace. We don't like that.
		 * Silently ignore.
		 */
		return;
	}

	/*
	 * Set up stuff, like borders, add the window to the client list,
	 * et cetera.
	 */
	client_t* client = setup_win(win);
	if (is_null(client)) {
		fprintf(stderr, "mcwm: Couldn't set up window. Out of memory.\n");
		return;
	}

	/* Add this window to the current workspace. */
	addtoworkspace(client, curws);

	/*
	 * If the client doesn't say the user specified the coordinates
	 * for the window we map it where our pointer is instead.
	 */
	if (! client->usercoord) {
		int16_t pointx;
		int16_t pointy;

		/* Get pointer position so we can move the window to the cursor. */
		if (! getpointer(screen->root, &pointx, &pointy)) {
			PDEBUG("Failed to get pointer coords! \n");
			pointx = 0;
			pointy = 0;
		}

		PDEBUG("Coordinates not set by user. Using pointer: %d,%d.\n", pointx, pointy);

		client->x = pointx;
		client->y = pointy;

		movewindow(client->frame, client->x, client->y);
	} else {
		PDEBUG("User set coordinates.\n");
	}

	/* Find the physical output this window will be on if RANDR is active. */
	if (-1 != randrbase) {
		client->monitor = findmonbycoord(client->x, client->y);
		if (is_null(client->monitor)) {
			/*
			 * Window coordinates are outside all physical monitors.
			 * Choose the first screen.
			 */
			if (monlist) {
				client->monitor = monlist->data;
			}
		}
	}


	fitonscreen(client);

	/* Show window on screen. */
	default_event_mask(client);
	show(client);

	/*
	 * Move cursor into the middle of the window so we don't lose the
	 * pointer to another window.
	 */
	xcb_warp_pointer(conn, XCB_WINDOW_NONE, win, 0, 0, 0, 0,
			client->width / 2, client->height / 2);

}


/*
 * Update local WM_NORMAL_HINTS information
 */
void icccm_update_wm_normal_hints(client_t* client)
{
	if (! client)
		return;

	xcb_size_hints_t hints;

	if (! xcb_icccm_get_wm_normal_hints_reply
			(conn, xcb_icccm_get_wm_normal_hints_unchecked(conn, client->id),
			 &hints, NULL)) {
		PDEBUG("Couldn't get size hints.\n");
		return;
	}

	/*
	 * The user specified the position coordinates. Remember that so
	 * we can use geometry later.
	 */
	if (hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) {
		client->usercoord = true;
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		client->min_width = hints.min_width;
		client->min_height = hints.min_height;
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		client->max_width = hints.max_width;
		client->max_height = hints.max_height;
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
		client->width_inc = hints.width_inc;
		client->height_inc = hints.height_inc;
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
		if ((hints.min_aspect_num == hints.max_aspect_num) &&
				(hints.min_aspect_den == hints.max_aspect_den)) {
			client->aspect_num = hints.min_aspect_num;
			client->aspect_den = hints.min_aspect_den;
		}
	}

	if (hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		client->base_width = hints.base_width;
		client->base_height = hints.base_height;
	}
}

/*
 * Update local WM_HINTS information
 */
void icccm_update_wm_hints(client_t* client)
{
	xcb_icccm_wm_hints_t wm_hints;

	if (! client)
		return;

	if (! xcb_icccm_get_wm_hints_reply
			(conn, xcb_icccm_get_wm_hints_unchecked(conn, client->id),
			 &wm_hints, NULL)) {
		PDEBUG("Couldn't get wm hints.\n");
		return;
	}

	if (wm_hints.flags & XCB_ICCCM_WM_HINT_INPUT) {
		client->allow_focus = !!(wm_hints.input);
	}
}

/*
 * Update local WM_PROTOCOLS information
 */
void icccm_update_wm_protocols(client_t* client)
{
	xcb_get_property_cookie_t cookie;
	xcb_icccm_get_wm_protocols_reply_t protocols;

	cookie = xcb_icccm_get_wm_protocols_unchecked(conn, client->id,
			icccm.wm_protocols);

	client->use_delete = false;

	if (xcb_icccm_get_wm_protocols_reply(conn, cookie, &protocols, NULL)) {
		for (uint32_t i = 0; i < protocols.atoms_len; i++) {
			if (protocols.atoms[i] == icccm.wm_delete_window) {
				client->use_delete = true;
				break;
			}
		}
		xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
	}
}

/*
 * Set border colour, width and event mask for window,
 * reparent etc.
 * Executed for each new handled window (unlike newwin)
 * */
client_t *setup_win(xcb_window_t win)
{
	item_t *item;
	client_t *client;
	uint32_t ws;

	/* Add this window to the X Save Set. */
	xcb_change_save_set(conn, XCB_SET_MODE_INSERT, win);

	/* Remember window and store a few things about it. */
	item = additem(&winlist);

	if (is_null(item)) {
		fprintf(stderr, "setup_win: Out of memory.\n");
		return NULL;
	}

	client = calloc(1, sizeof(client_t));
	if (is_null(client)) {
		fprintf(stderr, "setup_win: Out of memory.\n");
		return NULL;
	}

	item->data = client;

	/* Initialize client. */
	client->id = win;
	client->frame = 0;

	client->usercoord = false;
	client->x = 0;
	client->y = 0;
	client->width = 0;
	client->height = 0;
	client->min_width = 0;
	client->min_height = 0;
	client->max_width = screen->width_in_pixels;
	client->max_height = screen->height_in_pixels;
	client->aspect_num = 0;
	client->aspect_den = 0;
	client->base_width = 0;
	client->base_height = 0;
	client->width_inc = 1;
	client->height_inc = 1;
	client->vertmaxed = false;
	client->maxed = false;
	client->fixed = false;
	client->monitor = NULL;

	client->allow_focus = true;
	client->use_delete = true;
	client->ewmh_state_set = false;

	client->killed = 0;

	client->ignore_unmap = 0;

	client->winitem = item;

	for (ws = 0; ws < WORKSPACES; ws++) {
		client->wsitem[ws] = NULL;
	}

	PDEBUG("Adding window 0x%x\n", client->id);


	/* Get window geometry. */
	if (! getgeom(client->id, &client->x, &client->y, &client->width,
				&client->height)) {
		PDEBUG("Couldn't get geometry in initial setup of window.\n");
	}

	reparent(client);
	setborders(client->frame, conf.borderwidth);
	set_frame_extents(client->id, conf.borderwidth);

	if (shapebase != -1) {
		xcb_shape_select_input(conn, client->id, 1);
		set_shape(client);
	}

	// local used information
	icccm_update_wm_hints(client);
	icccm_update_wm_normal_hints(client);
	icccm_update_wm_protocols(client);

	ewmh_update_state(client);

	// update client list
	ewmh_update_client_list();

	xcb_ewmh_set_wm_allowed_actions(ewmh, client->id,
			sizeof(ewmh_allowed_actions)/sizeof(xcb_atom_t),
			ewmh_allowed_actions);

	return client;
}

/*
 * Get a keycode from a keysym.
 *
 * Returns keycode value.
 */
xcb_keycode_t keysymtokeycode(xcb_keysym_t keysym, xcb_key_symbols_t * keysyms)
{
	xcb_keycode_t *keyp;
	xcb_keycode_t key;

	/* We only use the first keysymbol, even if there are more. */
	keyp = xcb_key_symbols_get_keycode(keysyms, keysym);
	if (is_null(keyp)) {
		fprintf(stderr, "mcwm: Couldn't look up key. Exiting.\n");
		exit(1);
	}

	key = *keyp;
	destroy(keyp);

	return key;
}

/*
 * Set up all shortcut keys.
 *
 * Returns 0 on success, non-zero otherwise.
 */
bool setup_keys(void)
{
	xcb_key_symbols_t *keysyms;
	unsigned i;

	PDEBUG("Setting up keys\n");
	/* Get all the keysymbols. */
	keysyms = xcb_key_symbols_alloc(conn);

	/*
	 * Find out what keys generates our MODKEY mask. Unfortunately it
	 * might be several keys.
	 */
	if (modkeys.keycodes) {
		destroy(modkeys.keycodes);
	}
	modkeys = getmodkeys(MODKEY);

	if (0 == modkeys.len) {
		fprintf(stderr,
				"We couldn't find any keycodes to our main modifier "
				"key! \n");
		return false;
	}

	/* Now grab the rest of the keys with the MODKEY modifier. */
	for (i = KEY_F; i < KEY_MAX; i++) {
		if (XK_VoidSymbol == keys[i].keysym) {
			keys[i].keycode = 0;
			continue;
		}
		keys[i].keycode = keysymtokeycode(keys[i].keysym, keysyms);
		if (0 == keys[i].keycode) {
			/* Couldn't set up keys! */

			/* Get rid of key symbols. */
			xcb_key_symbols_free(keysyms);
			PDEBUG(".. could not setup keys\n");
			return false;
		}

		/* Grab other keys with a modifier mask. */
		xcb_grab_key(conn, 1, screen->root,
				MODKEY | (i == KEY_TAB ? 0 : CONTROLMOD),
				keys[i].keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

		/* also grab with extended modmask */
		if (i >= KEY_H && i <= KEY_L) {
			xcb_grab_key(conn, 1, screen->root,
					MODKEY | CONTROLMOD | SHIFTMOD,
					keys[i].keycode, XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_ASYNC);
		}

	}							/* for */

	/* Need this to take effect NOW! */
	xcb_flush(conn);

	/* Get rid of the key symbols table. */
	xcb_key_symbols_free(keysyms);

	PDEBUG(".. setup successful!\n");
	return true;
}


/*
 * Get ICCCM atoms
 */
bool setup_icccm(void)
{
	icccm.wm_delete_window	= get_atom("WM_DELETE_WINDOW");
	icccm.wm_change_state	= get_atom("WM_CHANGE_STATE");
	icccm.wm_state			= get_atom("WM_STATE");
	icccm.wm_protocols		= get_atom("WM_PROTOCOLS");
	return true;
}

/*
 * Initialize EWMH stuff
 */
bool setup_ewmh(void)
{

	/* Get some atoms. */

	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	if (! ewmh) {
		return false;
	}

	xcb_intern_atom_cookie_t *cookies = xcb_ewmh_init_atoms(conn, ewmh);

	if (is_null(cookies)) {
		return false;
	}

	if (! xcb_ewmh_init_atoms_replies(ewmh, cookies, NULL)) {
		free(ewmh);
		free(cookies);
		return false;
	}

	//	ewmh_1_4_NET_WM_STATE_FOCUSED = get_atom("_NET_WM_STATE_FOCUSED");


	ewmh_allowed_actions[0] = ewmh->_NET_WM_ACTION_MAXIMIZE_VERT;
	ewmh_allowed_actions[1] = ewmh->_NET_WM_ACTION_FULLSCREEN;

	xcb_atom_t atoms[] = {
		ewmh->_NET_SUPPORTED,				// root
		ewmh->_NET_NUMBER_OF_DESKTOPS,		// root
		ewmh->_NET_CURRENT_DESKTOP,			// root
		ewmh->_NET_ACTIVE_WINDOW,			// root
		ewmh->_NET_CLIENT_LIST,				// root

/*		ewmh->_NET_WORKAREA,				// root
/		and _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL */
		ewmh->_NET_WM_NAME,					// window
		ewmh->_NET_WM_DESKTOP,				// window

		ewmh->_NET_WM_STATE,				// window
		ewmh->_NET_WM_STATE_STICKY,			// option
		ewmh->_NET_WM_STATE_MAXIMIZED_VERT,	// option
		ewmh->_NET_WM_STATE_FULLSCREEN,		// option
//		ewmh->_NET_WM_STATE_HIDDEN,			// option
//		ewmh_1_4_NET_WM_STATE_FOCUSED,		// option

		ewmh->_NET_WM_ALLOWED_ACTIONS,		// window
		ewmh->_NET_WM_ACTION_MAXIMIZE_VERT,	// option
		ewmh->_NET_WM_ACTION_FULLSCREEN,	// option

		ewmh->_NET_SUPPORTING_WM_CHECK,		// window
		ewmh->_NET_FRAME_EXTENTS,			// window
		ewmh->_NET_REQUEST_FRAME_EXTENTS,   // message
		ewmh->_NET_CLOSE_WINDOW,			// message
		icccm.wm_change_state,				// message
		icccm.wm_delete_window,				// message
		icccm.wm_change_state,				// message
		icccm.wm_state,						//
		icccm.wm_protocols					//
	};

	xcb_ewmh_set_supported(ewmh, screen_number,
			sizeof(atoms)/sizeof(xcb_atom_t), atoms);

	xcb_ewmh_set_wm_name(ewmh, screen->root, 4, "mcwm");
	xcb_ewmh_set_supporting_wm_check(ewmh, screen->root, screen->root);
	xcb_ewmh_set_number_of_desktops(ewmh, screen_number, workspaces);
	xcb_ewmh_set_active_window(ewmh, screen_number, 0);

	ewmh_update_client_list();

	return true;
}

/*
 * Walk through all existing windows and set them up.
 *
 * Returns 0 on success.
 */
bool setup_screen(void)
{
	xcb_query_tree_reply_t *reply;
	int i;
	int len;
	xcb_window_t *children;
	xcb_get_window_attributes_reply_t *attr;
	client_t *client;
	uint32_t ws;

	/* Get all children. */
	reply = xcb_query_tree_reply(conn, xcb_query_tree(conn, screen->root), 0);
	if (is_null(reply)) {
		return false;
	}

	len = xcb_query_tree_children_length(reply);
	children = xcb_query_tree_children(reply);

	/* Set up all windows on this root. */
	for (i = 0; i < len; i++) {
		attr = xcb_get_window_attributes_reply(conn,
				xcb_get_window_attributes_unchecked(conn,
					children[i]),
				NULL);

		if (is_null(attr)) {
			fprintf(stderr, "Couldn't get attributes for window %d.",
					children[i]);
			continue;
		}

		/*
		 * Don't set up or even bother windows in override redirect
		 * mode.
		 *
		 * This mode means they wouldn't have been reported to us
		 * with a MapRequest if we had been running, so in the
		 * normal case we wouldn't have seen them.
		 *
		 * Only handle visible windows.
		 */
		if (!attr->override_redirect
				&& attr->map_state == XCB_MAP_STATE_VIEWABLE) {
			client = setup_win(children[i]);
			// XXX just check for null
			if (client) {
				/*
				 * Find the physical output this window will be on if
				 * RANDR is active.
				 */
				if (-1 != randrbase) {
					PDEBUG("Looking for monitor on %d x %d.\n", client->x,
							client->y);
					client->monitor = findmonbycoord(client->x, client->y);
#if DEBUG
					if (client->monitor) {
						PDEBUG("Found client on monitor %s.\n",
								client->monitor->name);
					} else {
						PDEBUG("Couldn't find client on any monitor.\n");
					}
#endif
				}

				/* Fit window on physical screen. */
				fitonscreen(client);

				/*
				 * Check if this window has a workspace set already as
				 * a WM hint.
				 *
				 */
				ws = ewmh_get_workspace(children[i]);

				if (ws == NET_WM_FIXED) {
					/* Add to current workspace. */
					addtoworkspace(client, curws);
					/* Add to all other workspaces. */
					fixwindow(client, false);
					show(client);
				} else if (ws < WORKSPACES) {
					addtoworkspace(client, ws);
					/* If it's on our current workspace, show it, else hide it. */
					if (ws == curws) {
						hide(client);
					} else {
						show(client);
					}
				} else {
					/*
					 * No workspace hint at all. Just add it to our
					 * current workspace.
					 */
					addtoworkspace(client, curws);
					show(client);
				}
			}
		}
		destroy(attr);
	}							/* for */

	/*
	 * Get pointer position so we can set focus on any window which
	 * might be under it.
	 */
	set_input_focus(XCB_WINDOW_NONE);
	destroy(reply);

	return true;
}
void set_frame_extents(xcb_window_t win, int width)
{
	long data[] = { width, width, width, width };
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, ewmh->_NET_FRAME_EXTENTS,
			XCB_ATOM_CARDINAL, 32, 4, &data);
}

/*
 * Fit frame window to shape of client window if neccessary
 */
void set_shape(client_t* client)
{
	/* XXX
	 * disable borders, they might appear
	 */
	xcb_shape_query_extents_reply_t *extents;
	xcb_generic_error_t* error;

	extents = xcb_shape_query_extents_reply(conn, xcb_shape_query_extents(conn, client->id), &error);
	if (error) {
		PDEBUG("error querying shape extents for 0x%x\n", client->id);
		print_x_error(error);
		destroy(error);
		return;
	}
	if (extents->bounding_shaped) {
		PDEBUG("0x%x is shaped, shaping frame\n", client->id);
		xcb_shape_combine(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, XCB_SHAPE_SK_BOUNDING,
				client->frame, 0, 0, client->id);
	}
	destroy(extents);
}

/*
 * Setup SHAPE extension
 */
int setup_shape(void)
{
	const xcb_query_extension_reply_t *extension;
	extension = xcb_get_extension_data(conn, &xcb_shape_id);
	if (!extension->present) {
		printf("No SHAPE extension.\n");
		return -1;
	} else {
		return extension->first_event;
	}
}

/*
 * Set up RANDR extension. Get the extension base and subscribe to
 * events.
 */
int setup_randr(void)
{
	const xcb_query_extension_reply_t *extension;
	int base;

	extension = xcb_get_extension_data(conn, &xcb_randr_id);
	if (!extension->present) {
		printf("No RANDR extension.\n");
		return -1;
	} else {
		getrandr();
	}

	base = extension->first_event;
	PDEBUG("randrbase is %d.\n", base);

	xcb_randr_select_input(conn, screen->root,
			XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
			XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
			XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
			XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

	return base;
}

/*
 * Get RANDR resources and figure out how many outputs there are.
 */
void getrandr(void)
{
	xcb_randr_get_screen_resources_current_cookie_t rcookie;
	xcb_randr_get_screen_resources_current_reply_t *res;
	xcb_randr_output_t *outputs;
	int len;
	xcb_timestamp_t timestamp;

	rcookie = xcb_randr_get_screen_resources_current(conn, screen->root);
	res = xcb_randr_get_screen_resources_current_reply(conn, rcookie, NULL);
	if (is_null(res)) {
		printf("No RANDR extension available.\n");
		return;
	}
	timestamp = res->config_timestamp;

	update_timestamp(timestamp);

	// XXX len ought to be uint, at least it is internal for xcb-randr
	len = xcb_randr_get_screen_resources_current_outputs_length(res);
	outputs = xcb_randr_get_screen_resources_current_outputs(res);

	PDEBUG("Found %d outputs.\n", len);

	/* Request information for all outputs. */
	getoutputs(outputs, len, timestamp);

	destroy(res);
}

/*
 * Walk through all the RANDR outputs (number of outputs == len) there
 * was at time timestamp.
 */
void getoutputs(xcb_randr_output_t * outputs, int len,
		xcb_timestamp_t timestamp)
{
	char *name = NULL;
	xcb_randr_get_crtc_info_cookie_t icookie;
	xcb_randr_get_crtc_info_reply_t *crtc = NULL;
	xcb_randr_get_output_info_reply_t *output;
	monitor_t *mon;
	monitor_t *clonemon;

	xcb_randr_get_output_info_cookie_t* ocookie;

	if (len < 1) {
		fprintf(stderr, "No outputs (%d) at all, what should we do now?\n",len);
		return;
	}

	ocookie = calloc(len, sizeof(xcb_randr_get_output_info_cookie_t));
	if (ocookie == NULL) {
		fprintf(stderr, "Out of memory.\n");
		cleanup(1);
	}


	for (int i = 0; i < len; i++) {
		ocookie[i] = xcb_randr_get_output_info(conn, outputs[i], timestamp);
	}

	/* Loop through all outputs. */
	for (int i = 0; i < len; i++) {
		output = xcb_randr_get_output_info_reply(conn, ocookie[i], NULL);

		if (output == NULL) {
			continue;
		}

		const int name_len  = xcb_randr_get_output_info_name_length(output);

		if (name_len <= 0) {
			name = NULL;
		} else {
			if (is_null(name = calloc(name_len + 1, sizeof(char)))) {;
				fprintf(stderr, "Out of memory.\n");
				cleanup(1);
			}
			strncpy(name, (char*)xcb_randr_get_output_info_name(output), name_len);
//			snprintf(name, name_len, "%s",
//					xcb_randr_get_output_info_name(output));
		}
		PDEBUG("Name: \"%s\" (len: %d)\n", name, name_len);
		PDEBUG("id: %d\n", outputs[i]);
		PDEBUG("Size: %d x %d mm.\n", output->mm_width, output->mm_height);

		if (XCB_NONE != output->crtc) {
			icookie = xcb_randr_get_crtc_info(conn, output->crtc, timestamp);
			crtc = xcb_randr_get_crtc_info_reply(conn, icookie, NULL);
			if (is_null(crtc)) {
				if (!is_null(name)) destroy(name);
				destroy(output);
				destroy(ocookie);
				return;
			}

			PDEBUG("CRTC: at %d, %d, size: %d x %d.\n", crtc->x, crtc->y,
					crtc->width, crtc->height);

			/* Check if it's a clone. */
			clonemon = findclones(outputs[i], crtc->x, crtc->y);
			if (clonemon) {
				PDEBUG
					("Monitor %s, id %d is a clone of %s, id %d. Skipping.\n",
					 name, outputs[i], clonemon->name, clonemon->id);
				if (!is_null(name)) destroy(name);
				destroy(crtc);
				destroy(output);
				continue;
			}

			/* Do we know this monitor already? */
			if (is_null((mon = findmonitor(outputs[i])))) {
				PDEBUG("Monitor not known, adding to list.\n");
				addmonitor(outputs[i], name, crtc->x, crtc->y, crtc->width,
						crtc->height);
			} else {
				bool changed = false;
				/*
				 * We know this monitor. Update information. If it's
				 * smaller than before, rearrange windows.
				 */
				PDEBUG("Known monitor. Updating info.\n");

				if (crtc->x != mon->x) {
					mon->x = crtc->x;
					changed = true;
				}
				if (crtc->y != mon->y) {
					mon->y = crtc->y;
					changed = true;
				}

				if (crtc->width != mon->width) {
					mon->width = crtc->width;
					changed = true;
				}
				if (crtc->height != mon->height) {
					mon->height = crtc->height;
					changed = true;
				}

				if (changed) {
					arrbymon(mon);
				}
			}
			destroy(crtc);
		} else {
			PDEBUG("Monitor not used at the moment.\n");
			/*
			 * Check if it was used before. If it was, do something.
			 */
			if ((mon = findmonitor(outputs[i]))) {
				item_t *item;
				client_t *client;

				/* Check all windows on this monitor and move them to
				 * the next or to the first monitor if there is no
				 * next.
				 *
				 * FIXME: Use per monitor workspace list instead of
				 * global window list.
				 */
				for (item = winlist; item; item = item->next) {
					client = item->data;
					if (client->monitor == mon) {
						if (is_null(client->monitor->item->next)) {
							if (is_null(monlist)) {
								client->monitor = NULL;
							} else {
								client->monitor = monlist->data;
							}
						} else {
							client->monitor = client->monitor->item->next->data;
						}

						fitonscreen(client);
					}
				}				/* for */

				/* It's not active anymore. Forget about it. */
				delmonitor(mon);
			}
		}
		destroy(name);
		destroy(output);
	}							/* for */
	destroy(ocookie);
}

void arrbymon(monitor_t *monitor)
{
	client_t *client;

	PDEBUG("arrbymon\n");
	/*
	 * Go through all windows on this monitor. If they don't fit on
	 * the new screen, move them around and resize them as necessary.
	 *
	 * FIXME: Use a per monitor workspace list instead of global
	 * windows list.
	 */
	for (item_t *item = winlist; item; item = item->next) {
		client = item->data;
		if (client->monitor == monitor) {
			fitonscreen(client);
		}
	}							/* for */

}

monitor_t *findmonitor(xcb_randr_output_t id)
{
	monitor_t *mon;

	for (item_t *item = monlist; item; item = item->next) {
		mon = item->data;
		if (id == mon->id) {
			PDEBUG("findmonitor: Found it. Output ID: %d\n", mon->id);
			return mon;
		}
		PDEBUG("findmonitor: Goint to %p.\n", (void*)item->next);
	}

	return NULL;
}

monitor_t *findclones(xcb_randr_output_t id, int16_t x, int16_t y)
{
	monitor_t *clonemon;

	for (item_t *item = monlist; item; item = item->next) {
		clonemon = item->data;

		PDEBUG("Monitor %s: x, y: %d--%d, %d--%d.\n",
				clonemon->name,
				clonemon->x, clonemon->x + clonemon->width,
				clonemon->y, clonemon->y + clonemon->height);

		/* Check for same position. */
		if (id != clonemon->id && clonemon->x == x && clonemon->y == y) {
			return clonemon;
		}
	}

	return NULL;
}

monitor_t *findmonbycoord(int16_t x, int16_t y)
{
	monitor_t* mon;

	for (item_t* item = monlist; item; item = item->next) {
		mon = item->data;
		PDEBUG("Monitor %s: x, y: %d--%d, %d--%d.\n",
				mon->name,
				mon->x, mon->x + mon->width, mon->y, mon->y + mon->height);

		PDEBUG("Is %d,%d between them?\n", x, y);

		if (x >= mon->x && x <= mon->x + mon->width
				&& y >= mon->y && y <= mon->y + mon->height) {
			PDEBUG("findmonbycoord: Found it. Output ID: %d, name %s\n",
					mon->id, mon->name);
			return mon;
		}
	}

	return NULL;
}

void delmonitor(monitor_t *mon)
{
	if (! is_null(mon)) {
		PDEBUG("Deleting output %s.\n", mon->name);
		destroy(mon->name);
		freeitem(&monlist, NULL, mon->item);
	}
}

monitor_t *addmonitor(xcb_randr_output_t id, char *name,
		uint32_t x, uint32_t y, uint16_t width,
		uint16_t height)
{
	item_t *item;
	monitor_t *mon;

	if (is_null((item = additem(&monlist)))) {
		fprintf(stderr, "Out of memory.\n");
		return NULL;
	}

	mon = calloc(1, sizeof(monitor_t));
	if (is_null(mon)) {
		fprintf(stderr, "Out of memory.\n");
		return NULL;
	}

	item->data = mon;

	if (is_null(name)) {
		mon->name = NULL;
	} else {
		mon->name = calloc(strlen(name) + 1, sizeof(char));
		strcpy(mon->name, name);
	}

	mon->id = id;
	mon->x = x;
	mon->y = y;
	mon->width = width;
	mon->height = height;
	mon->item = item;

	return mon;
}

/* Raise window win to top of stack. */
void raisewindow(xcb_drawable_t win)
{
	uint32_t values[] = { XCB_STACK_MODE_ABOVE };

	if (screen->root == win || 0 == win) {
		return;
	}

	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

/*
 * Set window client to either top or bottom of stack depending on
 * where it is now.
 */
void raiseorlower(client_t *client)
{
	uint32_t values[] = { XCB_STACK_MODE_OPPOSITE };
	xcb_drawable_t win;

	if (is_null(client)) {
		PDEBUG("Raising NULL client\n");
		return;
	}

	win = client->frame;

	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

void movelim(client_t *client)
{
	xcb_rectangle_t mon;

	get_mondim(client->monitor, &mon);

	/* Is it outside the physical monitor? */
	if (client->x < mon.x) {
		client->x = mon.x;
	}
	if (client->y < mon.y) {
		client->y = mon.y;
	}

	if (client->x + client->width > mon.x + mon.width - conf.borderwidth * 2) {
		client->x = (mon.x + mon.width - conf.borderwidth * 2) - client->width;
	}

	if (client->y + client->height > mon.y + mon.height - conf.borderwidth * 2) {
		client->y = (mon.y + mon.height - conf.borderwidth * 2)
			- client->height;
	}

	movewindow(client->frame, client->x, client->y);
}

/* Move window win to root coordinates x,y. */
void movewindow(xcb_drawable_t win, uint16_t x, uint16_t y)
{
	uint32_t values[2];

	if (win == screen->root || win == XCB_WINDOW_NONE) {
		/* Can't move root. */
		return;
	}

	values[0] = x;
	values[1] = y;

	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X
			| XCB_CONFIG_WINDOW_Y, values);

}

/* Change focus to next in window ring. */
void focusnext(void)
{

	// XXX check for only one client on list, skip if focussed
	client_t *client = NULL;

#if DEBUG
	if (focuswin) {
		PDEBUG("Focus now in win 0x%x\n", focuswin->id);
	}
#endif

	if (is_null(wslist[curws])) {
		PDEBUG("No windows to focus on in this workspace.\n");
		return;
	}

	if (! is_mode(mode_tab)) {
		/*
		 * Remember what we last focused on. We need this when the
		 * MODKEY is released and we move the last focused window in
		 * the tabbing order list.
		 */
		lastfocuswin = focuswin;
		set_mode(mode_tab);

		PDEBUG("Began tabbing.\n");
	}

	/* If we currently have no focus focus first in list. */
	if (is_null(focuswin) || is_null(focuswin->wsitem[curws])) {
		PDEBUG("Focusing first in list: @%p\n", (void*)wslist[curws]);
		client = wslist[curws]->data;
#if DEBUG
		if (focuswin && is_null(focuswin->wsitem[curws])) {
			// XXX
			PDEBUG("XXX Our focused window 0x%x isn't on this workspace!\n",
					focuswin->id);
		}
#endif
	} else {
		if (is_null(focuswin->wsitem[curws]->next)) {
			/*
			 * We were at the end of list. Focusing on first window in
			 * list unless we were already there.
			 */
			if (focuswin->wsitem[curws] != wslist[curws]->data) {
				PDEBUG("End of list. Focusing first in list: @%p\n",
						(void*)wslist[curws]);
				client = wslist[curws]->data;
			}
		} else {
			/* Otherwise, focus the next in list. */
			PDEBUG("Tabbing. Focusing next: @%p.\n",
					(void*)focuswin->wsitem[curws]->next);
			client = focuswin->wsitem[curws]->next->data;
		}
	}							/* if NULL focuswin */

	if (client && client != focuswin) {
		/*
		 * Raise window if it's occluded, then warp pointer into it and
		 * set keyboard focus to it.
		 */
		uint32_t values[] = { XCB_STACK_MODE_TOP_IF };

		xcb_configure_window(conn, client->frame,
				XCB_CONFIG_WINDOW_STACK_MODE, values);
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->id, 0, 0, 0, 0,
				client->width / 2, client->height / 2);
		setfocus(client);
	}
}

// XXX win vs focuswin?
/* Mark window win as unfocused. */
void setunfocus()
{
	uint32_t values[1];

	PDEBUG("setunfocus() focuswin = 0x%x\n", focuswin ? focuswin->frame : 0);
	if (is_null(focuswin))
		return;

	/* add when there is an atom on that window that marks it focused */
	//	ewmh_update_state(focuswin);

	/* Set new border colour. */
	values[0] = conf.unfocuscol;
	xcb_change_window_attributes(conn, focuswin->frame, XCB_CW_BORDER_PIXEL, values);
}

/*
 * Find client with client->id win or client->frame
 * in global window list.
 *
 * Returns client pointer or NULL if not found.
 */

client_t *findclientp(xcb_drawable_t win)
{
	client_t *client;

	if (win == screen->root) {
		PDEBUG("findclientp(): Root Window\n", win);
		return NULL;
	}

	if (focuswin) {
		if (focuswin->id == win) {
			PDEBUG("findclientp(): Win: 0x%x (focuswin->id)\n", win);
			return focuswin;
		} else if (focuswin->frame == win) {
			PDEBUG("findclientp(): Win: 0x%x (focuswin->frame)\n", win);
			return focuswin;
		}
	}

	for (item_t *item = winlist; item; item = item->next) {
		client = item->data;
		if (win == client->id) {
			PDEBUG("findclientp(): Win: 0x%x (id)\n ", win);
			return client;
		} else if (win == client->frame) {
			PDEBUG("findclientp(): Win: 0x%x (frame)\n", win);
			return client;
		}
	}

	PDEBUG("findclientp(): unknown window 0x%x\n", win);
	return NULL;
}



/*
 * Find client with client->id win in global window list.
 *
 * Returns client pointer or NULL if not found.
 */
client_t *findclient(xcb_drawable_t win)
{
	client_t *client;

	if (win == screen->root) {
		PDEBUG("findclient(): Root Window\n", win);
		return NULL;
	}

	if (focuswin && focuswin->id == win) {
		PDEBUG("findclient(): Win: 0x%x (focuswin->id)\n", win);
		return focuswin;
	}

	for (item_t *item = winlist; item; item = item->next) {
		client = item->data;
		if (win == client->id) {
			PDEBUG("findclient(): Win: 0x%x (id)\n ", win);
			return client;
		}
	}

	PDEBUG("findclient(): unknown window 0x%x\n", win);
	return NULL;
}

// XXX what when we do send a event and the client asks in return?
// XXX we should check if that client is on the current ws
/* Set focus on window client. */
void setfocus(client_t *client)
{
	uint32_t values[1];

	/* XXX this is not true anymore
	 * If client is NULL, we focus on whatever the pointer is on.
	 *
	 * This is a pathological case, but it will make the poor user
	 * able to focus on windows anyway, even though this window
	 * manager might be buggy.
	 */
	if (is_null(client)) {
		PDEBUG("setfocus: client was NULL! \n");

		focuswin = NULL;
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
		xcb_ewmh_set_active_window(ewmh, screen_number, 0);

		return;
	}

	PDEBUG("setfocus: client = 0x%x (focuswin = 0x%x)\n",
			client->id, focuswin ? focuswin->id : 0);

	/* Don't bother focusing on the same window that already has focus */
	if (client == focuswin) {
		return;
	}

	if (client->allow_focus) {
		PDEBUG("xcb_set_input_focus: 0x%x\n", client->id);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				client->id, get_timestamp());
	}

	/* Set new border colour. */
	if (client->fixed) {
		values[0] = conf.fixedcol;
	} else {
		values[0] = conf.focuscol;
	}

	xcb_change_window_attributes(conn, client->frame, XCB_CW_BORDER_PIXEL, values);

	/* Unset last focus. */
	if (focuswin) {
		setunfocus();
	}

	xcb_ewmh_set_active_window(ewmh, screen_number, client->id);

	/* Remember the new window as the current focused window. */
	focuswin = client;
}

int start(char *program)
{
	if (program == NULL)
		return 0;

	const pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return -1;
	} else if (0 == pid) {
		char *argv[2] = { program, NULL };

		/*
		 * Make this process a new process leader, otherwise the
		 * terminal will die when the wm dies. Also, this makes any
		 * SIGCHLD go to this process when we fork again.
		 */

		if (setsid() == -1) {
			perror("setsid");
			exit(1);
		}
		/* reset signals on the new process, allowing it to catch em */
		signal(SIGCHLD, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);

		if (execvp(program, argv) == -1) {
			perror("execve");
			exit(1);
		}
	//	_exit(0);

	}
	return 0;
}

/* Resize with limit. */
void resizelim(client_t *client)
{

	xcb_rectangle_t mon;

	get_mondim(client->monitor, &mon);

	/* Is it smaller than it wants to  be? */
	if (0 != client->min_height && client->height < client->min_height) {
		client->height = client->min_height;
	}

	if (0 != client->min_width && client->width < client->min_width) {
		client->width = client->min_width;
	}

	if (client->x + client->width + conf.borderwidth * 2 > mon.x + mon.width) {
		client->width =
			mon.width - ((client->x - mon.x) + conf.borderwidth * 2);
	}

	if (client->y + client->height + conf.borderwidth * 2 > mon.y + mon.height) {
		client->height =
			mon.height - ((client->y - mon.y) + conf.borderwidth * 2);
	}

	resize(client->frame, client->width, client->height);
	resize(client->id, client->width, client->height);
}

void moveresize(xcb_drawable_t win, uint16_t x, uint16_t y,
		uint16_t width, uint16_t height)
{
	if (win == screen->root || win == XCB_WINDOW_NONE) {
		/* Can't move or resize root. */
		return;
	}

	PDEBUG("Moving to %d, %d, resizing to %d x %d.\n", x, y, width, height);

	const uint32_t values[4] = { x, y, width, height };
	xcb_configure_window(conn, win,
			XCB_CONFIG_WINDOW_X
			| XCB_CONFIG_WINDOW_Y
			| XCB_CONFIG_WINDOW_WIDTH
			| XCB_CONFIG_WINDOW_HEIGHT, values);
}

/* Resize window win to width,height. */
void resize(xcb_drawable_t win, uint16_t width, uint16_t height)
{
	uint32_t values[2];

	if (win == screen->root || win == XCB_WINDOW_NONE ) {
		/* Can't resize root. */
		return;
	}

	PDEBUG("Resizing to %d x %d.\n", width, height);

	values[0] = width;
	values[1] = height;

	xcb_configure_window(conn, win,
			XCB_CONFIG_WINDOW_WIDTH
			| XCB_CONFIG_WINDOW_HEIGHT, values);
}

/*
 * Resize window client in direction direction. Direction is:
 *
 * h = left, that is decrease width.
 *
 * j = down, that is, increase height.
 *
 * k = up, that is, decrease height.
 *
 * l = right, that is, increase width.
 */
void resizestep(client_t *client, char direction)
{
	int step_x = MOVE_STEP;
	int step_y = MOVE_STEP;

	if (is_null(client)) {
		return;
	}

	if (client->maxed) {
		/* Can't resize a fully maximized window. */
		return;
	}

	raisewindow(client->frame);

	if (client->width_inc > 1) {
		step_x = client->width_inc;
	} else {
		step_x = MOVE_STEP;
	}

	if (client->height_inc > 1) {
		step_y = client->height_inc;
	} else {
		step_y = MOVE_STEP;
	}

	switch (direction) {
		case 'h':
			client->width = client->width - step_x;
			break;

		case 'j':
			client->height = client->height + step_y;
			break;

		case 'k':
			client->height = client->height - step_y;
			break;

		case 'l':
			client->width = client->width + step_x;
			break;

		default:
			PDEBUG("resizestep in unknown direction.\n");
			break;
	}							/* switch direction */

	resizelim(client);

	/* If this window was vertically maximized, remember that it isn't now. */
	if (client->vertmaxed) {
		client->vertmaxed = false;
		ewmh_update_state(client);
	}

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->id, 0, 0, 0, 0,
			client->width / 2, client->height / 2);
}

/*
 * Move window win as a result of pointer motion to coordinates
 * rel_x,rel_y.
 */
void mousemove(client_t *client, int rel_x, int rel_y)
{
	client->x = rel_x;
	client->y = rel_y;

	movelim(client);
}

void mouseresize(client_t *client, int rel_x, int rel_y)
{
	const uint16_t width = client->width;
	const uint16_t height = client->width;

	client->width = abs(rel_x - client->x);
	client->height = abs(rel_y - client->y);

	client->width -= (client->width - client->base_width) % client->width_inc;
	client->height -= (client->height - client->base_height)
		% client->height_inc;

	if (client->width == width && client->height == height)
		return;

	PDEBUG("Trying to resize to %dx%d (%dx%d)\n", client->width,
			client->height,
			(client->width - client->base_width) / client->width_inc,
			(client->height - client->base_height) / client->height_inc);

	resizelim(client);

	/* If this window was vertically maximized, remember that it isn't now. */
	if (client->vertmaxed) {
		client->vertmaxed = false;
		ewmh_update_state(client);
	}
}

void movestep(client_t *client, char direction)
{
	int16_t start_x;
	int16_t start_y;

	if (is_null(client)) {
		return;
	}

	if (client->maxed) {
		/* We can't move a fully maximized window. */
		return;
	}

	/* Save pointer position so we can warp pointer here later. */
	if (!getpointer(client->id, &start_x, &start_y)) {
		return;
	}

	raisewindow(client->frame);
	switch (direction) {
		case 'h':
			client->x = client->x - MOVE_STEP;
			break;

		case 'j':
			client->y = client->y + MOVE_STEP;
			break;

		case 'k':
			client->y = client->y - MOVE_STEP;
			break;

		case 'l':
			client->x = client->x + MOVE_STEP;
			break;

		default:
			PDEBUG("movestep: Moving in unknown direction.\n");
			break;
	}							/* switch direction */

	movelim(client);

	/*
	 * If the pointer was inside the window to begin with, move
	 * pointer back to where it was, relative to the window.
	 */
	if (start_x > 0 - conf.borderwidth && start_x < client->width
			+ conf.borderwidth && start_y > 0 - conf.borderwidth && start_y
			< client->height + conf.borderwidth) {
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->id, 0, 0, 0, 0,
				start_x, start_y);
	}
}

void setborders(xcb_drawable_t win, int width)
{
	uint32_t values[1];
	uint32_t mask = XCB_CONFIG_WINDOW_BORDER_WIDTH;

	PDEBUG("Setting borders (%d) to 0x%x\n", width, win);
	values[0] = width;

	xcb_configure_window(conn, win, mask, &values[0]);
}

void unmax(client_t *client)
{

	// XXX if a client is fullscreen allready on start it will not be unmaxed
	// to fit the borders
	if (is_null(client)) {
		PDEBUG("unmax: client was NULL! \n");
		return;
	}

	client->x = client->origsize.x;
	client->y = client->origsize.y;
	client->width = client->origsize.width;
	client->height = client->origsize.height;

	/* Restore geometry. */
	moveresize(client->frame, client->x, client->y, client->width, client->height);
	resize(client->id, client->width, client->height);

	if (client->maxed) {
		setborders(client->frame, conf.borderwidth);
		set_frame_extents(client->id, conf.borderwidth);
	}
	/* Warp pointer to window or we might lose it. */
	xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->id, 0, 0, 0, 0,
			client->width / 2, client->height / 2);

}

void maximize(client_t *client)
{
	xcb_rectangle_t mon;

	if (is_null(client)) {
		PDEBUG("maximize: client was NULL! \n");
		return;
	}

	get_mondim(client->monitor, &mon);

	/*
	 * Check if maximized already. If so, revert to stored
	 * geometry.
	 */
	if (client->vertmaxed) {
		unmax(client);
		client->vertmaxed = false;
	} else if (client->maxed) {
		PDEBUG("<> Client maximized, unmaximizing\n");
		unmax(client);
		client->maxed = false;
		ewmh_update_state(client);
		return;
	}
	PDEBUG("<> Client unmaximized, maximizing!\n");

	client->maxed = true;
	ewmh_update_state(client);


	/* FIXME: Store original geom in property as well? */
	client->origsize.x = client->x;
	client->origsize.y = client->y;
	client->origsize.width = client->width;
	client->origsize.height = client->height;

	/* Remove borders. */
	setborders(client->frame, 0);
	set_frame_extents(client->id, 0);

	/* Move to top left and resize. */
	client->x = mon.x;
	client->y = mon.y;
	client->width = mon.width;
	client->height = mon.height;

	moveresize(client->frame, client->x, client->y, client->width,
				client->height);
	resize(client->id, client->width, client->height);

	/* Raise. Pretty silly to maximize below something else. */
	raisewindow(client->frame);

}

void maxvert(client_t *client)
{
	xcb_rectangle_t mon;

	if (is_null(client)) {
		PDEBUG("maxvert: client was NULL\n");
		return;
	}

	get_mondim(client->monitor, &mon);

	/*
	 * Check if maximized already. If so, revert to stored geometry.
	 */
	if (client->maxed) {
		unmax(client);
		client->maxed = false;
	} else if (client->vertmaxed) {
		unmax(client);
		client->vertmaxed = false;
		ewmh_update_state(client);
		return;
	}

	/* Raise first. Pretty silly to maximize below something else. */
	raisewindow(client->frame);

	/*
	 * Store original coordinates and geometry.
	 * FIXME: Store in property as well?
	 */
	client->origsize.x = client->x;
	client->origsize.y = client->y;
	client->origsize.width = client->width;
	client->origsize.height = client->height;

	client->y = mon.y;
	/* Compute new height considering height increments and screen height. */
	client->height = mon.height - conf.borderwidth * 2;
//	client->height -= (client->height - client->base_height)
//		% client->height_inc;

	/* Move to top of screen and resize. */
	moveresize(client->frame, client->x, client->y, client->width, client->height);
	resize(client->id, client->width, client->height);

	/* Remember that this client is vertically maximized. */
	client->vertmaxed = true;
	ewmh_update_state(client);
}

void default_event_mask(client_t *client)
{
	const uint32_t	mask = XCB_CW_EVENT_MASK;
	const uint32_t	values[] = { DEFAULT_FRAME_EVENTS};
	xcb_change_window_attributes(conn, client->frame, mask, values);
}

void hidden_event_mask(client_t *client)
{
	const uint32_t	mask = XCB_CW_EVENT_MASK;
	const uint32_t	values[] = { HIDDEN_FRAME_EVENTS};
	xcb_change_window_attributes(conn, client->frame, mask, values);
}

void show(client_t *client)
{
	long data[] = { XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE };

	/* Map window and declare normal */
	xcb_map_window(conn, client->id);
	xcb_map_window(conn, client->frame);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->id,
			icccm.wm_state, icccm.wm_state, 32, 2, data);

}

void hide(client_t *client)
{
	long data[] = { XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE };

	/*
	 * Unmap window and declare iconic.
	 *
	 * Unmapping will generate an UnmapNotify event so we can forget
	 * about the window later.
	 * The quantity of that events will be mentioned in ignore_unmap.
	 */
	client->ignore_unmap++;
	PDEBUG("++ignore_unmap == %d\n", client->ignore_unmap);
	/* 4.1.4
	 * Reparenting window managers must unmap the client's window
	 * when it is in the Iconic state, even if an ancestor window
	 * being unmapped renders the client's window unviewable.
	 */
	xcb_unmap_window(conn, client->frame);
	xcb_unmap_window(conn, client->id);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->id,
			icccm.wm_state, icccm.wm_state, 32, 2, data);
}

/*
 * Reparent window
 *
 * also install listening-events to parent and children
 * this does not check if there is allready a parent
 */
void reparent(client_t *client)
{
	/* mask and values for frame window */
	uint32_t	mask = XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
	uint32_t	values[3] = { conf.unfocuscol, 1, HIDDEN_FRAME_EVENTS};

	/* Create new frame window */
	client->frame = xcb_generate_id(conn);
	xcb_create_window(conn, screen->root_depth, client->frame, screen->root,
			client->x,
			client->y,
			client->width, client->height,
			client->maxed ? 0 : conf.borderwidth,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			XCB_COPY_FROM_PARENT,
			mask, values);

	/* set client window borderless */
	setborders(client->id, 0);

	/* mapped window will be unmapped and mapped under new parent
	 * unmapping notify after reparenting
	 */

	/* Reparent client to my window */
	PDEBUG("Reparenting 0x%x to 0x%x\n", client->id, client->frame);
	xcb_reparent_window(conn, client->id, client->frame, 0, 0);

	/* Add default events to clients window */
	mask = XCB_CW_EVENT_MASK;
	values[0] = DEFAULT_WINDOW_EVENTS;
	xcb_change_window_attributes(conn, client->id, mask, values);

}

bool getpointer(xcb_drawable_t win, int16_t *x, int16_t *y)
{
	xcb_query_pointer_reply_t *pointer;

	pointer = xcb_query_pointer_reply(conn,
			xcb_query_pointer_unchecked(conn, win),
			0);

	if (is_null(pointer)) {
		return false;
	}

	*x = pointer->win_x;
	*y = pointer->win_y;

	destroy(pointer);

	return true;
}

bool getgeom(xcb_drawable_t win, int16_t *x, int16_t *y,
		uint16_t *width, uint16_t *height)
{
	xcb_get_geometry_reply_t *geom;

	geom = xcb_get_geometry_reply(conn,
			xcb_get_geometry_unchecked(conn, win),
			NULL);
	if (is_null(geom)) {
		return false;
	}

	*x = geom->x;
	*y = geom->y;
	*width = geom->width;
	*height = geom->height;

	destroy(geom);

	return true;
}

void topleft(void)
{
	int16_t pointx;
	int16_t pointy;
	xcb_rectangle_t mon;

	if (is_null(focuswin)) {
		return;
	}

	get_mondim(focuswin->monitor, &mon);

	raisewindow(focuswin->frame);

	if (!getpointer(focuswin->id, &pointx, &pointy)) {
		return;
	}

	focuswin->x = mon.x;
	focuswin->y = mon.y;

	movewindow(focuswin->frame, focuswin->x, focuswin->y);
	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id,
			0, 0, 0, 0, pointx, pointy);

}

void topright(void)
{
	int16_t pointx;
	int16_t pointy;
	xcb_rectangle_t mon;

	if (is_null(focuswin)) {
		return;
	}

	get_mondim(focuswin->monitor, &mon);

	raisewindow(focuswin->frame);

	if (!getpointer(focuswin->id, &pointx, &pointy)) {
		return;
	}

	focuswin->x = mon.x + mon.width - (focuswin->width + conf.borderwidth * 2);
	focuswin->y = mon.y;

	movewindow(focuswin->frame, focuswin->x, focuswin->y);
	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id,
			0, 0, 0, 0, pointx, pointy);

}

void botleft(void)
{
	int16_t pointx;
	int16_t pointy;
	xcb_rectangle_t mon;

	if (is_null(focuswin)) {
		return;
	}

	get_mondim(focuswin->monitor, &mon);

	raisewindow(focuswin->frame);

	if (!getpointer(focuswin->id, &pointx, &pointy)) {
		return;
	}

	focuswin->x = mon.x;
	focuswin->y = mon.y + mon.height - (focuswin->height + conf.borderwidth * 2);

	movewindow(focuswin->frame, focuswin->x, focuswin->y);
	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id,
			0, 0, 0, 0, pointx, pointy);

}

void botright(void)
{
	int16_t pointx;
	int16_t pointy;
	xcb_rectangle_t mon;

	if (is_null(focuswin)) {
		return;
	}

	get_mondim(focuswin->monitor, &mon);

	raisewindow(focuswin->frame);

	if (! getpointer(focuswin->id, &pointx, &pointy)) {
		return;
	}

	focuswin->x = mon.x + mon.width
		- (focuswin->width + conf.borderwidth * 2);
	focuswin->y = mon.y + mon.height
		- (focuswin->height + conf.borderwidth * 2);

	movewindow(focuswin->frame, focuswin->x, focuswin->y);
	xcb_warp_pointer(conn, XCB_WINDOW, focuswin->id,
			0, 0, 0, 0, pointx, pointy);

}

/*
 * End program
 */

void deletewin(client_t* client)
{
	if (is_null(client)) {
		return;
	}


	if (client->use_delete && client->killed++ < 3) {
		/* WM_DELETE_WINDOW message */
		xcb_client_message_event_t ev = {
			.response_type = XCB_CLIENT_MESSAGE,
			.format = 32,
			.sequence = 0,
			.window = client->id,
			.type = icccm.wm_protocols, // ewmh.WM_PROTOCOLS available
			.data.data32 = {icccm.wm_delete_window, get_timestamp()}
		};

		PDEBUG("deletewin: 0x%x (send_event #%d)\n", client->id,
				client->killed);
		xcb_send_event(conn, false, client->id,
				XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
	} else {
		/* WM_DELETE_WINDOW either NA or failed 3 times  */
		/* TODO * Destroy Window instead ? */
		PDEBUG("deletewin: 0x%x (kill_client)\n", client->id);
		xcb_kill_client(conn, client->id);
	}

}

void prevscreen(void)
{
	item_t *item;

	if (is_null(focuswin) || is_null(focuswin->monitor)) {
		return;
	}

	item = focuswin->monitor->item->prev;

	if (is_null(item)) {
		return;
	}

	focuswin->monitor = item->data;

	raisewindow(focuswin->frame);
	fitonscreen(focuswin);
	movelim(focuswin);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id,
			0, 0, 0, 0, 0, 0);
}

void nextscreen(void)
{
	item_t *item;

	if (is_null(focuswin) || is_null(focuswin->monitor)) {
		return;
	}

	item = focuswin->monitor->item->next;

	if (is_null(item)) {
		return;
	}

	focuswin->monitor = item->data;

	raisewindow(focuswin->frame);
	fitonscreen(focuswin);
	movelim(focuswin);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id,
			0, 0, 0, 0, 0, 0);
}

/* Helper function to configure a window. */
void configwin(xcb_window_t win, uint16_t old_mask, winconf_t wc)
{
	uint32_t values[7];
	int i = 0;

	uint16_t mask = 0;

	if (old_mask & XCB_CONFIG_WINDOW_X) {
		mask |= XCB_CONFIG_WINDOW_X;
		values[i++] = wc.x;
	}

	if (old_mask & XCB_CONFIG_WINDOW_Y) {
		mask |= XCB_CONFIG_WINDOW_Y;
		values[i++] = wc.y;
	}

	if (old_mask & XCB_CONFIG_WINDOW_WIDTH) {
		mask |= XCB_CONFIG_WINDOW_WIDTH;
		values[i++] = wc.width;
	}

	if (old_mask & XCB_CONFIG_WINDOW_HEIGHT) {
		mask |= XCB_CONFIG_WINDOW_HEIGHT;
		values[i++] = wc.height;
	}

	if (old_mask & XCB_CONFIG_WINDOW_SIBLING) {
		mask |= XCB_CONFIG_WINDOW_SIBLING;
		values[i++] = wc.sibling;
	}

	if (old_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
		mask |= XCB_CONFIG_WINDOW_STACK_MODE;
		values[i++] = wc.stackmode;
	}

	if (i > 0) {
		xcb_configure_window(conn, win, mask, values);
	}
}


void events(void)
{
	xcb_generic_event_t *ev = NULL;

	int fd;						/* Our X file descriptor */
	fd_set in;					/* For select */

	/* Get the file descriptor so we can do select() on it. */
	fd = xcb_get_file_descriptor(conn);
	if (fd == -1) {
		fprintf(stderr, "Could not connect to xcb-fd\n");
		cleanup(1);
	}

	for (sigcode = 0; sigcode == 0;) {
		/*
		 * Check for events, again and again. When poll returns NULL
		 * (and it does that a lot), we block on select() until the
		 * event file descriptor gets readable again.
		 *
		 * We do it this way instead of xcb_wait_for_event() since
		 * select() will return if we were interrupted by a signal. We
		 * like that.
		 */

		FD_ZERO(&in); FD_SET(fd, &in);

		if (select(fd + 1, &in, NULL, NULL, NULL) == -1) {
			/* We received a signal. Break out of loop. */
			if (errno == EINTR)
				break;

			perror("mcwm select");
			cleanup(1);
		}

		while ((ev = xcb_poll_for_event(conn))) {
			const uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(ev);
			PDEBUG("Event: %s (%d, handler: %d)\n",
					xcb_event_get_label(response_type),
					response_type,
					handler[response_type] ? 1 : 0);

			if (randrbase != -1 && response_type == (randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
				PDEBUG("RANDR screen change notify. Checking outputs.\n");
				getrandr();
			} else if (shapebase != -1 && response_type == shapebase + XCB_SHAPE_NOTIFY) {
				xcb_shape_notify_event_t *sev = (xcb_shape_notify_event_t*) ev;
				set_timestamp(sev->server_time);

				PDEBUG("SHAPE notify (win: 0x%x, shaped: %d)\n", sev->affected_window, sev->shaped);
				if (sev->shaped) {
					client_t* client = findclient(sev->affected_window);
					if (client) {
						set_shape(client);
					}
				}
			} else if (handler[response_type]) {
				handler[response_type](ev);
			}
			destroy(ev);
		}

		xcb_flush(conn);

		/*
		 * Check if we have an unrecoverable connection error,
		 * like a disconnected X server.
		 */
		if (xcb_connection_has_error(conn)) {
			cleanup(1);
		}
	}
}

/*
 * Event handlers
 */

void print_x_error(xcb_generic_error_t *e)
{
	fprintf(stderr, "mcwm: X error = %s - %s (code: %d, op: %d/%d res: 0x%x seq: %d, fseq: %d)\n",
		xcb_event_get_error_label(e->error_code),
		xcb_event_get_request_label(e->major_code),
		e->error_code,
		e->major_code,
		e->minor_code,
		e->resource_id,
		e->sequence,
		e->full_sequence);
}

void handle_error_event(xcb_generic_event_t *ev)
{
	xcb_generic_error_t *e = (xcb_generic_error_t*) ev;
	print_x_error(e);
}

void handle_map_request(xcb_generic_event_t* ev)
{
	xcb_map_request_event_t *e;
	e = (xcb_map_request_event_t *) ev;

	new_win(e->window);
}

void handle_property_notify(xcb_generic_event_t *ev)
{
	const xcb_property_notify_event_t* e = (xcb_property_notify_event_t*)ev;
	client_t *client = findclient(e->window);

	update_timestamp(e->time);

	if (! client)
		return;

#ifdef DEBUG
	char* name = get_atomname(e->atom);

	if (is_null(name)) {
		PDEBUG("0x%x notfifies changed atom (%d)\n", e->window, e->atom);
	} else {
		PDEBUG("0x%x notifies changed atom (%d: %s)\n", e->window, e->atom, name);
		free(name);
	}
#endif

	switch (e->atom) {
		case XCB_ATOM_WM_HINTS:
			icccm_update_wm_hints(client);
			break;
		case XCB_ATOM_WM_NORMAL_HINTS:
			icccm_update_wm_normal_hints(client);
			break;
		default:
			if (e->atom == icccm.wm_protocols) {
				icccm_update_wm_protocols(client);
			}
			/*else if (e->atom == ewmh->_NET_WM_STATE) {
				PDEBUG("Atom was _NET_WM_STATE, this shall not happen!\n");
			} */
			break;
	}
}

void handle_button_press(xcb_generic_event_t* ev)
{
	xcb_button_press_event_t *e = (xcb_button_press_event_t *) ev;

	update_timestamp(e->time);

	PDEBUG("Button %d pressed in window 0x%x, subwindow 0x%x "
			"coordinates (%d,%d)\n",
			e->detail, e->event, e->child, e->event_x,
			e->event_y);

	if (0 == e->child) {
		/* Mouse click on root window. Start programs? */

		switch (e->detail) {
			case 1:	/* Mouse button one. */
				start(MOUSE1);
				break;

			case 2:	/* Middle mouse button. */
				start(MOUSE2);
				break;

			case 3:	/* Mouse button three. */
				start(MOUSE3);
				break;
		}			/* switch */
		return;
	}

	/*
	 * If we don't have any currently focused window, we can't
	 * do anything. We don't want to do anything if the mouse
	 * cursor is in the wrong window (root window or a panel,
	 * for instance). There is a limit to sloppy focus.
	 */
	if (is_null(focuswin)
			|| (focuswin->frame != e->child && focuswin->id != e->child)) {
		PDEBUG("Somehow in the wrong window?\n");
		return;
	}

	/*
	 * If middle button was pressed, raise window or lower
	 * it if it was already on top.
	 */
	if (2 == e->detail) {
		raiseorlower(focuswin);
	} else {
		int16_t pointx;
		int16_t pointy;

		/* We're moving or resizing. */

		/*
		 * Get and save pointer position inside the window
		 * so we can go back to it when we're done moving
		 * or resizing.
		 */
		if (!getpointer(focuswin->frame, &pointx, &pointy)) {
			return;
		}

		mode_x = pointx;
		mode_y = pointy;

		/* Raise window. */
		raisewindow(focuswin->frame);

		/* Mouse button 1 was pressed. */
		if (1 == e->detail) {
			set_mode(mode_move);
		} else {
			/* Mouse button 3 was pressed. */

			set_mode(mode_resize);
			/* Warp pointer to lower right. */
			xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->id, 0,
					0, 0, 0, focuswin->width,
					focuswin->height);
			xcb_flush(conn);
		}

		/*
		 * Take control of the pointer in the root window
		 * and confine it to root.
		 *
		 * Give us events when the key is released or if
		 * any motion occurs with the key held down.
		 *
		 * Keep updating everything else.
		 *
		 * Don't use any new cursor.
		 */
		xcb_grab_pointer(conn, 0, screen->root,
				XCB_EVENT_MASK_BUTTON_RELEASE
				| XCB_EVENT_MASK_BUTTON_MOTION
				| XCB_EVENT_MASK_POINTER_MOTION_HINT,
				XCB_GRAB_MODE_ASYNC,
				XCB_GRAB_MODE_ASYNC,
				screen->root, XCB_NONE, get_timestamp());


		PDEBUG("mode now : %d\n", get_mode());
	}
}


void handle_motion_notify(xcb_generic_event_t *ev) //wunused
{
	/*
	 * We can't do anything if we don't have a focused window
	 * or if it's fully maximized.
	 */

	xcb_input_device_motion_notify_event_t *e =
		(xcb_input_device_motion_notify_event_t*)ev;

	update_timestamp(e->time);

	if (is_null(focuswin) || focuswin->maxed) {
		return;
	}

	/*
	 * This is not really a real notify, but just a hint that
	 * the mouse pointer moved. This means we need to get the
	 * current pointer position ourselves.
	 */
	xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(conn,
			xcb_query_pointer(conn, screen->root), 0);

	if (is_null(pointer)) {
		PDEBUG("Couldn't get pointer position.\n");
		return;
	}

	/*
	 * Our pointer is moving and since we even get this event
	 * we're either resizing or moving a window.
	 */
	if (is_mode(mode_move)) {
		mousemove(focuswin, pointer->root_x - mode_x, pointer->root_y - mode_y);
	} else if (is_mode(mode_resize)) {
		mouseresize(focuswin, pointer->root_x, pointer->root_y);
	} else {
		PDEBUG("Motion event when we're not moving our resizing! \n");
	}
	destroy(pointer);
}

void handle_button_release(xcb_generic_event_t *ev)
{
	xcb_button_release_event_t *e =
		(xcb_button_release_event_t*)ev;

	update_timestamp(e->time);

	PDEBUG("Mouse button released! mode = %d\n", get_mode());

	if (is_mode(mode_nothing))
		return;

	/* We're finished moving or resizing. */
	if (is_null(focuswin)) {
		PDEBUG("No focused window when finished moving or resizing!");
		/*
		 * We don't seem to have a focused window! Just
		 * ungrab and reset the mode.
		 */
	}

	xcb_ungrab_pointer(conn, get_timestamp());
	xcb_flush(conn);	/* Important! */

	set_mode(mode_nothing);
	PDEBUG("mode now = %d\n", get_mode());

	/* XXX obsolete now?
	 * We will get an EnterNotify and focus another window
	 * if the pointer just happens to be on top of another
	 * window when we ungrab the pointer, so we have to
	 * warp the pointer before to prevent this.
	 *
	 * Move to saved position within window or if that
	 * position is now outside current window, move inside
	 * window.
	 */
}

void handle_key_press(xcb_generic_event_t *ev)
{
	int i;
	key_enum_t key;

	xcb_key_press_event_t *e = (xcb_key_press_event_t*)ev;

	update_timestamp(e->time);

	PDEBUG("key_press: Key %d pressed (state: %d).\n", e->detail, e->state);

	/* XXX * this should be superflous as we only grab wanted keys */
	for (key = KEY_MAX, i = KEY_F; i < KEY_MAX; i++) {
		if (keys[i].keycode && e->detail == keys[i].keycode) {
			key = i;
			break;
		}
	}

	if (is_mode(mode_tab) && key != KEY_TAB) {
		/* First finish tabbing around. Then deal with the next key. */
		finishtabbing();
	}

	if (key == KEY_MAX) {
		PDEBUG("key_press: Unknown key pressed.\n");

		/*
		 * We don't know what to do with this key. Send this key press
		 * event to the focused window.
		 *
		 * XXX
		 * We should not have received this key because we didn't
		 * grab it
		 */
		xcb_send_event(conn, false, XCB_SEND_EVENT_DEST_ITEM_FOCUS,
				XCB_EVENT_MASK_NO_EVENT, (char *) e);
		return;
	}

	if (e->state & CONTROLMOD) {
		/* META+CTRL */
		if (e->state & SHIFTMOD) {
			/* META+CTRL+SHIFT */
			switch (key) {
				case KEY_H:		/* h */
					resizestep(focuswin, 'h');
					break;

				case KEY_J:		/* j */
					resizestep(focuswin, 'j');
					break;

				case KEY_K:		/* k */
					resizestep(focuswin, 'k');
					break;

				case KEY_L:		/* l */
					resizestep(focuswin, 'l');
					break;

				default:
					PDEBUG("got key I didn't register for 0: (%d)\n", key);
					xcb_send_event(conn, false, XCB_SEND_EVENT_DEST_ITEM_FOCUS,
							XCB_EVENT_MASK_NO_EVENT, (char *) e);
					break;
			}
		} else {
			/* META+CTRL without SHIFT */
			switch (key) {
				case KEY_RET:		/* return */
					start(conf.terminal);
					break;

				case KEY_M:		/* m */
					start(conf.menu);
					break;

				case KEY_F:		/* f */
					fixwindow(focuswin, true);
					break;

				case KEY_H:		/* h */
					movestep(focuswin, 'h');
					break;

				case KEY_J:		/* j */
					movestep(focuswin, 'j');
					break;

				case KEY_K:		/* k */
					movestep(focuswin, 'k');
					break;

				case KEY_L:		/* l */
					movestep(focuswin, 'l');
					break;

				case KEY_V:		/* v */
					maxvert(focuswin);
					break;

				case KEY_R:		/* r */
					raiseorlower(focuswin);
					break;

				case KEY_X:		/* x */
					maximize(focuswin);
					break;

				case KEY_1:
					changeworkspace(0);
					break;

				case KEY_2:
					changeworkspace(1);
					break;

				case KEY_3:
					changeworkspace(2);
					break;

				case KEY_4:
					changeworkspace(3);
					break;

				case KEY_5:
					changeworkspace(4);
					break;

				case KEY_6:
					changeworkspace(5);
					break;

				case KEY_7:
					changeworkspace(6);
					break;

				case KEY_8:
					changeworkspace(7);
					break;

				case KEY_9:
					changeworkspace(8);
					break;

				case KEY_0:
					changeworkspace(9);
					break;

				case KEY_Y:
					topleft();
					break;

				case KEY_U:
					topright();
					break;

				case KEY_B:
					botleft();
					break;

				case KEY_N:
					botright();
					break;

				case KEY_END:
					deletewin(focuswin);
					break;

				case KEY_PREVSCR:
					prevscreen();
					break;

				case KEY_NEXTSCR:
					nextscreen();
					break;

				case KEY_ICONIFY:
					if (conf.allowicons) {
						hidden_event_mask(focuswin);
						hide(focuswin);
					}
					break;
				default:
					PDEBUG("got key I didn't register for 1: (%d)\n", key);
					xcb_send_event(conn, false, XCB_SEND_EVENT_DEST_ITEM_FOCUS,
							XCB_EVENT_MASK_NO_EVENT, (char *) e);
					break;
			}					/* switch unshifted */
		}
	} else {
		/* only META */
		if (key == KEY_TAB) {	/* tab */
			PDEBUG("key_press: MOD+TAB\n");
			focusnext();
		} else {
			PDEBUG("got key I didn't register for 2: (%d)\n", key);
			xcb_send_event(conn, false, XCB_SEND_EVENT_DEST_ITEM_FOCUS,
					XCB_EVENT_MASK_NO_EVENT, (char *) e);
		}
	}
}

void handle_key_release(xcb_generic_event_t *ev)
{
	xcb_key_release_event_t *e = (xcb_key_release_event_t *) ev;
	unsigned i;

	update_timestamp(e->time);

	PDEBUG("key_release: Key %d released (state %d).\n", e->detail, e->state);

	if (is_mode(mode_tab)) {
		/*
		 * Check if it's the that was released was a key
		 * generating the MODKEY mask.
		 */
		for (i = 0; i < modkeys.len; i++) {
			PDEBUG("key_release: Is it %d?\n", modkeys.keycodes[i]);

			if (e->detail == modkeys.keycodes[i]) {
				finishtabbing();

				/* Get out of for... */
				break;
			}
		}			/* for keycodes. */
	}				/* if tabbing. */
}

void handle_enter_notify(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *) ev;
	client_t *client;

	update_timestamp(e->time);

	PDEBUG
		("event: Enter notify eventwin 0x%x, child 0x%x, detail %d.\n",
		 e->event, e->child, e->detail);

	/*
	 * If this isn't a normal enter notify, don't bother.
	 *
	 * We also need ungrab events, since these will be
	 * generated on button and key grabs and if the user for
	 * some reason presses a button on the root and then moves
	 * the pointer to our window and releases the button, we
	 * get an Ungrab EnterNotify.
	 *
	 * The other cases means the pointer is grabbed and that
	 * either means someone is using it for menu selections or
	 * that we're moving or resizing. We don't want to change
	 * focus in those cases.
	 */

	if (!(e->mode == XCB_NOTIFY_MODE_NORMAL
				|| e->mode != XCB_NOTIFY_MODE_UNGRAB)) {
		PDEBUG("Not a normal enter notify!\n");
		return;
	}

#ifdef DEBUG
	/* TODO * why is that */
	if (e->detail == XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL) {
		PDEBUG("> root: 0x%x event: 0x%x child: 0x%x state: %d\n", e->root, e->event, e->child, e->state);
		//break;
	}
#endif
	/*
	 * If we're entering the same window we focus now,
	 * then don't bother focusing.
	 */
	if (is_null(focuswin) || (e->event != focuswin->id && e->event != focuswin->frame)) {
		/*
		 * Otherwise, set focus to the window we just
		 * entered if we can find it among the windows we
		 * know about. If not, just keep focus in the old
		 * window.
		 */
		client = findclientp(e->event);
		if (client) {
			if (! is_mode(mode_tab)) {
				/*
				 * We are focusing on a new window. Since
				 * we're not currently tabbing around the
				 * window ring, we need to update the
				 * current workspace window list: Move
				 * first the old focus to the head of the
				 * list and then the new focus to the head
				 * of the list.
				 */
				if (focuswin) {
					movetohead(&wslist[curws],
							focuswin->wsitem[curws]);
					lastfocuswin = NULL;
				}

				movetohead(&wslist[curws],
						client->wsitem[curws]);
			} /* if not tabbing */

			setfocus(client);
		}
	}
}

void handle_configure_notify(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e
		= (xcb_configure_notify_event_t *) ev;

	if (e->window == screen->root) {
		/*
		 * When using RANDR or Xinerama, the root can change
		 * geometry when the user adds a new screen, tilts
		 * their screen 90 degrees or whatnot. We might need
		 * to rearrange windows to be visible.
		 *
		 * We might get notified for several reasons, not just
		 * if the geometry changed. If the geometry is
		 * unchanged we do nothing.
		 */
		PDEBUG("Notify event for root! \n");
		PDEBUG("Possibly a new root geometry: %dx%d\n",
				e->width, e->height);

		if (e->width == screen->width_in_pixels
				&& e->height == screen->height_in_pixels) {
			/* Root geometry is really unchanged. Do nothing. */
			PDEBUG("Hey! Geometry didn't change.\n");
		} else {
			screen->width_in_pixels = e->width;
			screen->height_in_pixels = e->height;

			/* Check for RANDR. */
			if (-1 == randrbase) {
				/* We have no RANDR so we rearrange windows to
				 * the new root geometry here.
				 *
				 * With RANDR enabled, we handle this per
				 * screen getrandr() when we receive an
				 * XCB_RANDR_SCREEN_CHANGE_NOTIFY event.
				 */
				arrangewindows();
			}
		}
	}
}

void handle_configure_request(xcb_generic_event_t *ev)
{
	winconf_t wc;
	client_t *client;

	xcb_configure_request_event_t* e = (xcb_configure_request_event_t*) ev;

	/* Find the client. */
	if (! (client = findclient(e->window))) {
		PDEBUG("We don't know about this window yet.\n");

		/*
		 * Unmapped window. Just pass all options except border
		 * width.
		 */
		wc.x = e->x;
		wc.y = e->y;
		wc.width = e->width;
		wc.height = e->height;
		wc.sibling = e->sibling;
		wc.stackmode = e->stack_mode;
		wc.borderwidth = e->border_width;

		configwin(e->window,
				e->value_mask & ~XCB_CONFIG_WINDOW_BORDER_WIDTH, wc);
		return;
	}

	bool resizing = false;
	xcb_rectangle_t mon;

	/* Find monitor position and size. */
	get_mondim(is_null(client) ? NULL : client->monitor, &mon);

#if 0
	/*
	 * We ignore moves the user haven't initiated, that is do
	 * nothing on XCB_CONFIG_WINDOW_X and XCB_CONFIG_WINDOW_Y
	 * ConfigureRequests.
	 *
	 * Code here if we ever change our minds or if you, dear user,
	 * wants this functionality.
	 */

	if (e->value_mask & XCB_CONFIG_WINDOW_X) {
		/* Don't move window if maximized. Don't move off the screen. */
		if (!client->maxed && e->x > 0) {
			client->x = e->x;
		}
	}

	if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
		/*
		 * Don't move window if maximized. Don't move off the
		 * screen.
		 */
		if (!client->maxed && !client->vertmaxed && e->y > 0) {
			client->y = e->y;
		}
	}
#endif

	/* XXX * allow maxed client to unmax that way ? */
	if (! client->maxed) {
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
			/* Don't resize if maximized. */
			client->width = e->width;
			resizing = true;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
			/* Don't resize if maximized. */
			client->height = e->height;
			resizing = true;
		}

		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
			PDEBUG("BORDER_WIDTH REQUEST: %d\n", e->border_width);
			if (e->border_width != 0 && ! client->maxed) {
				setborders(client->frame, conf.borderwidth);
				set_frame_extents(client->id, conf.borderwidth);
			} else {
				setborders(client->frame, 0);
				set_frame_extents(client->id, 0);
			}
		}
	}


	/*
	 * XXX Do we really need to pass on sibling and stack mode
	 * configuration? Do we want to?
	 */
	if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
		uint32_t values[1];
		client_t *sibling = findclient(e->sibling);
		PDEBUG("configure request : sibling 0x%x\n", e->sibling);

		if (sibling) {
			values[0] = sibling->frame;
		} else {
			values[0] = e->sibling;
		}
		xcb_configure_window(conn, client->frame,
				XCB_CONFIG_WINDOW_SIBLING, values);

	}

	if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
		uint32_t values[1];

		PDEBUG("configure request : stack mode\n");
		values[0] = e->stack_mode;
		xcb_configure_window(conn, client->frame,
				XCB_CONFIG_WINDOW_STACK_MODE, values);
	}

	/* Check if window fits on screen after resizing. */

	if (! resizing) {
		return;
	}

	if (client->x + client->width + 2 * conf.borderwidth
			> mon.x + mon.width) {
		/*
		 * See if it fits if we move away the window from the
		 * right edge of the screen.
		 */
		client->x = mon.x + mon.width
			- (client->width + 2 * conf.borderwidth);

		/*
		 * If we moved over the left screen edge, move back and
		 * fit exactly on screen.
		 */
		if (client->x < mon.x) {
			client->x = mon.x;
			client->width = mon.width - 2 * conf.borderwidth;
		}
	}

	if (client->y + client->height + 2 * conf.borderwidth
			> mon.y + mon.height) {
		/*
		 * See if it fits if we move away the window from the
		 * bottom edge.
		 */
		client->y = mon.y + mon.height
			- (client->height + 2 * conf.borderwidth);

		/*
		 * If we moved over the top screen edge, move back and fit
		 * on screen.
		 */
		if (client->y < mon.y) {
			PDEBUG("over the edge: y < %d\n", mon.y);
			PDEBUG(" mon.x = %d, mon.y = %d, mon.height = %d, mon.width = %d, conf.borderwidth = %d\n",
					mon.x, mon.y, mon.height, mon.width, conf.borderwidth);

			client->y = mon.y;
			client->height = mon.height - 2 * conf.borderwidth;
		}
	}

	moveresize(client->frame, client->x, client->y, client->width, client->height);
	if (resizing) {
		resize(client->id, client->width, client->height);
	}
}

static void handle_client_message(xcb_generic_event_t *ev)
{
	xcb_client_message_event_t *e
		= (xcb_client_message_event_t *) ev;

	client_t *client = findclient(e->window);


	/* Some window want's to know how our frame extends, anyone welcome */
	if (e->type == ewmh->_NET_REQUEST_FRAME_EXTENTS) {
		PDEBUG("client_message: _NET_REQUEST_FRAME_EXTENTS for 0x%x.\n", e->window);
		set_frame_extents(e->window, client && client->maxed ? 0 : conf.borderwidth);
		return;
	}

	/* We don't act on any other client messages from unhandled windows */
	if (! client) {
		PDEBUG("client_message: unknown window (0x%x), type: %d!\n",
				e->window, e->type);
		return;
	}

	if (e->type == icccm.wm_change_state && e->format == 32) {
		PDEBUG("client_message: wm_change_state\n");
		if (conf.allowicons) {
			if (e->data.data32[0] == XCB_ICCCM_WM_STATE_ICONIC) {
				hidden_event_mask(client);
				hide(client);
				return;
			}
		}
		return;
	}
	if (e->type == ewmh->_NET_CLOSE_WINDOW) {
		PDEBUG("client_message: net_close_window\n");
		if (e->data.data32[1] == XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER) { // direct user, pager
			deletewin(client);
		}
		return;
	}
	if (e->type == ewmh->_NET_ACTIVE_WINDOW) {
		PDEBUG("client_message: net_active_window\n");
		if (e->data.data32[1] == XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER) { // direct user, pager
			setfocus(client);
		}
		return;
	}
	if (e->type == ewmh->_NET_WM_STATE) {
		PDEBUG("client_message: net_wm_state\n");
		//		bool max_h	= false;
		//		bool hide	= false;
		bool max_v	= false;
		bool fs		= false;

		int action	= e->data.data32[0];

		for (int i = 1; i < 3; i++) {
			xcb_atom_t atom = (xcb_atom_t)e->data.data32[i];
			if (atom == ewmh->_NET_WM_STATE_FULLSCREEN)
				fs = true;
			else if (atom == ewmh->_NET_WM_STATE_MAXIMIZED_VERT)
				max_v = true;
//			else if (atom == ewmh->_NET_WM_STATE_MAXIMIZE_HORZ) // XXX unimpl
//				max_h = true;
			//			else if (atom == ewmh->_NET_WM_STATE_HIDDEN)		// TODO
			//				hide = true;
#ifdef DEBUG
			else {
				if (i == 2 && atom == 0)
					break;
				PDEBUG("Unknown _NET_WM_STATE demanded! (%d)\n", atom);
			}
#endif
		}
		// TODO logic
		// only one wins today
		switch (action) {
			case XCB_EWMH_WM_STATE_ADD:
				PDEBUG(">> add\n");
				if (fs && !client->maxed) {
					maximize(client);
					break;
				}
				if (max_v && !client->vertmaxed) {
					maxvert(client);
					break;
				}
				break;
			case XCB_EWMH_WM_STATE_TOGGLE:
				PDEBUG(">> toggle\n");
				if (fs) {
					maximize(client);
					break;
				}
				if (max_v) {
					maxvert(client);
					break;
				}
				break;
			case XCB_EWMH_WM_STATE_REMOVE:
				PDEBUG(">> remove\n");
				if (fs && client->maxed) {
					maximize(client);
					break;
				}
				if (fs && client->vertmaxed) {
					maxvert(client);
					break;
				}
				break;
		} // switch

	} // if _net_wm_state
//	} else if (e->type == ewmh->_NET_CURRENT_DESKTOP) {
}

void handle_circulate_request(xcb_generic_event_t *ev)
{
	xcb_circulate_request_event_t *e
		= (xcb_circulate_request_event_t *) ev;

	/*
	 * Subwindow e->window to parent e->event is about to be
	 * restacked.
	 *
	 * Just do what was requested, e->place is either
	 * XCB_PLACE_ON_TOP or _ON_BOTTOM. We don't care.
	 */
	xcb_circulate_window(conn, e->window, e->place);
}

void handle_mapping_notify(xcb_generic_event_t *ev)
{
	xcb_mapping_notify_event_t *e
		= (xcb_mapping_notify_event_t *) ev;

	/*
	 * XXX Gah! We get a new notify message for *every* key!
	 * We want to know when the entire keyboard is finished.
	 * Impossible? Better handling somehow?
	 */

	/*
	 * We're only interested in keys and modifiers, not
	 * pointer mappings, for instance.
	 */
	if (e->request != XCB_MAPPING_MODIFIER
			&& e->request != XCB_MAPPING_KEYBOARD) {
		return;
	}

	/* Forget old key bindings. */
	xcb_ungrab_key(conn, XCB_GRAB_ANY, screen->root,
			XCB_MOD_MASK_ANY);
	/* Use the new ones. */
	if (! setup_keys()) {
		fprintf(stderr, "mcwm: Couldn't set up keycodes. Exiting.");
		cleanup(1);
	}
}

void handle_unmap_notify(xcb_generic_event_t *ev)
{
	const xcb_unmap_notify_event_t *e
		= (xcb_unmap_notify_event_t *) ev;


	/*
	 * Find the window in our *current* workspace list, then
	 * forget about it. If it gets mapped, we add it to our
	 * lists again then.
	 *
	 * Note that we might not know about the window we got the
	 * UnmapNotify event for. It might be a window we just
	 * unmapped on *another* workspace when changing
	 * workspaces, for instance, or it might be a window with
	 * override redirect set. This is not an error.
	 *
	 * XXX We might need to look in the global window list,
	 * after all. Consider if a window is unmapped on our last
	 * workspace while changing workspaces... If we do this,
	 * we need to keep track of our own windows and ignore
	 * UnmapNotify on them. XXX is that I did that
	 *
	 * XXX all wrong
	 */
	// XXX
	// maybe like evilwm c->ignore_unmap
	// also see dwm.c


	// XXX differentate between notifies between parent and id XXX
	// CURRENT
	//
//		xcb_change_save_set(conn, XCB_SET_MODE_DELETE, client->id);

	PDEBUG("unmap_notify: sent=%d event=0x%x, window=0x%x, seq=%d\n",
			XCB_EVENT_SENT(ev),
			e->event, e->window, e->sequence);

	client_t *client = findclient(e->window);
	if (client) {
		/* we await that unmap, do nothing */
		if (client->ignore_unmap > 0) {
			client->ignore_unmap--;
			PDEBUG("--ignore_unmap\n");
			return;
		}

		if (XCB_EVENT_SENT(ev)) {
			// synthetic event, indicates wanting to withdrawn state
			PDEBUG("unmap_notify for 0x%x [synthetic]\n", e->window);
		}
		withdraw_client(client);
		remove_client(client);
	}
}

void handle_destroy_notify(xcb_generic_event_t *ev)
{
	/*
	 * Find this window in list of clients and forget about
	 * it.
	 */

	const xcb_destroy_notify_event_t *e
		= (xcb_destroy_notify_event_t *) ev;

	client_t *client = findclient(e->window);
	PDEBUG("destroy_notify for 0x%x (is client = %d)\n", e->window, client ? 1 : 0);

	if (client) {
		/*
		 * If we had focus or our last focus in this window,
		 * forget about the focus.
		 *
		 * We will get an EnterNotify if there's another window
		 * under the pointer so we can set the focus proper later.
		 */

		/* XXX
		 * We should never ever have a client at this state, right?
		 *
		 * */
		PDEBUG("XXX Should not happen\n");
		PDEBUG("Destroy frame window if it still exists (0x%x)\n",
				client->frame);
		xcb_destroy_window(conn, client->frame);
		remove_client(client);
	}
}

#if 0
void handle_focus_in(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e = (xcb_focus_in_event_t*)ev;

	// XXX I dont know what to do here, copied from dwm :P
	if (focuswin &&
			(e->event != focuswin->id && e->event != focuswin->frame)) {
		setfocus(focuswin);
	}
}
#endif

void printhelp(void)
{
	printf("mcwm: Usage: mcwm [-b] [-t terminal-program] [-f colour] "
			"[-u colour] [-x colour] \n");
	printf("  -b means draw no borders\n");
	printf("  -t urxvt will start urxvt when MODKEY + Return is pressed\n");
	printf("  -f colour sets colour for focused window borders of focused "
			"to a named color.\n");
	printf("  -u colour sets colour for unfocused window borders.");
	printf("  -x color sets colour for fixed window borders.");
}

void sigcatch(int sig)
{
	sigcode = sig;
}

/*
 * Get a defined atom from the X server.
 */
xcb_atom_t get_atom(char *atom_name)
{
	xcb_intern_atom_cookie_t atom_cookie;
	xcb_atom_t atom;
	xcb_intern_atom_reply_t *rep;

	atom_cookie = xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
	rep = xcb_intern_atom_reply(conn, atom_cookie, NULL);
	if (rep) {
		atom = rep->atom;
		destroy(rep);
		return atom;
	}
	PDEBUG("Atom %s didn't work out.\n", atom_name);

	return XCB_ATOM_NONE;
}


#if DEBUG
/*
 * Get atom name string
 *
 * returns malloc'd string or NULL
 */

char* get_atomname(xcb_atom_t atom)
{
	char* name;
	xcb_get_atom_name_reply_t *an_rep;

	an_rep = xcb_get_atom_name_reply(conn,
			xcb_get_atom_name_unchecked(conn, atom), NULL);

	if (is_null(an_rep)) {
		return NULL;
	}
	name = calloc(xcb_get_atom_name_name_length(an_rep) + 1,
			sizeof(char));
	strncpy(name, xcb_get_atom_name_name(an_rep),
			xcb_get_atom_name_name_length(an_rep));
	destroy(an_rep);

	return name;
}
#endif

void get_mondim(monitor_t* monitor, xcb_rectangle_t* sp)
{
	if (is_null(monitor)) {
		/*
		 * A window isn't attached to any physical monitor. This
		 * probably means there is no RANDR, so we use the root window
		 * size.
		 */
		sp->x = 0;
		sp->y = 0;
		sp->width = screen->width_in_pixels;
		sp->height = screen->height_in_pixels;
	} else {
		sp->x = monitor->x;
		sp->y = monitor->y;
		sp->width = monitor->width;
		sp->height = monitor->height;
	}
}
#if 0
int get_wm_name(xcb_window_t win, char** string, int* len)
{
	if (! get_wm_name_ewmh(win, string, len)) {
		if (! get_wm_name_icccm(win, string, len)) {
			*string = NULL;
			*len = 0;
			return 0;
		}
	}
	return 1;
}


// this is all horrible
// what about the encoding
int get_wm_name_icccm(xcb_window_t win, char **string, int* len)
{
	if (string == NULL)
		return 0;

	*string = NULL;
	*len = 0;

	xcb_get_property_cookie_t			cookie;
	xcb_icccm_get_text_property_reply_t	data;

	cookie = xcb_icccm_get_wm_name(conn, win);
	if (! xcb_icccm_get_wm_name_reply(conn, cookie, &data, NULL))
		return 0;

	if (data.name_len) {
		*string = calloc(data.name_len + 1, sizeof(char));
		*len = data.name_len;
		if (is_null(*string)) {
			perror("get_wm_name icccm");
			cleanup(1);
		}
		memcpy(*string, data.name, data.name_len * sizeof(char));
	}
	xcb_icccm_get_text_property_reply_wipe(&data);
	return 1;
}
// get NET_WM_NAME or if unsuccessful
//
int get_wm_name_ewmh(xcb_window_t win, char **string, int* len)
{
	if (string == NULL)
		return 0;

	*string = NULL;
	*len = 0;

	xcb_get_property_cookie_t			cookie;
	xcb_ewmh_get_utf8_strings_reply_t	data;

	cookie = xcb_ewmh_get_wm_name(ewmh, win);
	if (! xcb_ewmh_get_wm_name_reply(ewmh, cookie, &data, NULL)) {
		return 0;
	}

	if (data.strings_len) {
		*string = calloc(data.strings_len + 1, sizeof(char));
		*len = data.strings_len;

		if (is_null(*string)) {
			perror("get_wm_name_ewmh");
			cleanup(1);
		}
		memcpy(*string, data.strings, data.strings_len * sizeof(char));
	}
	xcb_ewmh_get_utf8_strings_reply_wipe(&data);
	return 1;
}
#endif

int main(int argc, char **argv)
{
	uint32_t mask = 0;
	uint32_t values[2];
	int ch;						/* Option character */
	screen_number = 0;
	xcb_void_cookie_t cookie;
	xcb_generic_error_t *error;
	xcb_drawable_t root;
	char *focuscol;
	char *unfocuscol;
	char *fixedcol;
	xcb_screen_iterator_t iter;

	/* Install signal handlers. */

	set_timestamp(XCB_CURRENT_TIME);

	ewmh = NULL; // XXX i dont like that

	/* We ignore child exists. Don't create zombies. */
	if (SIG_ERR == signal(SIGCHLD, SIG_IGN)) {
		perror("mcwm: signal");
		exit(1);
	}

	if (SIG_ERR == signal(SIGINT, sigcatch)) {
		perror("mcwm: signal");
		exit(1);
	}

	if (SIG_ERR == signal(SIGTERM, sigcatch)) {
		perror("mcwm: signal");
		exit(1);
	}

	/* Set up defaults. */

	conf.borderwidth = BORDERWIDTH;
	conf.terminal = TERMINAL;
	conf.menu = MENU;
	conf.allowicons = ALLOWICONS;
	focuscol = FOCUSCOL;
	unfocuscol = UNFOCUSCOL;
	fixedcol = FIXEDCOL;

	while (1) {
		ch = getopt(argc, argv, "b:it:f:u:x:");
		if (-1 == ch) {

			/* No more options, break out of while loop. */
			break;
		}

		switch (ch) {
			case 'b':
				/* Border width */
				conf.borderwidth = atoi(optarg);
				break;

			case 'i':
				conf.allowicons = true;
				break;

			case 't':
				conf.terminal = optarg;
				break;

			case 'f':
				focuscol = optarg;
				break;

			case 'u':
				unfocuscol = optarg;
				break;

			case 'x':
				fixedcol = optarg;
				break;

			default:
				printhelp();
				exit(0);
		}						/* switch */
	}

	/*
	 * Use $DISPLAY. After connecting scrno will contain the value of
	 * the display's screen.
	 */
	conn = xcb_connect(NULL, &screen_number);
	if (xcb_connection_has_error(conn)) {
		perror("xcb_connect");
		exit(1);
	}

	/* Find our screen. */

	iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (int i = 0; i < screen_number; ++i) {
		xcb_screen_next(&iter);
	}

	screen = iter.data;
	if (!screen) {
		fprintf(stderr, "mcwm: Can't get the current screen. Exiting.\n");
		xcb_disconnect(conn);
		exit(1);
	}

	root = screen->root;

	PDEBUG("Screen size: %dx%d\nRoot window: 0x%x\n",
			screen->width_in_pixels, screen->height_in_pixels, screen->root);

	/* Get some colours. */
	conf.focuscol = getcolor(focuscol);
	conf.unfocuscol = getcolor(unfocuscol);
	conf.fixedcol = getcolor(fixedcol);

	/* setup ICCCM */
	if (! setup_icccm()) {
		fprintf(stderr, "mcwm: Failed to initialize ICCCM atoms. Exiting.\n");
		cleanup(1);
	}

	/* setup EWMH-lib */
	if (! setup_ewmh()) {
		fprintf(stderr, "mcwm: Failed to initialize xcb-ewmh. Exiting.\n");
		cleanup(1);
	}

	/* Check for RANDR extension and configure. */
	randrbase = setup_randr();

	/* Check for SHAPE extension */
	shapebase = setup_shape();

	/* Loop over all clients and set up stuff. */
	if (! setup_screen()) {
		fprintf(stderr, "mcwm: Failed to initialize windows. Exiting.\n");
		xcb_disconnect(conn);
		exit(1);
	}

	/* Set up key bindings. */
	if (! setup_keys()) {
		fprintf(stderr, "mcwm: Couldn't set up keycodes. Exiting.");
		cleanup(1);
	}

	/* Grab mouse buttons. */

	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 1 /* left mouse button */ ,
			MOUSEMODKEY);

	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 2 /* middle mouse button */ ,
			MOUSEMODKEY);

	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 3 /* right mouse button */ ,
			MOUSEMODKEY);

	/* Subscribe to events. */
	mask = XCB_CW_EVENT_MASK;

	values[0] = DEFAULT_ROOT_WINDOW_EVENTS;

	cookie = xcb_change_window_attributes_checked(conn, root, mask, values);
	error = xcb_request_check(conn, cookie);

	if (error) {
		fprintf(stderr, "mcwm: Can't get SUBSTRUCTURE REDIRECT. "
				"Another window manager running? Exiting.\n");
		print_x_error(error);
		destroy(error);
		xcb_disconnect(conn);
		exit(1);
	}

	xcb_flush(conn);
	set_input_focus(XCB_WINDOW_NONE);
	/* Loop over events. */
	events();

	/* Die gracefully. */
	cleanup(sigcode);
}
void set_input_focus(xcb_window_t win)
{
	xcb_query_pointer_reply_t *pointer;

	if (win == XCB_WINDOW_NONE) {
		pointer = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, screen->root), 0);

		if (! is_null(pointer)) {
			win = pointer->child;
			destroy(pointer);
		}
	}
	setfocus(findclientp(win));
}
