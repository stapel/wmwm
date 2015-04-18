/*
 * hidden - A small program to listen all windows with WM_STATE set to
 * Iconic.
 *
 * Copyright (c) 2012 Michael Cardell Widerkrantz, mc at the domain
 * hack.org.
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

/* XXX
 * Does not work anymore because this is a reparenting WM now!
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>

xcb_connection_t *conn;
xcb_screen_t *screen;

xcb_atom_t wm_state;
xcb_atom_t wm_icon_name;

bool printcommand = false;
xcb_ewmh_connection_t *ewmh;
int scrno;

static uint32_t get_wm_state(xcb_drawable_t win);
static int get_wm_name(xcb_window_t win, char** string, int* len);
static int get_wm_name_ewmh(xcb_window_t win, char **string, int* len);
static int get_wm_name_icccm(xcb_window_t win, char **string, int* len);
static int32_t ewmh_get_workspace(xcb_drawable_t win);

static int findhidden(void);
static void init(void);
static void cleanup(void);
static xcb_atom_t getatom(char *atom_name);
static void printhelp(void);

int32_t ewmh_get_workspace(xcb_drawable_t win)
{
	xcb_get_property_cookie_t cookie;
	uint32_t ws;

	cookie = xcb_ewmh_get_wm_desktop_unchecked(ewmh, win);

	if (! xcb_ewmh_get_wm_desktop_reply(ewmh, cookie, &ws, NULL))
		ws = -1;
	return ws;
}


uint32_t get_wm_state(xcb_drawable_t win)
{
	xcb_get_property_reply_t *reply;
	xcb_get_property_cookie_t cookie;
	uint32_t *statep;
	uint32_t state = 0;

	cookie = xcb_get_property(conn, false, win, wm_state, wm_state, 0,
							  sizeof(int32_t));

	reply = xcb_get_property_reply(conn, cookie, NULL);
	if (NULL == reply) {
		fprintf(stderr, "mcwm: Couldn't get properties for win %d\n", win);
		return -1;
	}

	/* Length is 0 if we didn't find it. */
	if (0 == xcb_get_property_value_length(reply)) {
		goto bad;
	}

	statep = xcb_get_property_value(reply);
	state = *statep;

bad:
	free(reply);
	return state;
}


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
		if (*string == NULL) {
			perror("get_wm_name icccm");
			cleanup();
			exit(1);
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

	xcb_get_property_cookie_t 			cookie;
	xcb_ewmh_get_utf8_strings_reply_t	data;

	cookie = xcb_ewmh_get_wm_name(ewmh, win);
	if (! xcb_ewmh_get_wm_name_reply(ewmh, cookie, &data, NULL)) {
		return 0;
	}

	if (data.strings_len) {
		*string = calloc(data.strings_len + 1, sizeof(char));
		*len = data.strings_len;

		if (*string == NULL) {
			perror("get_wm_name_ewmh");
			cleanup();
			exit(1);
		}
		memcpy(*string, data.strings, data.strings_len * sizeof(char));
	}
	xcb_ewmh_get_utf8_strings_reply_wipe(&data);
	return 1;
}



/*
 * List all hidden windows.
 *
 */
int findhidden(void)
{
	uint32_t i;

	char* name;
	int name_len;

	xcb_window_t id;

	xcb_get_property_cookie_t  p;
	uint32_t state;

	xcb_ewmh_get_windows_reply_t clients;

	/* Get windows listed in _NET_CLIENT_LIST */

	p = xcb_ewmh_get_client_list_unchecked(ewmh, scrno);
	if (! xcb_ewmh_get_client_list_reply(ewmh, p, &clients, NULL)) {
		return 0;
	}

	/* List all hidden windows on this root. */
	for (i = 0; i < clients.windows_len; i++) {
		id = (clients.windows)[i];
		state = get_wm_state(id);
		if (state == XCB_ICCCM_WM_STATE_ICONIC) {
			printf("#%d\t", ewmh_get_workspace(id));
			if (get_wm_name(id, &name, &name_len) != 0) {
				printf("%s\n", name);
				free(name);
			} else {
				printf("unnamed window (0x%x)\n", id);
			}
		}
	}
//	xcb_ewmh_get_windows_reply_wipe(clients);
	return 0;
}

void init(void)
{
	xcb_screen_iterator_t iter;

	conn = xcb_connect(NULL, &scrno);
	if (!conn) {
		fprintf(stderr, "can't connect to an X server\n");
		exit(1);
	}

	iter = xcb_setup_roots_iterator(xcb_get_setup(conn));

	for (int i = 0; i < scrno; ++i) {
		xcb_screen_next(&iter);
	}

	screen = iter.data;

	if (!screen) {
		fprintf(stderr, "can't get the current screen\n");
		xcb_disconnect(conn);
		exit(1);
	}
	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	xcb_intern_atom_cookie_t *cookies = xcb_ewmh_init_atoms(conn, ewmh);
	xcb_ewmh_init_atoms_replies(ewmh, cookies, NULL);
}

void cleanup(void)
{
	if (ewmh) xcb_ewmh_connection_wipe(ewmh);
	xcb_disconnect(conn);
}

/*
 * Get a defined atom from the X server.
 */
xcb_atom_t getatom(char *atom_name)
{
	xcb_intern_atom_cookie_t atom_cookie;
	xcb_atom_t atom;
	xcb_intern_atom_reply_t *rep;

	atom_cookie = xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
	rep = xcb_intern_atom_reply(conn, atom_cookie, NULL);
	if (NULL != rep) {
		atom = rep->atom;
		free(rep);
		return atom;
	}

	/*
	 * XXX Note that we return 0 as an atom if anything goes wrong.
	 * Might become interesting.
	 */
	return 0;
}

void printhelp(void)
{
	printf("hidden: Usage: hidden [-c]\n");
	printf("  -c print 9menu/xdotool compatible output.\n");
}

int main(int argc, char **argv)
{
	int ch;						/* Option character */

	while (1) {
		ch = getopt(argc, argv, "c");
		if (-1 == ch) {
			/* No more options, break out of while loop. */
			break;
		}
		switch (ch) {
		case 'c':
			printcommand = true;
			break;

		default:
			printhelp();
			exit(0);
		}						/* switch ch */
	}							/* while 1 */

	init();
	wm_state = getatom("WM_STATE");
	findhidden();
	cleanup();
	exit(0);
}
