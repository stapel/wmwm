/* wmwm, mcwm fork */

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

#include "wmwm.h"

#include <errno.h>            // for EINTR, errno
#include <getopt.h>           // for optarg, getopt
#include <poll.h>             // for pollfd, poll, POLLIN
#include <signal.h>           // for signal, SIG_DFL, SIG_ERR, SIGCHLD, SIGINT
#include <stdbool.h>          // for false, bool, true
#include <stdint.h>           // for uint32_t, uint16_t, uint8_t, int16_t
#include <stdio.h>            // for NULL, fprintf, stderr, perror, printf
#include <stdlib.h>           // for free, exit, calloc, atoi, strtoul
#include <string.h>           // for strlen, memset, strcpy, strncpy
#include <unistd.h>           // for execvp, fork, setsid, pid_t

#include <X11/keysymdef.h>    // for XK_VoidSymbol

#include <xcb/xcb.h>          // for xcb_generic_event_t, xcb_generic_error_t
#include <xcb/randr.h>        // for xcb_randr_get_crtc_info_reply_t, xcb_ra...
#include <xcb/shape.h>        // for xcb_shape_notify_event_t, xcb_shape_com...
#include <xcb/xcb_event.h>    // for xcb_event_get_error_label, xcb_event_ge...
#include <xcb/xcb_ewmh.h>     // for xcb_ewmh_connection_t, xcb_ewmh_set_act...
#include <xcb/xcb_icccm.h>    // for xcb_size_hints_t, ::XCB_ICCCM_SIZE_HINT...
#include <xcb/xcb_keysyms.h>  // for xcb_key_symbols_free, xcb_key_symbols_t
#include <xcb/xinput.h>       // for xcb_input_device_motion_notify_event_t
#include <xcb/xproto.h>       // for xcb_rectangle_t, xcb_screen_t, xcb_atom_t

/* list functions */
#include "list.h"             // for list_t, list_add, list_to_head, list_erase...


/* Check here for user configurable parts: */
#include "config.h"


