/* User configurable stuff. */
#include <X11/keysym.h>  // for XK_0, XK_1, XK_2, XK_3, XK_4, XK_5, XK_6
/*
 * Move this many pixels when moving or resizing with keyboard unless
 * the window has hints saying otherwise.
 */
#define MOVE_STEP 32

/*
 * Use this modifier combined with other keys to control wm from
 * keyboard. Default is Mod4, which on my keyboard is the Alt key but
 * is usually the Windows key on more normal keyboard layouts.
 */
/* XXX: These three all have to be different for now */
#define MODKEY XCB_MOD_MASK_4

/* Extra modifier for resizing. Default is Shift. */
#define EXTRA_MODKEY (MODKEY | XCB_MOD_MASK_SHIFT)

/*
 * Modifier key to use with mouse buttons. Default Mod1, Meta on my
 * keyboard.
 */
#define MOUSE_MODKEY XCB_MOD_MASK_4

/*
 * Start this program when pressing MODKEY + USERKEY_TERMINAL. Needs
 * to be in $PATH.
 *
 * Change to "xterm" if you're feeling conservative.
 *
 * Can be set from command line with "-t program".
 */
#define TERMINAL "/usr/bin/urxvt"

/*
 * Start this program when pressing MODKEY + USERKEY_MENU. Needs
 * to be in $PATH.
 */

#define MENU "dmenu_run"

/*
 * Do we allow windows to be iconified? Set to true if you want this
 * behaviour to be default. Can also be set by calling mcwm with -i.
 */
#define ALLOWICONS false

/*
 * Start these programs when pressing MODKEY and mouse buttons on root window.
 */
#define MOUSE1 NULL
#define MOUSE2 NULL
#define MOUSE3 NULL
/*
 * Default color on border for focused windows. Can be set from
 * command line with "-f color".
 */
#define FOCUSCOL "#0063ff"

/* Ditto for unfocused. Use "-u color". */
//#define UNFOCUSCOL "grey50"
#define UNFOCUSCOL "#003383"

/* Default width of border window, in pixels. Used unless -b width. */
#define BORDERWIDTH 1

/*
 * Keysym codes for window operations. Look in X11/keysymdefs.h for
 * actual symbols.
 */
#define USERKEY_MOVE_LEFT	XK_H
#define USERKEY_MOVE_DOWN	XK_J
#define USERKEY_MOVE_UP		XK_K
#define USERKEY_MOVE_RIGHT	XK_L
#define USERKEY_MAXVERT		XK_V
#define USERKEY_RAISE		XK_R
#define USERKEY_TERMINAL	XK_Return
#define USERKEY_MENU	 	XK_D
#define USERKEY_MAX			XK_X
#define USERKEY_CHANGE		XK_Tab
#define USERKEY_WS10		XK_0
#define USERKEY_WS1			XK_1
#define USERKEY_WS2			XK_2
#define USERKEY_WS3			XK_3
#define USERKEY_WS4			XK_4
#define USERKEY_WS5			XK_5
#define USERKEY_WS6			XK_6
#define USERKEY_WS7			XK_7
#define USERKEY_WS8			XK_8
#define USERKEY_WS9			XK_9
#define USERKEY_TOPLEFT		XK_Z
#define USERKEY_TOPRIGHT	XK_U
#define USERKEY_BOTLEFT		XK_B
#define USERKEY_BOTRIGHT	XK_N
#define USERKEY_DELETE		XK_End
#define USERKEY_PREVSCREEN	XK_comma
#define USERKEY_NEXTSCREEN	XK_period
#define USERKEY_ICONIFY		XK_I