#define PERROR(Args...) \
	do { fprintf(stderr, "ERROR wmwm: "); fprintf(stderr, ##Args); } while(0)

#ifdef DEBUG
#define PDEBUG(Args...) \
	do { fprintf(stderr, "wmwm: "); fprintf(stderr, ##Args); } while(0)
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

typedef enum {
	step_up 	= 1 << 0,
	step_down 	= 1 << 1,
	step_left 	= 1 << 2,
	step_right 	= 1 << 3
} step_direction_t;

/*
typedef enum {
	sh_us_position	= XCB_ICCCM_SIZE_HINT_US_POSITION,
	sh_us_size		= XCB_ICCCM_SIZE_HINT_US_SIZE,
	sh_position		= XCB_ICCCM_SIZE_HINT_P_POSITION,
	sh_size			= XCB_ICCCM_SIZE_HINT_P_SIZE,
	sh_min_size		= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE,
	sh_max_size		= XCB_ICCCM_SIZE_HINT_P_MAX_SIZE,
	sh_resize_inc	= XCB_ICCCM_SIZE_HINT_P_RESIZE_INC,
	sh_aspect		= XCB_ICCCM_SIZE_HINT_P_ASPECT,
	sh_base_size	= XCB_ICCCM_SIZE_HINT_BASE_SIZE,
	sh_win_gravity	= XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY
} size_hint_t;
*/

/* This means we didn't get any window hint at all. */
#define WORKSPACE_NONE  0xfffffffe

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
#define DEFAULT_ROOT_WINDOW_EVENTS (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_ENTER_WINDOW )



/* Globals */

int sigcode;					/* Signal code. Non-zero if we've been
								 * interrupted by a signal. */
xcb_connection_t *conn;			/* Connection to X server. */
xcb_screen_t *screen;			/* Our current screen.  */
int screen_number;

xcb_timestamp_t	current_time;	/* latest timestamp XXX */

xcb_ewmh_connection_t *ewmh;		/* EWMH Connection */
xcb_atom_t ewmh__NET_WM_STATE_FOCUSED; /* atom not in extension */

int randrbase;					/* Beginning of RANDR extension events. */
int shapebase;					/* Beginning of SHAPE extension events. */

uint32_t curws = 0;				/* Current workspace. */

int16_t mode_x = 0;
int16_t mode_y = 0;

client_t *focuswin = NULL;		/* Current focus window. */
client_t *lastfocuswin = NULL;	/* Last focused window. NOTE! Only
								 * used to communicate between
								 * start and end of tabbing
								 * mode. */

list_t *winlist = NULL;			/* Global list of all client windows. */
list_t *monlist = NULL;			/* List of all physical monitor outputs. */

wm_mode_t MCWM_mode = mode_nothing;		/* Internal mode, such as move or resize */

/*
 * Workspace list: Every workspace has a list of all visible
 * windows.
 */
list_t *wslist[WORKSPACES] = {
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

/* Shortcut key type and initialization. */
struct keys {
	xcb_keysym_t keysym;
	xcb_keycode_t keycode;
} keys[KEY_MAX] = {
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
	uint32_t focuscol;			/* Focused border color. */
	uint32_t unfocuscol;		/* Unfocused border color.  */
	bool allowicons;			/* Allow windows to be unmapped. */
} conf;

/* elemental atoms not in ewmh */
// JUST USE XCB_WM_NAME_ etc pp?
struct icccm {
	xcb_atom_t wm_delete_window;	/* WM_DELETE_WINDOW event to close windows.  */
	xcb_atom_t wm_change_state;
	xcb_atom_t wm_state;
	xcb_atom_t wm_protocols;		/* WM_PROTOCOLS.  */

	xcb_atom_t wm_take_focus;

} icccm;

static xcb_atom_t ewmh_allowed_actions[2] = { XCB_ATOM_NONE, XCB_ATOM_NONE };

/* Functions declarations. */

/* print out X error to stderr */
static void print_x_error(xcb_generic_error_t *e);

/* event handlers */
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
static void handle_colormap_notify(xcb_generic_event_t*);

// RESPONSE_TYPE_MASK is uint_8t (and is only 0x1f, so little waste)
static void (*handler[XCB_EVENT_RESPONSE_TYPE_MASK]) (xcb_generic_event_t*) = {
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
	[XCB_PROPERTY_NOTIFY]	= handle_property_notify,
	[XCB_COLORMAP_NOTIFY]	= handle_colormap_notify
};

static uint32_t getcolor(const char *colstr);
static xcb_atom_t get_atom(char *atom_name);
#if DEBUG
static char* get_atomname(xcb_atom_t atom);
#endif

static bool ewmh_is_fullscreen(client_t*);
static uint32_t ewmh_get_workspace(xcb_drawable_t win);
static void ewmh_update_client_list();
static void ewmh_frame_extents(xcb_window_t win, int width);
static void ewmh_update_state(client_t* client);

static void resize_step(client_t *client, step_direction_t direction);
static void mouse_move(client_t *client, int rel_x, int rel_y);
static void mouse_resize(client_t *client, int rel_x, int rel_y);
static void move_step(client_t *client, step_direction_t direction);

static void set_workspace(client_t *client, uint32_t ws);
static void change_workspace(uint32_t ws);

static void fix_client(client_t *client);
static void update_shape(client_t *client);
static void raise_client(client_t *client);
static void raise_or_lower_client(client_t *client);
static void set_focus(client_t *client);
static void unset_focus();
static void focus_next(void);
static void finish_tab(void);

static void toggle_fullscreen(client_t *client);
static void toggle_vertical(client_t *client);
static void unmax(client_t *client);

static void attach_frame(client_t *client);
static void delete_win(client_t*);
static void hide(client_t *client);
static void remove_client(client_t *client);
static void show(client_t *client);

static void send_client_message(xcb_window_t window, xcb_atom_t atom);
static void send_configuration(client_t *client);

static void set_input_focus(xcb_window_t win);
static void set_borders(xcb_drawable_t win, int width);
static void update_bordercolor(client_t* client);

static void arrbymon(monitor_t *monitor);
static void arrangewindows(void);

static int start(char *program);
static void new_win(xcb_window_t win);
static client_t *create_client(xcb_window_t win);

static key_enum_t key_from_keycode(xcb_keycode_t keycode);
static struct modkeycodes get_modkeys(xcb_mod_mask_t modmask);
static xcb_keycode_t keysym_to_keycode(xcb_keysym_t keysym,
									 xcb_key_symbols_t * keysyms);

static bool setup_keys(void);
static bool setup_screen(void);
static bool setup_ewmh(void);
static int setup_randr(void);
static void get_randr(void);
static void get_outputs(xcb_randr_output_t * outputs, int len,
					   xcb_timestamp_t timestamp);

static monitor_t *find_monitor(xcb_randr_output_t id);
static monitor_t *find_clones(xcb_randr_output_t id, int16_t x, int16_t y);
static monitor_t *find_monitor_at(int16_t x, int16_t y);
static void del_monitor(monitor_t *mon);
static monitor_t *add_monitor(xcb_randr_output_t id, char *name,
								  uint32_t x, uint32_t y, uint16_t width,
								  uint16_t height);

static void apply_gravity(client_t *client, xcb_rectangle_t* geometry);
static int update_geometry(client_t *client, const xcb_rectangle_t *geometry);

static client_t *find_client(xcb_drawable_t win);
static client_t *find_clientp(xcb_drawable_t win);

static bool get_pointer(xcb_drawable_t win, int16_t * x, int16_t * y);
static bool get_geometry(xcb_drawable_t win, xcb_rectangle_t *geometry);

static void set_hidden_events(client_t *client);
static void set_default_events(client_t *client);

static void warp_focuswin(step_direction_t direction);
static void prev_screen(void);
static void next_screen(void);
static void configure_win(xcb_window_t win, uint16_t old_mask, winconf_t wc);
static void events(void);
static void print_help(void);
static void signal_catch(int sig);

static void get_monitor_geometry(monitor_t* monitor, xcb_rectangle_t* sp);

static void cleanup(int code);

/* Function bodies. */
// XXX this is just a little precaution and encapsulation
static void			set_mode(wm_mode_t modus)	{ MCWM_mode = modus; }
static wm_mode_t	get_mode(void)				{ return MCWM_mode; }
static bool			is_mode(wm_mode_t modus)	{ return (get_mode() == modus); }

static xcb_timestamp_t get_timestamp() { return current_time; }
static void set_timestamp(xcb_timestamp_t t) { current_time = t; }
static void update_timestamp(xcb_timestamp_t t) { if (t != XCB_TIME_CURRENT_TIME) current_time = t; }

/* check if pointer is over client */
/* XXX check for workspace and monitor */
bool pointer_over_client(client_t* client)
{
	int16_t x,y;
	const xcb_rectangle_t *geo = &client->geometry;

	if (! get_pointer(screen->root, &x, &y))
		return false;

	return (x >= geo->x &&
			y >= geo->y &&
			x <= geo->width + geo->x &&
			y <= geo->height + geo->y);
}

/*
 * Update client's window's atoms
 */
void ewmh_update_state(client_t* client)
{
	xcb_atom_t atoms[5];
	uint32_t i = 0;

	if (! client)
		return;

	if (client->fullscreen)
		atoms[i++] = ewmh->_NET_WM_STATE_FULLSCREEN;
	if (client->vertmaxed)
		atoms[i++] = ewmh->_NET_WM_STATE_MAXIMIZED_VERT;
	if (client->hidden)
		atoms[i++] = ewmh->_NET_WM_STATE_HIDDEN;
	if (client == focuswin)
		atoms[i++] = ewmh__NET_WM_STATE_FOCUSED;

	if (i > 0) {
		xcb_ewmh_set_wm_state(ewmh, client->id, i, atoms);
		client->ewmh_state_set = true;
	} else if (client->ewmh_state_set) {
		/* remove atom if there's no state and an old atom */
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
void finish_tab(void)
{
	PDEBUG("Finish tabbing!\n");
	set_mode(mode_nothing);

	if (lastfocuswin && focuswin) {
		list_to_head(&wslist[curws], lastfocuswin->wsitem[curws]);
		lastfocuswin = NULL;
	}
}

/* XXX: I don't know what that does at all */
/*
 * Find out what keycode modmask is bound to. Returns a struct. If the
 * len in the struct is 0 something went wrong.
 */
struct modkeycodes get_modkeys(xcb_mod_mask_t modmask)
{
	xcb_get_modifier_mapping_cookie_t cookie;
	xcb_get_modifier_mapping_reply_t *reply;
	xcb_keycode_t *modmap;
	struct modkeycodes keycodes = {
		NULL,
		0
	};
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
	if (! reply) {
		return keycodes;
	}

	keycodes.keycodes = calloc(reply->keycodes_per_modifier,
			sizeof(xcb_keycode_t));

	if (! keycodes.keycodes) {
		PERROR("Out of memory.\n");
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
	for (unsigned mask = 0; mask < (sizeof(masks) / sizeof(int)); mask++) {
		if (masks[mask] == modmask) {
			for (uint8_t i = 0; i < reply->keycodes_per_modifier; i++) {
				if (modmap[mask * reply->keycodes_per_modifier + i]) {
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
	/*
	 * Go through all windows. If they don't fit on the new screen,
	 * move them around and resize them as necessary.
	 */
	for (list_t *item = winlist; item; item = item->next) {
		client_t *client = item->data;
		update_geometry(client, NULL);
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
	list_t *item;
	xcb_window_t *window_list;

	uint32_t windows = 0;

	/* leave if no windows */
	if (! winlist) {
		xcb_ewmh_set_client_list(ewmh, screen_number, 0, NULL);
		return;
	}

	/* count windows */
	for (item = winlist; item; item = item->next, ++windows);

	/* create window array */
	window_list = calloc(windows, sizeof(xcb_window_t));
	if (! window_list) {
		xcb_ewmh_set_client_list(ewmh, screen_number, 0, NULL);
		return;
	}

	/* fill window array in reverse order */
	uint32_t id = windows;
	for (item = winlist; item; item = item->next) {
		const client_t* client = item->data;
		window_list[--id] = client->id;
	}
	xcb_ewmh_set_client_list(ewmh, screen_number, windows, window_list);
	destroy(window_list);
}

/*
 * Check if window has _NET_WM_STATE_FULLSCREEN atom
 */
bool ewmh_is_fullscreen(client_t* client)
{
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
 * Returns either workspace, WORKSPACE_NONE if we didn't find any hints.
 */
uint32_t ewmh_get_workspace(xcb_drawable_t win)
{
	xcb_get_property_cookie_t cookie;
	uint32_t ws;

	cookie = xcb_ewmh_get_wm_desktop_unchecked(ewmh, win);
	if (! xcb_ewmh_get_wm_desktop_reply(ewmh, cookie, &ws, NULL)) {
		return WORKSPACE_NONE;
	}
	return ws;
}

/*
 * set client to one/all or no workspace
 */
void set_workspace(client_t *client, uint32_t ws)
{
	list_t *item;

	PDEBUG("set workspace for 0x%x to %u\n", client->id, ws);

	/* remove from all workspaces but ws */
	for (uint32_t i = 0; i < WORKSPACES; i++) {
		if (i != ws && client->wsitem[i]) {
			list_remove(&wslist[i], client->wsitem[i]);
			client->wsitem[i] = NULL;
		}
	}

	/* add if not hidden or already */
	if (ws != WORKSPACE_NONE && ! client->wsitem[ws]) {
		/* add to destined workspace */
		if ((item = list_add(&wslist[ws])) == NULL) {
			perror("wmwm");
			return;
		}
		client->wsitem[ws] = item;
		item->data = client;
	}

	/* Set _NET_WM_DESKTOP accordingly or leave it  */
	if (ws != WORKSPACE_NONE) {
		xcb_ewmh_set_wm_desktop(ewmh, client->id, ws);
	}
}


/* Change current workspace to ws. */
void change_workspace(uint32_t ws)
{
	list_t *item;
	client_t *client;

	if (ws == curws) {
		return;
	}

	PDEBUG("Changing from workspace #%d to #%d\n", curws, ws);

	/*
	 * We lose our focus if the window we focus isn't fixed. An
	 * EnterNotify event will set focus later.
	 */
	if (focuswin)
		unset_focus();

	/* Apply hidden window event mask, this ensures no invalid enter events */
	for (item = wslist[curws]; item; item = item->next) {
		client = item->data;
		set_hidden_events(client);
	}

	/* Go through list of current ws. Unmap everything that isn't fixed. */
	for (item = wslist[curws]; item; item = item->next) {
		client = item->data;
		/*
		 * This is an ordinary window. Just unmap it. Note that
		 * this will generate an unnecessary UnmapNotify event
		 * which we will try to handle later.
		 */
		hide(client);
	}

	/* Set the new current workspace */
	xcb_ewmh_set_current_desktop(ewmh, screen_number, ws);
	curws = ws;

	/* Go through list of new ws. Map everything that isn't fixed. */
	for (item = wslist[curws]; item; item = item->next) {
		client = item->data;
		show(client);
	}

	/* Re-enable enter events */
	for (item = wslist[ws]; item; item = item->next) {
		client = item->data;
		set_default_events(client);
	}

	/* Map the windows now */
	xcb_flush(conn);

	/* Set focus on the window under the mouse */
	set_input_focus(XCB_WINDOW_NONE);
}

/*
 * Get the pixel values of a named color colstr.
 *
 * Returns pixel values.
 */
uint32_t getcolor(const char *colstr)
{
	xcb_alloc_named_color_reply_t *col_reply;
	xcb_generic_error_t *error;
	xcb_alloc_named_color_cookie_t colcookie;

	uint32_t color;

	if (! colstr)
		return 0;

	if (strlen(colstr) > 1 && *colstr == '#') {
		return (uint32_t)strtoul((colstr + 1), NULL, 16);
	}
	colcookie = xcb_alloc_named_color(conn, screen->default_colormap, strlen(colstr),
			colstr);
	col_reply = xcb_alloc_named_color_reply(conn, colcookie, &error);

	if (col_reply == NULL) {
		PERROR("Couldn't get pixel value for color %s. Exiting.\n", colstr);
		print_x_error(error);
		destroy(error);
		cleanup(1);
	}
	color = col_reply->pixel;
	destroy(col_reply);
	return color;
}

/* Check new geometrys legality, apply hints and update window */
int update_geometry(client_t *client,
		const xcb_rectangle_t *geometry)
{
	xcb_rectangle_t monitor;
	xcb_rectangle_t geo;

	const xcb_size_hints_t *hints = &client->hints;
	const int border = client->fullscreen ? 0 : conf.borderwidth;

	get_monitor_geometry(client->monitor, &monitor);

	/* XXX: check if geometry or monitor geometry changed (or hints, maybe set a geo changed flag) */

	/* Fullscreen, skip the checks  */
	if (client->fullscreen) {
		geo = monitor;
		goto out;
	}

	/* Is geometry proposed, or do we check current */
	if (! geometry)
		geo = client->geometry;
	else
		geo = *geometry;

	/* Is it bigger than our viewport? */
	if (geo.width + border * 2 > monitor.width)
		geo.width = monitor.width - border * 2;

	if (geo.height + border * 2 > monitor.height)
		geo.height = monitor.height - border * 2;

	/* Is size within allowed increments */
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC &&
			(hints->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE
			 || hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {

		geo.width -= (geo.width - hints->base_width)
			% hints->width_inc;
		geo.height -= (geo.height - hints->base_height)
			% hints->height_inc;
	}

	/* Is it smaller than it wants to be? */
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		if (geo.height < hints->min_height)
			geo.height = hints->min_height;

		if (geo.width < hints->min_width)
			geo.width = hints->min_width;
	}

	/* Is it bigger than it's maximal size? */
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		if (geo.height > hints->max_height)
			geo.height = hints->max_height;
		if (geo.width > hints->max_width)
			geo.width = hints->max_width;
	}

	/* Is it outside of the physical monitor
	 * or is it overlapping with the monitor edge?
	 */
	if (geo.x < monitor.x)
		geo.x = monitor.x;
	else if (geo.x + geo.width + border*2 > monitor.width + monitor.x)
		geo.x = monitor.x + monitor.width - geo.width - border*2;
	else if (geo.x > monitor.x + monitor.width)
		geo.x = monitor.x + monitor.width - geo.width - border*2;

	if (geo.y < monitor.y)
		geo.y = monitor.y;
	else if (geo.y + geo.height + border*2 > monitor.height + monitor.y)
		geo.y = monitor.y + monitor.height - geo.height - border*2;
	else if (geo.y > monitor.y + monitor.height)
		geo.y = monitor.y + monitor.height - geo.height - border*2;


out: ;
	uint32_t values[2];
	uint16_t value_mask = 0;
	uint32_t frame_values[4];
	uint16_t frame_value_mask = 0;
	int cm = 0;
	int fm = 0;

	if (client->geometry.x != geo.x) {
		frame_value_mask |= XCB_CONFIG_WINDOW_X;
		frame_values[fm++] = geo.x;
	}
	if (client->geometry.y != geo.y) {
		frame_value_mask |= XCB_CONFIG_WINDOW_Y;
		frame_values[fm++] = geo.y;
	}
	if (client->geometry.width != geo.width) {
		value_mask |= XCB_CONFIG_WINDOW_WIDTH;
		frame_value_mask |= XCB_CONFIG_WINDOW_WIDTH;
		values[cm++] = geo.width;
		frame_values[fm++] = geo.width;
	}

	if (client->geometry.height != geo.height) {
		value_mask |= XCB_CONFIG_WINDOW_HEIGHT;
		frame_value_mask |= XCB_CONFIG_WINDOW_HEIGHT;
		values[cm++] = geo.height;
		frame_values[fm++] = geo.height;
	}

	if (cm == 0 && fm == 0) {
		PDEBUG("Geometry for 0x%x unchanged\n", client->id);
		return 0;
	}
	PDEBUG("Geometry for 0x%x updated to %d,%d %dx%d\n",
			client->id,
			geo.x, geo.y,
			geo.width, geo.height);

	client->geometry = geo;

	/* frame modified (move || resize) */
	if (fm)
		xcb_configure_window(conn, client->frame, frame_value_mask,
				frame_values);

	/* client modified (resize) */
	if (cm)
		xcb_configure_window(conn, client->id, value_mask, values);

	/* send information about positional change to client */
	if (fm > cm)
		send_configuration(client);

	return 1;
}

/*
 * Set position, geometry and attributes of a new window and show it
 * on the screen.
 */
void new_win(xcb_window_t win)
{
	if (find_client(win)) {
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
	client_t* client = create_client(win);
	if (! client)
		return;

	xcb_rectangle_t geometry = client->geometry;

	/* Get the window's colormap */
	xcb_get_window_attributes_reply_t *attr =
		xcb_get_window_attributes_reply(conn,
			xcb_get_window_attributes_unchecked(conn,
				win),
			NULL);
	if (attr) {
		client->colormap = attr->colormap;
		destroy(attr);
	}

	/* Add this window to the current workspace. */
	set_workspace(client, curws);

	/*
	 * If the client doesn't say the user specified the coordinates
	 * for the window we map it where our pointer is instead.
	 * Or to the center of the monitor out pointer is on.
	 */
	if (client->usercoord) {
#if 0
		/* hints.x,y are obsolete and often not used, in that case just use
		 x,y given in initialization */
		geometry.x = client->hints.x;
		geometry.y = client->hints.y;
#endif
		PDEBUG("User set coordinates: %d,%d\n", geometry.x, geometry.y);
	} else {
		int16_t pointx;
		int16_t pointy;

		/* Get pointer position so we can move the window to the cursor. */
		if (get_pointer(screen->root, &pointx, &pointy)) {
			geometry.x = pointx - geometry.width / 2;
			geometry.y = pointy - geometry.height / 2;
		} else {
			geometry.x = 0;
			geometry.y = 0;
		}
		PDEBUG("Coordinates not set by user. Using: %d,%d.\n",
				pointx, pointy);
	}

	/* Find the physical output this window will be on if RANDR
	   is active. */
	if (-1 != randrbase) {
		client->monitor = find_monitor_at(geometry.x, geometry.y);
		if (! client->monitor) {
			/*
			 * Window coordinates are outside all physical monitors.
			 * Choose the first screen.
			 */
			if (monlist) client->monitor = monlist->data;
		}
	}

	apply_gravity(client, &geometry);
	update_geometry(client, &geometry);

	/* Show window on screen. */
	set_default_events(client);
	show(client);

	/*
	 * Move cursor over the window so we don't lose the
	 * pointer to another window.
	 */
	if (! pointer_over_client(client))
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, win, 0, 0, 0, 0,
				client->geometry.width / 2, client->geometry.height / 2);
}

/*
 * Update local WM_NORMAL_HINTS information
 */
void icccm_update_wm_normal_hints(client_t* client)
{
	if (! client)
		return;

	xcb_size_hints_t *hints = &client->hints;

	/* zero current hints */
	memset(hints, 0, sizeof(xcb_size_hints_t));

	if (! xcb_icccm_get_wm_normal_hints_reply(conn,
				xcb_icccm_get_wm_normal_hints_unchecked(conn, client->id),
			 	hints, NULL)) {
		memset(hints, 0, sizeof(xcb_size_hints_t));
		PDEBUG("Couldn't get size hints.\n");
		return;
	}

	/*
	 * The user specified the position coordinates. Remember that so
	 * we can use geometry later.
	 */
	if (hints->flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
		client->usercoord = true;

	if (!(hints->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			&& (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {
		PDEBUG("base_size hints missing, using min_size\n");
		hints->base_width = hints->min_width;
		hints->base_height = hints->min_height;
		hints->flags |= XCB_ICCCM_SIZE_HINT_BASE_SIZE;
	} else if ((hints->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)
			&& (!(hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE))) {
		PDEBUG("min_size hints missing, using base_size\n");
		hints->min_width = hints->base_width;
		hints->min_height = hints->base_height;
		hints->flags |= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE;
	}

	if (!(hints->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC)) {
		hints->width_inc = 1;
		hints->height_inc = 1;
	} else {
		/* failsafes */
		if (hints->width_inc < 1)
			hints->width_inc = 1;
		if (hints->height_inc < 1)
			hints->height_inc = 1;
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

	/* set input focus, allow if not set */
	if (wm_hints.flags & XCB_ICCCM_WM_HINT_INPUT)
		client->allow_focus = !!(wm_hints.input);
	else
		client->allow_focus = true;
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
	client->take_focus = false;

	if (xcb_icccm_get_wm_protocols_reply(conn, cookie, &protocols, NULL)) {
		for (uint32_t i = 0; i < protocols.atoms_len; i++) {
			if (protocols.atoms[i] == icccm.wm_delete_window) {
				client->use_delete = true;
				continue;
			}
			if (protocols.atoms[i] == icccm.wm_take_focus) {
				client->take_focus = true;
				continue;
			}
		}
		xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
	}
}

/*
 * Set border color, width and event mask for window,
 * reparent etc.
 * Executed for each new handled window (unlike newwin)
 * */
client_t *create_client(xcb_window_t win)
{
	list_t *item;
	client_t *client;
	uint32_t ws;

	/* Add this window to the X Save Set. */
	xcb_change_save_set(conn, XCB_SET_MODE_INSERT, win);

	/* Remember window and store a few things about it. */
	item = list_add(&winlist);

	if (! item) {
		PERROR("create_client: Out of memory.\n");
		return NULL;
	}

	client = calloc(1, sizeof(client_t));
	if (! client) {
		PERROR("create_client: Out of memory.\n");
		return NULL;
	}

	item->data = client;

	/* Initialize client. */
	client->id = win;
	client->frame = XCB_WINDOW_NONE;

	client->monitor = NULL;
	client->usercoord = false;
	client->vertmaxed = false;
	client->fullscreen = false;
	client->take_focus = false;
	client->use_delete = false;
	client->hidden = false;
	client->ignore_unmap = false;
	client->ewmh_state_set = false;
	client->killed = 0;

	client->allow_focus = true;
	client->colormap = screen->default_colormap;

	client->winitem = item;
	for (ws = 0; ws < WORKSPACES; ws++) {
		client->wsitem[ws] = NULL;
	}

	PDEBUG("Adding window 0x%x\n", client->id);

	/* Get window geometry. */
	if (! get_geometry(client->id, &client->geometry)) {
		PDEBUG("Couldn't get geometry in initial setup of window. Reject managing.\n");
		remove_client(client);
		return NULL;
	}
	client->geometry_last = client->geometry;

	/* Create frame and reparent */
	attach_frame(client);

	/* Check if the window has _NET_WM_STATE_FULLSCREEN set
	 * (XXX check for other states as well ?)
	 */
	if (ewmh_is_fullscreen(client)) {
		toggle_fullscreen(client);
	} else {
		/* Set borders and frame extents */
		set_borders(client->frame, conf.borderwidth);
		ewmh_frame_extents(client->id, conf.borderwidth);
	}

	if (shapebase != -1) {
		/* Enable shape change notifications for client */
		xcb_shape_select_input(conn, client->id, 1);
		/* Set shape, if any */
		update_shape(client);
	}

	/* Gather ICCCM specified hints for window management */
	icccm_update_wm_hints(client);
	icccm_update_wm_normal_hints(client);
	icccm_update_wm_protocols(client);

	/* Set _NET_WM_STATE_* */
	ewmh_update_state(client);

	/* Update root window's client list */
	ewmh_update_client_list();

	/* Set WM actions allowed by the client */
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
xcb_keycode_t keysym_to_keycode(xcb_keysym_t keysym, xcb_key_symbols_t * keysyms)
{
	xcb_keycode_t *keyp;
	xcb_keycode_t key;

	/* We only use the first keysymbol, even if there are more. */
	keyp = xcb_key_symbols_get_keycode(keysyms, keysym);
	if (! keyp) {
		PERROR("wmwm: Couldn't look up key. Exiting.\n");
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
//	if (modkeys.keycodes) {
//		destroy(modkeys.keycodes);
//	}
//	modkeys = get_modkeys(MODKEY);

//	if (0 == modkeys.len) {
//		PERROR("We couldn't find any keycodes to our main modifierkey! \n");
//		return false;
//	}

	/* Now grab the rest of the keys with the MODKEY modifier. */
	for (i = KEY_LEFT; i < KEY_MAX; i++) {
		if (XK_VoidSymbol == keys[i].keysym) {
			keys[i].keycode = 0;
			continue;
		}

		keys[i].keycode = keysym_to_keycode(keys[i].keysym, keysyms);
		if (0 == keys[i].keycode) {
			/* Couldn't set up keys! */
			/* Get rid of key symbols. */
			xcb_key_symbols_free(keysyms);
			PDEBUG(".. couldn't setup keys\n");
			return false;
		}

		switch (i) {
			case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
				/* grab hjkl with extended modmask for resizing */
				xcb_grab_key(conn, 1, screen->root,
						EXTRA_MODKEY,
						keys[i].keycode,
						XCB_GRAB_MODE_ASYNC,
						XCB_GRAB_MODE_ASYNC);
				PDEBUG("Grabbing key (%d, with keycode: %d)\n",
					i, keys[i].keycode);
			default:
				xcb_grab_key(conn, 1, screen->root,
						MODKEY,
						keys[i].keycode,
						XCB_GRAB_MODE_ASYNC,
						XCB_GRAB_MODE_ASYNC);
				PDEBUG("Grabbing key (%d, with keycode: %d)\n",
					i, keys[i].keycode);

		}
	} /* for */

	/* Need this to take effect NOW! */
	xcb_flush(conn);

	/* Get rid of the key symbols table. */
	xcb_key_symbols_free(keysyms);

	PDEBUG(".. setup successful!\n");
	return true;
}

/*
 * Initialize EWMH stuff
 */
bool setup_ewmh(void)
{
 	/* get ICCCM atoms */
	icccm.wm_delete_window	= get_atom("WM_DELETE_WINDOW");
	icccm.wm_take_focus		= get_atom("WM_TAKE_FOCUS");
	icccm.wm_change_state	= get_atom("WM_CHANGE_STATE");
	icccm.wm_state			= get_atom("WM_STATE");
	icccm.wm_protocols		= get_atom("WM_PROTOCOLS");

	/* establish ewmh connection (-lxcb_ewmh) */
	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	if (! ewmh)
		return false;

	xcb_intern_atom_cookie_t *cookies = xcb_ewmh_init_atoms(conn, ewmh);
	if (! cookies)
		return false;

	if (! xcb_ewmh_init_atoms_replies(ewmh, cookies, NULL)) {
		destroy(ewmh);
		destroy(cookies);
		return false;
	}

	ewmh__NET_WM_STATE_FOCUSED = get_atom("_NET_WM_STATE_FOCUSED");

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
		ewmh->_NET_WM_STATE_MAXIMIZED_VERT,	// option
		ewmh->_NET_WM_STATE_FULLSCREEN,		// option
		ewmh->_NET_WM_STATE_HIDDEN,			// option
		ewmh__NET_WM_STATE_FOCUSED,		// option

		ewmh->_NET_WM_ALLOWED_ACTIONS,		// window
		ewmh->_NET_WM_ACTION_MAXIMIZE_VERT,	// option
		ewmh->_NET_WM_ACTION_FULLSCREEN,	// option

		ewmh->_NET_SUPPORTING_WM_CHECK,		// window
		ewmh->_NET_FRAME_EXTENTS,			// window
		ewmh->_NET_REQUEST_FRAME_EXTENTS,   // message
		ewmh->_NET_CLOSE_WINDOW,			// message
		ewmh->_NET_MOVERESIZE_WINDOW,		// message
		icccm.wm_change_state,				// message
		icccm.wm_delete_window,				// message
		icccm.wm_change_state,				// message
		icccm.wm_state,						//
		icccm.wm_protocols					//
	};

	xcb_ewmh_set_supported(ewmh, screen_number,
			sizeof(atoms)/sizeof(xcb_atom_t), atoms);

	xcb_ewmh_set_wm_name(ewmh, screen->root, 4, "wmwm");
	xcb_ewmh_set_supporting_wm_check(ewmh, screen->root, screen->root);
	xcb_ewmh_set_number_of_desktops(ewmh, screen_number, WORKSPACES);
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
	/* Get all children. */
	xcb_query_tree_reply_t *reply =
		xcb_query_tree_reply(conn, xcb_query_tree(conn, screen->root), 0);

	if (! reply)
		return false;

	int len = xcb_query_tree_children_length(reply);
	xcb_window_t *children = xcb_query_tree_children(reply);

	/* Set up all windows on this root. */
	for (int i = 0; i < len; i++) {
		xcb_get_window_attributes_reply_t *attr =
			xcb_get_window_attributes_reply(conn,
				xcb_get_window_attributes_unchecked(conn,
					children[i]),
				NULL);

		if (! attr) {
			PERROR("Couldn't get attributes for window %d.", children[i]);
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
		if (! attr->override_redirect
				&& attr->map_state == XCB_MAP_STATE_VIEWABLE) {
			client_t *client;
			if (!(client = create_client(children[i])))
				continue;
			/*
			 * Find the physical output this window will be on if
			 * RANDR is active.
			 */
			if (randrbase != -1) {
				PDEBUG("Looking for monitor on %d x %d.\n",
						client->geometry.x,
						client->geometry.y);
				client->monitor = find_monitor_at(client->geometry.x,
						client->geometry.y);
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
			update_geometry(client, NULL);

			/* save individual colormap */
			client->colormap = attr->colormap;
			/*
			 * Check if this window has a workspace set already as
			 * a WM hint.
			 *
			 */
			uint32_t ws = ewmh_get_workspace(children[i]);

			if (ws < WORKSPACES) {
				set_workspace(client, ws);
				/* If it's on our current workspace, show it, else hide it. */
				if (ws == curws)
					show(client);
				else
					hide(client);
			} else {
				/*
				 * No workspace hint or bad one. Just add it to our
				 * current workspace.
				 */
				set_workspace(client, curws);
				show(client);
			}
		}
		destroy(attr);
	}							/* for */

	 /* Set focus on any window which might be under it */
	set_input_focus(XCB_WINDOW_NONE);

	destroy(reply);
	return true;
}
void ewmh_frame_extents(xcb_window_t win, int width)
{
	uint32_t data[] = { width, width, width, width };
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, ewmh->_NET_FRAME_EXTENTS,
			XCB_ATOM_CARDINAL, 32, 4, &data);
}

/*
 * Fit frame window to shape of client window if necessary
 */
void update_shape(client_t* client)
{
	xcb_shape_query_extents_reply_t *extents;
	xcb_generic_error_t* error;

	extents = xcb_shape_query_extents_reply(conn, xcb_shape_query_extents(conn, client->id), &error);
	if (error) {
		PDEBUG("error querying shape extents for 0x%x\n", client->id);
		print_x_error(error);
		destroy(error);
		return;
	}
	/* Do we have a bounding shape, e.g. shape for the frame */
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
		get_randr();
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
void get_randr(void)
{
	xcb_randr_get_screen_resources_current_cookie_t rcookie;
	xcb_randr_get_screen_resources_current_reply_t *res;
	xcb_randr_output_t *outputs;
	int len;

	rcookie = xcb_randr_get_screen_resources_current(conn, screen->root);
	res = xcb_randr_get_screen_resources_current_reply(conn, rcookie, NULL);
	if (! res) {
		printf("No RANDR extension available.\n");
		return;
	}
	update_timestamp(res->config_timestamp);

	len = xcb_randr_get_screen_resources_current_outputs_length(res);
	outputs = xcb_randr_get_screen_resources_current_outputs(res);

	PDEBUG("Found %d outputs.\n", len);

	/* Request information for all outputs. */
	get_outputs(outputs, len, res->config_timestamp);

	destroy(res);
}

/*
 * Walk through all the RANDR outputs (number of outputs == len) there
 * was at time timestamp.
 */
void get_outputs(xcb_randr_output_t * outputs, int len,
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
		PERROR("No outputs (%d) at all, what should we do now?\n", len);
		return;
	}

	ocookie = calloc(len, sizeof(xcb_randr_get_output_info_cookie_t));
	if (ocookie == NULL) {
		PERROR("Out of memory.\n");
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

		const int name_len = xcb_randr_get_output_info_name_length(output);

		if (name_len <= 0) {
			name = NULL;
		} else {
			if (!(name = calloc(name_len + 1, sizeof(char)))) {
				perror("wmwm outputs");
				cleanup(1);
			}
			strncpy(name, (char*)xcb_randr_get_output_info_name(output), name_len);
		}
		PDEBUG("Name: \"%s\" (len: %d)\n", name, name_len);
		PDEBUG("id: %d\n", outputs[i]);
		PDEBUG("Size: %d x %d mm.\n", output->mm_width, output->mm_height);

		if (XCB_NONE != output->crtc) {
			icookie = xcb_randr_get_crtc_info(conn, output->crtc, timestamp);
			crtc = xcb_randr_get_crtc_info_reply(conn, icookie, NULL);
			if (! crtc) {
				if (name) destroy(name);
				destroy(output);
				destroy(ocookie);
				return;
			}
			PDEBUG("CRTC: at %d, %d, size: %d x %d.\n", crtc->x, crtc->y,
					crtc->width, crtc->height);

			/* Check if it's a clone. */
			clonemon = find_clones(outputs[i], crtc->x, crtc->y);
			if (clonemon) {
				PDEBUG
					("Monitor %s, id %d is a clone of %s, id %d. Skipping.\n",
					 name, outputs[i], clonemon->name, clonemon->id);
				if (name) destroy(name);
				destroy(crtc);
				destroy(output);
				continue;
			}

			/* Do we know this monitor already? */
			if (!(mon = find_monitor(outputs[i]))) {
				PDEBUG("Monitor unknown, adding to list.\n");
				add_monitor(outputs[i], name,
						crtc->x, crtc->y,
						crtc->width, crtc->height);
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
			if ((mon = find_monitor(outputs[i]))) {
				list_t *item;
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
						if (! (client->monitor->item->next)) {
							if (! monlist) {
								client->monitor = NULL;
							} else {
								client->monitor = monlist->data;
							}
						} else {
							client->monitor =
								client->monitor->item->next->data;
						}

						update_geometry(client, NULL);
					}
				} /* for */

				/* It's not active anymore. Forget about it. */
				del_monitor(mon);
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
	 */
	for (list_t *item = winlist; item; item = item->next) {
		client = item->data;
		if (client->monitor == monitor) {
			update_geometry(client, NULL);
		}
	}							/* for */
}

monitor_t *find_monitor(xcb_randr_output_t id)
{
	monitor_t *mon;

	for (list_t *item = monlist; item; item = item->next) {
		mon = item->data;
		if (id == mon->id) {
			PDEBUG("find_monitor: Found it. Output ID: %d\n", mon->id);
			return mon;
		}
	}

	return NULL;
}

monitor_t *find_clones(xcb_randr_output_t id, int16_t x, int16_t y)
{
	monitor_t *clonemon;

	for (list_t *item = monlist; item; item = item->next) {
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

monitor_t *find_monitor_at(int16_t x, int16_t y)
{
	monitor_t* mon;

	for (list_t* item = monlist; item; item = item->next) {
		mon = item->data;
		PDEBUG("Monitor %s: x, y: %d--%d, %d--%d.\n",
				mon->name,
				mon->x, mon->x + mon->width, mon->y, mon->y + mon->height);

		PDEBUG("Is %d,%d between them?\n", x, y);

		if (x >= mon->x && x <= mon->x + mon->width
				&& y >= mon->y && y <= mon->y + mon->height) {
			PDEBUG("find_monitor_at: Found it. Output ID: %d, name %s\n",
					mon->id, mon->name);
			return mon;
		}
	}

	return NULL;
}

void del_monitor(monitor_t *mon)
{
	PDEBUG("Deleting output %s.\n", mon->name);
	destroy(mon->name);
	list_erase(&monlist, NULL, mon->item);
}

monitor_t *add_monitor(xcb_randr_output_t id, char *name,
		uint32_t x, uint32_t y, uint16_t width, uint16_t height)
{
	list_t *item;
	monitor_t *mon;

	if (! (item = list_add(&monlist))) {
		perror("wmwm add_monitor");
		return NULL;
	}

	if (! (mon = calloc(1, sizeof(monitor_t)))) {
		perror("wmwm add_monitor");
		return NULL;
	}

	item->data = mon;

	if (name) {
		mon->name = calloc(strlen(name) + 1, sizeof(char));
		strcpy(mon->name, name);
	} else {
		mon->name = NULL;
	}

	mon->id = id;
	mon->x = x;
	mon->y = y;
	mon->width = width;
	mon->height = height;
	mon->item = item;

	return mon;
}

void raise_client(client_t *client)
{
	uint32_t values[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, client->frame, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

/*
 * Set window client to either top or bottom of stack depending on
 * where it is now.
 */
void raise_or_lower_client(client_t *client)
{
	uint32_t values[] = { XCB_STACK_MODE_OPPOSITE };
	xcb_drawable_t win;

	if (! client)
		return;

	win = client->frame;

	xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

/* Change focus to next in window ring. */
void focus_next(void)
{
	client_t *client = NULL;

	if (! wslist[curws]) {
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
	if (! focuswin || ! focuswin->wsitem[curws]) {
		PDEBUG("Focusing first in list: @%p\n", (void*)wslist[curws]);
		client = wslist[curws]->data;
	} else {
		if (! focuswin->wsitem[curws]->next) {
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

		if (! pointer_over_client(client)) {
			xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->frame, 0, 0, 0, 0,
					client->geometry.width / 2, client->geometry.height / 2);
		}
		set_focus(client);
	}
}

/* Mark window win as unfocused. */
void unset_focus()
{
	PDEBUG("unset_focus() focuswin = 0x%x\n", focuswin ? focuswin->id : 0);
	if (! focuswin)
		return;

	client_t *client = focuswin;
	focuswin = NULL;
	ewmh_update_state(client);
	/* Set new border color. */
	update_bordercolor(client);
}

/*
 * Find client with client->id win or client->frame
 * in global window list.
 *
 * Returns client pointer or NULL if not found.
 */
client_t *find_clientp(xcb_drawable_t win)
{
	if (win == XCB_WINDOW_NONE)
		return NULL;

	if (win == screen->root)
		return NULL;

	if (focuswin && (focuswin->id == win || focuswin->frame == win))
		return focuswin;

	for (list_t *item = winlist; item; item = item->next) {
		client_t *client = item->data;
		if (win == client->id) {
			return client;
		} else if (win == client->frame) {
			return client;
		}
	}
	return NULL;
}

/*
 * Find client with client->id win in global window list.
 *
 * Returns client pointer or NULL if not found.
 */
client_t *find_client(xcb_drawable_t win)
{
	if (win == XCB_WINDOW_NONE)
		return NULL;

	if (win == screen->root)
		return NULL;

	if (focuswin && focuswin->id == win)
		return focuswin;

	for (list_t *item = winlist; item; item = item->next) {
		client_t *client = item->data;
		if (win == client->id)
			return client;
	}
	return NULL;
}

/* Set focus on window client. */
void set_focus(client_t *client)
{
	PDEBUG("set_focus: client = 0x%x (focuswin = 0x%x)\n",
			client ? client->id : 0, focuswin ? focuswin->id : 0);

	/* If client is NULL, focus on root */
	if (! client) {
		PDEBUG("set_focus: client was NULL! \n");

		/* install default colormap */
		xcb_install_colormap(conn, screen->default_colormap);

		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				XCB_INPUT_FOCUS_POINTER_ROOT, get_timestamp());
		xcb_ewmh_set_active_window(ewmh, screen_number, 0);

		/* mark current focuswin as no longer focused */
		if (focuswin) {
			client = focuswin;
			focuswin = NULL;
			ewmh_update_state(client);
		}

		return;
	}

	/* Don't bother focusing on the same window that already has focus */
	if (client == focuswin)
		return;

	/* Don't bother if the client is not on this workspace */
	if (! client->wsitem[curws])
		return;

	/* set input focus (preferred) or
	 * send WM_TAKE_FOCUS
	 */
	if (client->allow_focus) {
		PDEBUG("xcb_set_input_focus: 0x%x\n", client->id);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				client->id, get_timestamp());
	} else if (client->take_focus) {
		send_client_message(client->id, icccm.wm_take_focus);
	}

	/* Unset last focus. */
	if (focuswin)
		unset_focus();

	/* Remember the new window as the current focused window. */
	focuswin = client;

	/* Set new border color. */
	update_bordercolor(client);

	/* Install client's colormap */
	xcb_install_colormap(conn, client->colormap);

	/* Set active window ewmh-hint */
	xcb_ewmh_set_active_window(ewmh, screen_number, client->id);

	/* Mark window as focuswin etc. */
	ewmh_update_state(client);
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

/*
 * Resize window client in direction direction.
 */
void resize_step(client_t *client, step_direction_t direction)
{
	int step_x = MOVE_STEP;
	int step_y = MOVE_STEP;

	if (! client)
		   return;

	xcb_rectangle_t geometry = client->geometry;

	if (client->fullscreen) {
		/* Can't resize a fully maximized window. */
		return;
	}

	raise_client(client);

	if (client->hints.width_inc > 1)
		step_x = client->hints.width_inc;

	if (client->hints.height_inc > 1)
		step_y = client->hints.height_inc;

	switch (direction) {
		case step_left:
			geometry.width  -= step_x;
			break;

		case step_up:
			geometry.height += step_y;
			break;

		case step_down:
			geometry.height -= step_y;
			break;

		case step_right:
			geometry.width  += step_x;
			break;
	}							/* switch direction */

	if (! update_geometry(client, &geometry))
		return;

	/* If this window was vertically maximized, remember that it isn't now. */
	if (client->vertmaxed) {
		client->vertmaxed = false;
		ewmh_update_state(client);
	}

	/* Place pointer in center if the it is not over client anymore */
	if (! pointer_over_client(client)) {
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->frame, 0, 0, 0, 0,
				client->geometry.width / 2, client->geometry.height / 2);
	}
}

/*
 * Move window win as a result of pointer motion to coordinates
 * rel_x,rel_y.
 */
void mouse_move(client_t *client, int rel_x, int rel_y)
{
	xcb_rectangle_t geo = client->geometry;
	geo.x = rel_x; geo.y = rel_y;
	update_geometry(client, &geo);
}

void mouse_resize(client_t *client, int rel_x, int rel_y)
{
	xcb_rectangle_t geo = client->geometry;

	/* Don't resize to negative values */
	if (rel_x > geo.x)
		geo.width = rel_x - geo.x;
	if (rel_y > geo.y)
		geo.height = rel_y - geo.y;

	if (! update_geometry(client, &geo))
		return; // nothing changed

	/* If this window was vertically maximized, remember that it isn't now. */
	if (client->vertmaxed) {
		client->vertmaxed = false;
		ewmh_update_state(client);
	}
}

void move_step(client_t *client, step_direction_t direction)
{
	int16_t start_x;
	int16_t start_y;

	if (! client)
		return;

	xcb_rectangle_t geo = client->geometry;

	if (client->fullscreen) {
		/* We can't move a fully maximized window. */
		return;
	}

	/* Save pointer position so we can warp pointer here later. */
	if (! get_pointer(client->id, &start_x, &start_y)) {
		return;
	}

	raise_client(client);

	switch (direction) {
		case step_left:
			geo.x -= MOVE_STEP;
			break;

		case step_down:
			geo.y += MOVE_STEP;
			break;

		case step_up:
			geo.y -= MOVE_STEP;
			break;

		case step_right:
			geo.x += MOVE_STEP;
			break;
	}							/* switch direction */

	if (! update_geometry(client, &geo))
		return;

	/*
	 * If the pointer was inside the window to begin with, move
	 * pointer back to where it was, relative to the window.
	 */
	if (start_x > 0 - conf.borderwidth
			&& start_x < client->geometry.width + conf.borderwidth
			&& start_y > 0 - conf.borderwidth
			&& start_y < client->geometry.height + conf.borderwidth) {
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->frame, 0, 0, 0, 0,
				start_x, start_y);
	}
}

void update_bordercolor(client_t *client)
{
	uint32_t color[1];
	if (! client)
		return;
	if (client == focuswin)
		color[0] = conf.focuscol;
	else
		color[0] = conf.unfocuscol;

	xcb_change_window_attributes(conn, client->frame,
		XCB_CW_BORDER_PIXEL, color);
}

void set_borders(xcb_drawable_t win, int width)
{
	uint32_t values[1];
	uint32_t mask = XCB_CONFIG_WINDOW_BORDER_WIDTH;

	PDEBUG("Setting borders (%d) to 0x%x\n", width, win);
	values[0] = width;

	xcb_configure_window(conn, win, mask, &values[0]);
}

void unmax(client_t *client)
{
	if (! client)
		   return;

	if (!client->fullscreen && !client->vertmaxed) {
		PDEBUG("unmax: client was not maxed!\n");
		return;
	}

	/* Restore geometry. */
	client->fullscreen = client->vertmaxed = false;
	update_geometry(client, &(client->geometry_last));

	set_borders(client->frame, conf.borderwidth);
	ewmh_frame_extents(client->id, conf.borderwidth);

	/* Warp pointer to window or we might lose it. */
	if (! pointer_over_client(client)) {
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, client->frame, 0, 0, 0, 0,
				client->geometry.width / 2, client->geometry.height / 2);
	}
}

void toggle_fullscreen(client_t *client)
{
	if (! client)
	   return;

	xcb_rectangle_t monitor;
	get_monitor_geometry(client->monitor, &monitor);

	/*
	 * Check if maximized already. If so, revert to stored
	 * geometry.
	 */
	if (client->vertmaxed) {
		unmax(client);
	} else if (client->fullscreen) {
		PDEBUG("<> Client maximized, unmaximizing\n");
		unmax(client);
		ewmh_update_state(client);
		return;
	}
	PDEBUG("<> Client unmaximized, maximizing!\n");

	client->fullscreen = true;

	client->geometry_last = client->geometry;

	/* Remove borders. */
	set_borders(client->frame, 0);
	ewmh_frame_extents(client->id, 0);
	update_geometry(client, &monitor);
	ewmh_update_state(client);

	raise_client(client);
}

void toggle_vertical(client_t *client)
{
	xcb_rectangle_t monitor;

	if (! client)
	   return;

	get_monitor_geometry(client->monitor, &monitor);

	/*
	 * Check if maximized already. If so, revert to stored geometry.
	 */
	if (client->fullscreen) {
		unmax(client);
	} else if (client->vertmaxed) {
		unmax(client);
		ewmh_update_state(client);
		return;
	}

	/* Raise first. Pretty silly to maximize below something else. */
	raise_client(client);

	client->geometry_last = client->geometry;

	monitor.x     = client->geometry.x;
	monitor.width = client->geometry.width;

	client->vertmaxed = true;

	/* Move to top of screen and resize. */
	if (! update_geometry(client, &monitor))
		return;

	/* Remember that this client is vertically maximized. */
	ewmh_update_state(client);
}

void set_default_events(client_t *client)
{
	const uint32_t	mask = XCB_CW_EVENT_MASK;
	const uint32_t	values[] = { DEFAULT_FRAME_EVENTS };
	xcb_change_window_attributes(conn, client->frame, mask, values);
}

void set_hidden_events(client_t *client)
{
	const uint32_t	mask = XCB_CW_EVENT_MASK;
	const uint32_t	values[] = { HIDDEN_FRAME_EVENTS };
	xcb_change_window_attributes(conn, client->frame, mask, values);
}

/* show client */
void show(client_t *client)
{
	uint32_t data[] = { XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE };

	/* Map window and declare normal */
	xcb_map_window(conn, client->id);
	xcb_map_window(conn, client->frame);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->id,
			icccm.wm_state, icccm.wm_state, 32, 2, data);

	client->hidden = false;
	ewmh_update_state(client);
}

/* send window into iconic mode and hide */
void hide(client_t *client)
{
	uint32_t data[] = { XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE };

	/*
	 * Unmap window and declare iconic.
	 * Set ignore_unmap not to remove the client.
	 */
	client->ignore_unmap = true;

	/* ICCCM 4.1.4
	 * Reparenting window managers must unmap the client's window
	 * when it is in the Iconic state, even if an ancestor window
	 * being unmapped renders the client's window unviewable.
	 */
	xcb_unmap_window(conn, client->frame);
	xcb_unmap_window(conn, client->id);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->id,
			icccm.wm_state, icccm.wm_state, 32, 2, data);

	client->hidden = true;
	ewmh_update_state(client);
}

/* Forget everything about client client. */
void remove_client(client_t *client)
{
	PDEBUG("remove_client: forgetting about win 0x%x\n", client->id);

	xcb_generic_error_t *error = NULL;

	/* set_focus ? XXX */
	if (focuswin == client)
		focuswin = NULL;
	if (lastfocuswin == client)
		lastfocuswin = NULL;

	if (client->frame != XCB_WINDOW_NONE) {
		error = xcb_request_check(conn,
				xcb_reparent_window_checked(conn, client->id, screen->root, 0, 0));
		xcb_destroy_window(conn, client->frame);
	}

	/* remove from all workspaces */
	set_workspace(client, WORKSPACE_NONE);

	/* check if the window is already gone */
	if (! error || error->error_code != XCB_WINDOW)
		xcb_change_save_set(conn, XCB_SET_MODE_DELETE, client->id);
	if (error)
		destroy(error);

	/* Remove from global window list. */
	list_erase(&winlist, NULL, client->winitem);
	ewmh_update_client_list();
}

/*
 * Reparent window
 *
 * also install listening-events to parent and children
 * this does not check if there is already a parent
 */
void attach_frame(client_t *client)
{
	/* mask and values for frame window */
	uint32_t	mask = XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;

	uint32_t	values[3] = { conf.unfocuscol, 1, HIDDEN_FRAME_EVENTS };

	const xcb_rectangle_t *geo = &(client->geometry);

	/* Create new frame window */
	client->frame = xcb_generate_id(conn);
	xcb_create_window(conn, screen->root_depth, client->frame,
			screen->root,
			geo->x, geo->y,
			geo->width, geo->height,
			client->fullscreen ? 0 : conf.borderwidth,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			XCB_COPY_FROM_PARENT,
			mask, values);

	/* set client window borderless */
	set_borders(client->id, 0);

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

bool get_pointer(xcb_drawable_t win, int16_t *x, int16_t *y)
{
	xcb_query_pointer_reply_t *pointer;

	pointer = xcb_query_pointer_reply(conn,
			xcb_query_pointer_unchecked(conn, win),
			0);

	if (! pointer) {
		return false;
	}

	*x = pointer->win_x;
	*y = pointer->win_y;

	destroy(pointer);

	return true;
}

bool get_geometry(xcb_drawable_t win, xcb_rectangle_t *geometry)
{
	xcb_get_geometry_reply_t *geom;

	geom = xcb_get_geometry_reply(conn,
			xcb_get_geometry_unchecked(conn, win),
			NULL);

	if (! geom)
		return false;

	geometry->x = geom->x;
	geometry->y = geom->y;
	geometry->width = geom->width;
	geometry->height = geom->height;

	destroy(geom);

	return true;
}

/* Move the focuswin to edges */
void warp_focuswin(step_direction_t direction)
{
	int16_t pointx;
	int16_t pointy;

	if (! focuswin || focuswin->fullscreen)
		return;

	xcb_rectangle_t mon;
	xcb_rectangle_t geo = focuswin->geometry;

	get_monitor_geometry(focuswin->monitor, &mon);

	raise_client(focuswin);

	if (!get_pointer(focuswin->id, &pointx, &pointy))
		return;

	if (direction & step_left)
		geo.x = mon.x;
	if (direction & step_right)
		geo.x = mon.x + mon.width - (geo.width + conf.borderwidth * 2);
	if (direction & step_up)
		geo.y = mon.y;
	if (direction & step_down)
		geo.y = mon.y + mon.height - (geo.height + conf.borderwidth * 2);

	if (update_geometry(focuswin, &geo))
		xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->frame,
				0, 0, 0, 0, pointx, pointy);
}

/* Inform client's window about esp. where it is.
 * Reparented windows get moved via their frame window, they do not
 * move by themselves, so we send a synthetic configure_event with
 * the new absolut position.
 */
void send_configuration(client_t *client)
{
	xcb_configure_notify_event_t ev = {
		.response_type = XCB_CONFIGURE_NOTIFY,
		.sequence = 0,
		.event = client->id,
		.window = client->id,
		.above_sibling = XCB_NONE,
		.x = client->geometry.x,
		.y = client->geometry.y,
		.width = client->geometry.width,
		.height = client->geometry.height,
		.border_width = 0,
		.override_redirect = 0
	};
	xcb_send_event(conn, false, client->id,
			XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
}

void send_client_message(xcb_window_t window, xcb_atom_t atom)
{
	PDEBUG("send client message %s (%d) to 0x%x\n", get_atomname(atom), atom, window);

	xcb_client_message_event_t ev = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = window,
		.type = icccm.wm_protocols, // ewmh.WM_PROTOCOLS available
		.data.data32 = {atom, get_timestamp()}
	};
	xcb_send_event(conn, false, window,
			XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
}

void delete_win(client_t* client)
{
	if (! client)
		return;

	if (client->use_delete && client->killed++ < 3) {
		/* WM_DELETE_WINDOW message */
		send_client_message(client->id, icccm.wm_delete_window);
		PDEBUG("delete_win - 0x%x (send_client_message #%d)\n", client->id,
				client->killed);
	} else {
		/* WM_DELETE_WINDOW either NA or failed 3 times  */
		PDEBUG("delete_win - 0x%x (kill_client)\n", client->id);
		xcb_kill_client(conn, client->id);
	}
}

/* Move focus window to previous screen */
void prev_screen(void)
{
	list_t *item;

	if (! focuswin || ! focuswin->monitor)
		return;

	item = focuswin->monitor->item->prev;

	if (! item)
		return;

	focuswin->monitor = item->data;

	raise_client(focuswin);
	update_geometry(focuswin, NULL);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->frame,
			0, 0, 0, 0, 0, 0);
}

/* Move focus window to next screen */
void next_screen(void)
{
	list_t *item;

	if (! focuswin || ! focuswin->monitor)
		return;

	item = focuswin->monitor->item->next;

	if (! item)
		return;

	focuswin->monitor = item->data;

	raise_client(focuswin);
	update_geometry(focuswin, NULL);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->frame,
			0, 0, 0, 0, 0, 0);
}

/* Helper function to configure a window. */
void configure_win(xcb_window_t win, uint16_t mask, winconf_t wc)
{
	uint32_t values[7];
	int i = 0;

	uint16_t new_mask = 0;

	if (mask & XCB_CONFIG_WINDOW_X) {
		new_mask |= XCB_CONFIG_WINDOW_X;
		values[i++] = wc.x;
	}

	if (mask & XCB_CONFIG_WINDOW_Y) {
		new_mask |= XCB_CONFIG_WINDOW_Y;
		values[i++] = wc.y;
	}

	if (mask & XCB_CONFIG_WINDOW_WIDTH) {
		new_mask |= XCB_CONFIG_WINDOW_WIDTH;
		values[i++] = wc.width;
	}

	if (mask & XCB_CONFIG_WINDOW_HEIGHT) {
		new_mask |= XCB_CONFIG_WINDOW_HEIGHT;
		values[i++] = wc.height;
	}

	if (mask & XCB_CONFIG_WINDOW_SIBLING) {
		new_mask |= XCB_CONFIG_WINDOW_SIBLING;
		values[i++] = wc.sibling;
	}

	if (mask & XCB_CONFIG_WINDOW_STACK_MODE) {
		new_mask |= XCB_CONFIG_WINDOW_STACK_MODE;
		values[i++] = wc.stackmode;
	}

	if (i > 0)
		xcb_configure_window(conn, win, new_mask, values);
}

void events(void)
{
	xcb_generic_event_t *ev = NULL;

	struct pollfd in;			/* poll struct with X fd */

	/* Get the file descriptor so we can do poll() on it. */
	in.fd = xcb_get_file_descriptor(conn);
	in.events = POLLIN;

	if (in.fd == -1) {
		PERROR("Could not connect to xcb file descriptor\n");
		cleanup(1);
	}

	/* Initial precautios flush */
	xcb_flush(conn);

	for (sigcode = 0; sigcode == 0;) {
		/*
		 * poll() for incoming events, then use xcb_poll_for_event()
		 * to get the pending events.
		 *
		 * We do it this way instead of xcb_wait_for_event() since
		 * poll() will return if we were interrupted by a signal.
		 *
		 */
		if (poll(&in, 1, -1) == -1) {
			/* We received a signal. Break out of loop. */
			if (errno == EINTR)
				break;
			perror("wmwm poll()");
			cleanup(1);
		}

		/* Get and process next event */
		while ((ev = xcb_poll_for_event(conn))) {
			const uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(ev);
			PDEBUG("  | %s (%d, handled: %d)\n",
					xcb_event_get_label(response_type),
					response_type,
					handler[response_type] ? 1 : 0);

			/* check for RANDR, SHAPE */
			if (randrbase != -1 && response_type ==
						(randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
				PDEBUG("RANDR screen change notify. Checking outputs.\n");
				get_randr();
			} else if (shapebase != -1
					&& response_type == shapebase + XCB_SHAPE_NOTIFY) {
				xcb_shape_notify_event_t *sev =
					(xcb_shape_notify_event_t*) ev;

				set_timestamp(sev->server_time);

				PDEBUG("SHAPE notify (win: 0x%x, shaped: %d)\n",
						sev->affected_window, sev->shaped);
				if (sev->shaped) {
					client_t* client = find_client(sev->affected_window);
					if (client)
						update_shape(client);
				}
			} else if (handler[response_type]) {
				handler[response_type](ev);
			}
			destroy(ev);
		}

		/* Flush after we have handled all queued events */
		xcb_flush(conn);

		/*
		 * Check if we have an unrecoverable connection error,
		 * like a disconnected X server.
		 */
		if (xcb_connection_has_error(conn))
			cleanup(1);
	}
	PDEBUG("got signal, bailing out!");
}

/* Generic Xerror printer */
void print_x_error(xcb_generic_error_t *e)
{
	PERROR("wmwm: X error = %s - %s (code: %d, op: %d/%d res: 0x%x seq: %d, fseq: %d)\n",
		xcb_event_get_error_label(e->error_code),
		xcb_event_get_request_label(e->major_code),
		e->error_code,
		e->major_code,
		e->minor_code,
		e->resource_id,
		e->sequence,
		e->full_sequence);
}


/*
 * Event handlers
 */

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
	const xcb_property_notify_event_t* e =
	   	(xcb_property_notify_event_t*)ev;
	client_t *client = find_client(e->window);

	update_timestamp(e->time);

	if (! client)
		return;

	PDEBUG("0x%x notifies changed atom %s (%d)\n", e->window, get_atomname(e->atom), e->atom);

	switch (e->atom) {
		case XCB_ATOM_WM_HINTS:
			icccm_update_wm_hints(client);
			break;
		case XCB_ATOM_WM_NORMAL_HINTS:
			icccm_update_wm_normal_hints(client);
			break;
		default:
			if (e->atom == icccm.wm_protocols)
				icccm_update_wm_protocols(client);
			/*else if (e->atom == ewmh->_NET_WM_STATE) {
				PDEBUG("Atom was _NET_WM_STATE, this shall not happen!\n");
			} */
			break;
	}
}

void handle_colormap_notify(xcb_generic_event_t *ev)
{
	xcb_colormap_notify_event_t *e = (xcb_colormap_notify_event_t*) ev;

	client_t* c;
	/* colormap has changed (not un/-installed) */
	if (e->_new && (c = find_client(e->window))) {
		c->colormap = e->colormap;
		if (c == focuswin)
			xcb_install_colormap(conn, e->colormap);
	}
}

void handle_button_press(xcb_generic_event_t* ev)
{
	xcb_button_press_event_t *e = (xcb_button_press_event_t *) ev;

	update_timestamp(e->time);

	/* Check if the button is awaited */
	switch (e->detail) {
		case 1: case 2: case 3: break;
		default: return;
	}

	if (e->child == XCB_WINDOW_NONE) {
		/* Mouse click on root window. Start programs? */
		switch (e->detail) {
			case 1:	/* Left Mouse button */
				start(MOUSE1);
				break;

			case 2:	/* Middle mouse button */
				start(MOUSE2);
				break;

			case 3:	/* Right mouse button */
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
	if (! focuswin
			|| (focuswin->frame != e->child && focuswin->id != e->child)) {
		PDEBUG("Somehow in the wrong window?\n");
		return;
	}

	/*
	 * If middle button was pressed, raise window or lower
	 * it if it was already on top.
	 */
	if (e->detail == 2) {
		raise_or_lower_client(focuswin);
		return;
	}

	/* We're moving or resizing, ignore when maxed. */
	if (focuswin->fullscreen)
		return;

	/*
	 * Get and save pointer position inside the window
	 * so we can keep our pointer fixed while moving.
	 */
	if (! get_pointer(focuswin->frame, &mode_x, &mode_y)) {
		PDEBUG("Could not get pointer?\n");
		return;
	}

	raise_client(focuswin);

	switch (e->detail) {
		case 1: /* left button: move */
			set_mode(mode_move);
			break;
		case 3: /* right button: resize */
			set_mode(mode_resize);
			/* Warp pointer to lower right. Ignore gravity.  */
			xcb_warp_pointer(conn, XCB_WINDOW_NONE, focuswin->frame, 0,
					0, 0, 0, focuswin->geometry.width,
					focuswin->geometry.height);
			break;
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
			| XCB_EVENT_MASK_POINTER_MOTION,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			screen->root, XCB_NONE, get_timestamp());
	/* Grab the mouse now! */
	xcb_flush(conn);
}

void handle_motion_notify(xcb_generic_event_t *ev)
{

	xcb_input_device_motion_notify_event_t *e =
		(xcb_input_device_motion_notify_event_t*)ev;

	update_timestamp(e->time);

	/*
	 * We can't do anything if we don't have a focused window
	 * or if it's fully maximized.
	 */
	if (! focuswin || focuswin->fullscreen)
		return;

	/*
	 * Our pointer is moving and since we even get this event
	 * we're either resizing or moving a window.
	 */
	if (is_mode(mode_move))
		mouse_move(focuswin, e->root_x - mode_x, e->root_y - mode_y);
	else if (is_mode(mode_resize))
		mouse_resize(focuswin, e->root_x, e->root_y);
#if DEBUG
	else
		PDEBUG("Motion event when we're not moving our resizing! \n");
#endif
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
	if (! focuswin) {
		PDEBUG("No focused window when finished moving or resizing!");
		/*
		 * We don't seem to have a focused window! Just
		 * ungrab and reset the mode.
		 */
	}

	xcb_ungrab_pointer(conn, get_timestamp());
	xcb_flush(conn);	/* Important! */

	set_mode(mode_nothing);

	/*
	 * We will get an EnterNotify if the pointer just happens to be
	 * on top of another window when we ungrab the pointer,
	 * but it's not a normal enter event, so we can ignore it.
	 */
}

key_enum_t key_from_keycode(xcb_keycode_t keycode)
{
	for (key_enum_t i = KEY_LEFT; i < KEY_MAX; i++) {
		if (keys[i].keycode && keycode == keys[i].keycode)
			return i;
	}
	return KEY_MAX;
}

void handle_key_press(xcb_generic_event_t *ev)
{
	xcb_key_press_event_t *e = (xcb_key_press_event_t*)ev;

	update_timestamp(e->time);

	key_enum_t key = key_from_keycode(e->detail);

	/* First finish tabbing around. Then deal with the next key. */
	if (is_mode(mode_tab) && key != KEY_NEXT)
		finish_tab();

	/* TODO impossible -> grabbed keys ? */
	/* XXX: This happens for Meta_L/Alt_L */
	if (key == KEY_MAX) {
		PERROR("Unknown key pressed (state %d - key %d).\n", e->state, e->detail);

		/*
		 * We don't know what to do with this key. Send this key press
		 * event to the focused window.
		 */
		xcb_send_event(conn, false, XCB_SEND_EVENT_DEST_ITEM_FOCUS,
				XCB_EVENT_MASK_NO_EVENT, (char *) e);
		return;
	}

	switch (e->state) {
		/* CTRL + META + SHIFT */
		case EXTRA_MODKEY:
			switch (key) {
				case KEY_LEFT:		/* left */
					resize_step(focuswin, step_left);
					break;

				case KEY_DOWN:		/* down */
					resize_step(focuswin, step_up);
					break;

				case KEY_UP:		/* up */
					resize_step(focuswin, step_down);
					break;

				case KEY_RIGHT:		/* right */
					resize_step(focuswin, step_right);
					break;

				default:
					break;
			}
			break;

		/* CTRL + META */
		case MODKEY:
			switch (key) {
				case KEY_NEXT:			/* tab */
					focus_next();
					break;

				case KEY_TERMINAL:		/* return */
					start(conf.terminal);
					break;

				case KEY_MENU:		/* m */
					start(conf.menu);
					break;

				case KEY_LEFT:		/* left */
					move_step(focuswin, step_left);
					break;

				case KEY_DOWN:		/* down */
					move_step(focuswin, step_down);
					break;

				case KEY_UP:		/* up */
					move_step(focuswin, step_up);
					break;

				case KEY_RIGHT:		/* right */
					move_step(focuswin, step_right);
					break;

				case KEY_VERTICAL:		/* v */
					toggle_vertical(focuswin);
					break;

				case KEY_RAISE_LOWER:		/* r */
					raise_or_lower_client(focuswin);
					break;

				case KEY_MAXIMIZE:		/* x */
					toggle_fullscreen(focuswin);
					break;

				case KEY_WS1:
					change_workspace(0);
					break;

				case KEY_WS2:
					change_workspace(1);
					break;

				case KEY_WS3:
					change_workspace(2);
					break;

				case KEY_WS4:
					change_workspace(3);
					break;

				case KEY_WS5:
					change_workspace(4);
					break;

				case KEY_WS6:
					change_workspace(5);
					break;

				case KEY_WS7:
					change_workspace(6);
					break;

				case KEY_WS8:
					change_workspace(7);
					break;

				case KEY_WS9:
					change_workspace(8);
					break;

				case KEY_WS10:
					change_workspace(9);
					break;

				case KEY_TOPLEFT:
					warp_focuswin(step_up   | step_left);
					break;

				case KEY_TOPRIGHT:
					warp_focuswin(step_up   | step_right);
					break;

				case KEY_BOTTOMLEFT:
					warp_focuswin(step_down | step_left);
					break;

				case KEY_BOTTOMRIGHT:
					warp_focuswin(step_down | step_right);
					break;

				case KEY_KILL:
					delete_win(focuswin);
					break;

				case KEY_PREVSCR:
					prev_screen();
					break;

				case KEY_NEXTSCR:
					next_screen();
					break;

				case KEY_ICONIFY:
					if (conf.allowicons) {
						/* hide and remove from workspace list */
						set_hidden_events(focuswin);
						hide(focuswin);
						set_workspace(focuswin, WORKSPACE_NONE);
					}
					break;
				default:
					break;
			} /* switch CTRL + META */
		default:
			break;
	}
}

/* is that even neccessary, because I get the same for keypress and key release XXX ? */
void handle_key_release(xcb_generic_event_t *ev)
{
	xcb_key_release_event_t *e = (xcb_key_release_event_t *) ev;
	update_timestamp(e->time);

	/* if we were tabbing, finish */
	if (is_mode(mode_tab) && key_from_keycode(e->detail) != KEY_NEXT)
		finish_tab();
}

void handle_enter_notify(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *) ev;

	update_timestamp(e->time);

	PDEBUG ("Enter notify event: win 0x%x, child 0x%x, detail %d, mode %d\n",
		 e->event, e->child, e->detail, e->mode);

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
				|| e->mode != XCB_NOTIFY_MODE_UNGRAB))
		return;

	if (e->event == screen->root) {
		/* root window entered */
		if (! focuswin) {
			/* No window has the focus, it might be reverted to 0x0,
			 * so we set it on under a window the cursor.
			 */
			set_input_focus(XCB_WINDOW_NONE);
		}
		return;
	}

	/*
	 * If we're entering the same window we focus now
	 * or cannot find a client, then don't bother focusing.
	 */
	client_t *client = find_clientp(e->event);
	if (! client || client == focuswin)
		return;

	/*
	 * Set focus to the window we just
	 * entered if we can find it among the windows we
	 * know about. If not, just keep focus in the old
	 * window.
	 */
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
			list_to_head(&wslist[curws],
					focuswin->wsitem[curws]);
			lastfocuswin = NULL;
		}

		list_to_head(&wslist[curws],
				client->wsitem[curws]);
	} /* if not tabbing */

	set_focus(client);
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
		PDEBUG("Notify event for root!\n");
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
				 * screen get_randr() when we receive an
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
	if (! (client = find_client(e->window))) {
		PDEBUG("We don't know about this window yet.\n");

		/* Unmapped window. Just pass all options except border width. */

		wc.x = e->x;
		wc.y = e->y;
		wc.width = e->width;
		wc.height = e->height;
		wc.sibling = e->sibling;
		wc.stackmode = e->stack_mode;
		wc.borderwidth = e->border_width;

		configure_win(e->window,
				e->value_mask & ~XCB_CONFIG_WINDOW_BORDER_WIDTH, wc);
		return;
	}

	/* Don't resize if maximized. */
	if (! client->fullscreen) {
		xcb_rectangle_t geometry = client->geometry;

		if (e->value_mask & XCB_CONFIG_WINDOW_X)
			geometry.x = e->x;
		if (e->value_mask & XCB_CONFIG_WINDOW_Y)
			geometry.y = e->y;
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)
			geometry.width = e->width;
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
			geometry.height = e->height;

		apply_gravity(client, &geometry);
		/* Check if window fits on screen after resizing. */
		update_geometry(client, &geometry);
	}

	/* Handle sibling/stacking order separately */
	const uint16_t mask = e->value_mask &
		(XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE);

	if (mask) {
		int i = 0;
		uint32_t values[2];
		if (mask & XCB_CONFIG_WINDOW_SIBLING) {
			PDEBUG("configure request : sibling 0x%x\n", e->sibling);
			client_t *sibling = find_client(e->sibling);
			/* replace sibling with its frame if it's ours */
			values[i++] = sibling ? sibling->frame : e->sibling;
		}
		if (mask & XCB_CONFIG_WINDOW_STACK_MODE) {
			PDEBUG("configure request : stack mode\n");
			values[i++] = e->stack_mode;
		}
		xcb_configure_window(conn, client->frame, mask, values);
	}
}

static void handle_client_message(xcb_generic_event_t *ev)
{
	xcb_client_message_event_t *e
		= (xcb_client_message_event_t *) ev;

	client_t *client = find_client(e->window);

	PDEBUG("client_message for 0x%x - %s (%d)\n", e->window, get_atomname(e->type), e->type);

	/* Window wants to know of the borders we'd set */
	if (e->type == ewmh->_NET_REQUEST_FRAME_EXTENTS) {
		ewmh_frame_extents(e->window,
				client && client->fullscreen ? 0 : conf.borderwidth);
		return;
	}

	/* Don't act on messages from unhandled windows */
	if (! client) {
		PDEBUG("client_message - unknown window (0x%x)\n", e->window);
		return;
	}

	/* Move and/or resize the window */
	if (e->type == ewmh->_NET_MOVERESIZE_WINDOW) {
		xcb_rectangle_t geometry = client->geometry;
		if (e->data.data8[0])
			client->hints.win_gravity = e->data.data8[0];
		if (e->data.data8[1] & XCB_CONFIG_WINDOW_X)
			geometry.x = e->data.data32[1];
		if (e->data.data8[1] & XCB_CONFIG_WINDOW_Y)
			geometry.y = e->data.data32[2];
		if (e->data.data8[1] & XCB_CONFIG_WINDOW_WIDTH)
			geometry.width = e->data.data32[3];
		if (e->data.data8[1] & XCB_CONFIG_WINDOW_HEIGHT)
			geometry.height = e->data.data32[4];
		/* XXX source ? */

		apply_gravity(client, &geometry);
		update_geometry(client, &geometry);

		return;
	}

	/* Change WM state, only allow to go to iconic. */
	if (e->type == icccm.wm_change_state && e->format == 32) {
		if (conf.allowicons && e->data.data32[0] == XCB_ICCCM_WM_STATE_ICONIC) {
			set_hidden_events(client);
			hide(client);
			set_workspace(client, WORKSPACE_NONE);
			return;
		}
		return;
	}

	/* Close window */
	if (e->type == ewmh->_NET_CLOSE_WINDOW) {
		delete_win(client);
		return;
	}

	/* Set active window */
	if (e->type == ewmh->_NET_ACTIVE_WINDOW) {
		set_focus(client);
		return;
	}

	/* window manager state request */
	if (e->type == ewmh->_NET_WM_STATE) {
		bool max_v	= false;
		bool fs		= false;

		uint32_t action = e->data.data32[0];

		for (int i = 1; i < 3; i++) {
			xcb_atom_t atom = (xcb_atom_t)e->data.data32[i];
			if (atom == ewmh->_NET_WM_STATE_FULLSCREEN)
				fs = true;
			else if (atom == ewmh->_NET_WM_STATE_MAXIMIZED_VERT)
				max_v = true;
			/* further possble states:
			 * _NET_WM_STATE_MAXIMIZE_HORZ (unimplemented)
			 * _NET_WM_STATE_HIDDEN)       (will not be allowed)
			 * stacking order              (TODO)
			 */
		}

		// Act on request, fullscreen takes precedence
		switch (action) {
			case XCB_EWMH_WM_STATE_ADD:
				if (fs && !client->fullscreen)
					toggle_fullscreen(client);
				else if (max_v && !client->vertmaxed)
					toggle_vertical(client);
				break;
			case XCB_EWMH_WM_STATE_TOGGLE:
				if (fs)
					toggle_fullscreen(client);
				else if (max_v)
					toggle_vertical(client);
				break;
			case XCB_EWMH_WM_STATE_REMOVE:
				if (fs && client->fullscreen)
					toggle_fullscreen(client);
				else if (max_v && client->vertmaxed)
					toggle_vertical(client);
				break;
		}
	} // if _net_wm_state
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
	PDEBUG("mapping_notify: req: %d count: %d first: %d\n", e->request,
			e->count, e->first_keycode);
	if (e->request != XCB_MAPPING_MODIFIER
			&& e->request != XCB_MAPPING_KEYBOARD)
		return;

	/* Forget old key bindings. */
	xcb_ungrab_key(conn, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);

	/* Use the new ones. */
	if (! setup_keys()) {
		PERROR("wmwm: Couldn't set up keycodes. Exiting.");
		cleanup(1);
	}
}

void handle_unmap_notify(xcb_generic_event_t *ev)
{
	const xcb_unmap_notify_event_t *e
		= (xcb_unmap_notify_event_t *) ev;

	/*
	 * Find the window in our global window list, then
	 * forget about it. If it gets mapped, we add it to our
	 * lists again then.
	 *
	 * Note that we might not know about the window we got the
	 * UnmapNotify event for. It might be a window we just
	 * unmapped on *another* workspace when changing
	 * workspaces, for instance, or it might be a window with
	 * override redirect set. This is not an error.
	 */

	PDEBUG("unmap_notify: sent=%d event=0x%x, window=0x%x, seq=%d\n",
			XCB_EVENT_SENT(ev),
			e->event, e->window, e->sequence);

	client_t *client = find_client(e->window);
	if (! client)
		return;

	/* we expect and ignore that unmap */
	if (client->ignore_unmap) {
		client->ignore_unmap = false;
		return;
	}

#if 0
	/* see ICCCM 4.1.4. Changing Window State
	 * When changing the state of the window to Withdrawn, the client must (in addition to unmapping
	   the window) send a synthetic UnmapNotify event by using a SendEvent
	 */
	if (XCB_EVENT_SENT(ev)) {
		// synthetic event, indicates wanting to withdrawn state
		PDEBUG("unmap_notify for 0x%x [synthetic]\n", e->window);
	}
#endif
	remove_client(client);
}

void handle_destroy_notify(xcb_generic_event_t *ev)
{
	/*
	 * Find this window in list of clients and forget about
	 * it. (It might hidden while being destroyed,
	 * so no unmap notify)
	 */

	const xcb_destroy_notify_event_t *e
		= (xcb_destroy_notify_event_t *) ev;

	client_t *client = find_client(e->window);
	PDEBUG("destroy_notify for 0x%x (is client = %d)\n", e->window, client ? 1 : 0);

	if (client)
		remove_client(client);
}

void print_help(void)
{
	printf("Usage: wmwm [-b width] [-t terminal] [-m menu]"
			"[-f color] [-F color] [-x color] [-X color]\n");
	printf("\n");
	printf("  -b width\tborder width\n");
	printf("  -t terminal\tstart terminal with MODKEY + Return\n");
	printf("  -m menu\tstart menu with MODKEY + m\n");
	printf("  -f color\tfocused window border color\n");
	printf("  -F color\tunfocused window border color\n");
	printf("\n");
	printf("color may be either a named color or in #000000 notation\n");
	printf("\n");
}

void signal_catch(int sig)
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
	static char* name = NULL;
	xcb_get_atom_name_reply_t *an_rep;

	an_rep = xcb_get_atom_name_reply(conn,
			xcb_get_atom_name_unchecked(conn, atom), NULL);

	if (! an_rep)
		return NULL;

	if (name)
		destroy(name);
	name = calloc(xcb_get_atom_name_name_length(an_rep) + 1,
			sizeof(char));
	strncpy(name, xcb_get_atom_name_name(an_rep),
			xcb_get_atom_name_name_length(an_rep));

	destroy(an_rep);
	return name;
}
#endif

void get_monitor_geometry(monitor_t* monitor, xcb_rectangle_t* sp)
{
	if (! monitor) {
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
	xcb_screen_iterator_t iter;

	set_timestamp(XCB_CURRENT_TIME);

	ewmh = NULL;

	/* Install signal handlers. */

	/* We ignore child exists. Don't create zombies. */
	if (SIG_ERR == signal(SIGCHLD, SIG_IGN)) {
		perror("wmwm: signal");
		exit(1);
	}

	if (SIG_ERR == signal(SIGINT, signal_catch)) {
		perror("wmwm: signal");
		exit(1);
	}

	if (SIG_ERR == signal(SIGTERM, signal_catch)) {
		perror("wmwm: signal");
		exit(1);
	}

	/* Set up defaults. */

	conf.borderwidth = BORDERWIDTH;
	conf.terminal = TERMINAL;
	conf.menu = MENU;
	conf.allowicons = ALLOWICONS;
	focuscol = FOCUSCOL;
	unfocuscol = UNFOCUSCOL;

	while ((ch = getopt(argc, argv, "b:it:m:f:F:x:X:")) != -1) {
		switch (ch) {
			case 'b':
				conf.borderwidth = atoi(optarg);
				break;
			case 'i':
				conf.allowicons = true;
				break;
			case 't':
				conf.terminal = optarg;
				break;
			case 'm':
				conf.menu = optarg;
				break;
			case 'f':
				focuscol = optarg;
				break;
			case 'F':
				unfocuscol = optarg;
				break;
			default:
				print_help();
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
	for (int i = 0; i < screen_number; ++i)
		xcb_screen_next(&iter);

	screen = iter.data;
	if (! screen) {
		PERROR("Can't get the current screen. Exiting.\n");
		xcb_disconnect(conn);
		exit(1);
	}

	root = screen->root;

	PDEBUG("Screen size: %dx%d\nRoot window: 0x%x\n",
			screen->width_in_pixels, screen->height_in_pixels, screen->root);

	/* Get some colors. */
	conf.focuscol = getcolor(focuscol);
	conf.unfocuscol = getcolor(unfocuscol);

	/* setup EWMH */
	if (! setup_ewmh()) {
		PERROR("Failed to initialize xcb-ewmh. Exiting.\n");
		cleanup(1);
	}

	/* Check for RANDR extension and configure. */
	randrbase = setup_randr();

	/* Check for SHAPE extension */
	shapebase = setup_shape();


	/* Loop over all clients and set up stuff. */
	if (! setup_screen()) {
		PERROR("Failed to initialize windows. Exiting.\n");
		xcb_disconnect(conn);
		exit(1);
	}

	/* Set up key bindings. */
	if (! setup_keys()) {
		PERROR("Couldn't set up keycodes. Exiting.");
		cleanup(1);
	}

	/* Grab mouse buttons. */
	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 1 /* left mouse button */ ,
			MOUSE_MODKEY);

	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 2 /* middle mouse button */ ,
			MOUSE_MODKEY);

	xcb_grab_button(conn, 0, root, XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root,
			XCB_NONE, 3 /* right mouse button */ ,
			MOUSE_MODKEY);

	/* why can't I put this before setup_screen ? XXX */
	/* Subscribe to events. */
	mask = XCB_CW_EVENT_MASK;
	values[0] = DEFAULT_ROOT_WINDOW_EVENTS;
	cookie = xcb_change_window_attributes_checked(conn, root, mask, values);
	error = xcb_request_check(conn, cookie);
	if (error) {
		print_x_error(error);
		destroy(error);
		PERROR("Can't get SUBSTRUCTURE REDIRECT. "
				"Another window manager running? Exiting.\n");
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
		pointer = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, screen->root), 0);
		if (pointer) {
			win = pointer->child;
			destroy(pointer);
		} else {
			PDEBUG("Did not find window under cursor.\n");
		}
	}
	set_focus(find_clientp(win));
}

/* apply client's gravity to given geometry */
void apply_gravity(client_t *client, xcb_rectangle_t* geometry)
{
	const int border = client->fullscreen ? 0 : conf.borderwidth;

	if (client->hints.flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY) {
		switch (client->hints.win_gravity) {
			case XCB_GRAVITY_STATIC:
				break;
			case XCB_GRAVITY_NORTH_WEST:
				geometry->x -= border;
				geometry->y -= border;
				break;
			case XCB_GRAVITY_NORTH:
				geometry->x += geometry->width / 2;
				geometry->y -= border;
				break;
			case XCB_GRAVITY_NORTH_EAST:
				geometry->x += geometry->width + border;
				geometry->y -= border;
				break;
			case XCB_GRAVITY_EAST:
				geometry->x += geometry->width + border;
				geometry->y += geometry->height / 2;
				break;
			case XCB_GRAVITY_SOUTH_EAST:
				geometry->x += geometry->width + border;
				geometry->y += geometry->height + border;
				break;
			case XCB_GRAVITY_SOUTH:
				geometry->x += geometry->width / 2;
				geometry->y += geometry->height + border;
				break;
			case XCB_GRAVITY_SOUTH_WEST:
				geometry->x -= border;
				geometry->y += geometry->height / 2 + border;
				break;
			case XCB_GRAVITY_WEST:
				geometry->x -= border;
				geometry->y += geometry->height / 2;
				break;
			case XCB_GRAVITY_CENTER:
				geometry->x += geometry->width / 2 ;
				geometry->y += geometry->height / 2;
				break;
		}
	}
}
