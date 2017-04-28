# TODO List

## Bugs
 * skip/update tiling-size when there are fullscreen-windows
 * tabbing fullscreen <-> floating (not tiling) and
 * stacking: [fullscreen, floating] > [tiling]
 * unsigned overflow/underflow in geometry.x += -= etc.
 * some windows take focus (override_redirect, GLX) without being handled (focuswin not nulled)
   -> temp fix via allowing set_focus to focuswin

## TODO
 * move floating, fullscreen test out of raise/lower
 * test and likely fix xrandr support
 * vertical maximization, do I want to support that or not?
 * client list atom in root window: all windows or workdesk? order?
 * DIRTY state for when screen resolution changes for other workspaces?
 * set CLASS and PID for Window Manager
 * respect workarea (e.g. title bar)?
 * stop always printing the window-tree :-)

## Suggestions
 * resizable tiles, store geometry
 * prefer use of wtree-functions using client_t instead of wtree_t 
 * different color for windows that cannot have inputfocus ?
 * WM_COLORMAP_WINDOWS ?
 * clean up atoms on quit ?
 * key handling, automate a little further, it looks really ugly
 * setup keys for single keys (mapping_notify)
 * better handling of atoms (ewmh,icccm) and their usage (cut xcb_ewmh ext?)
 * !stacking
 * MWM hints
 * color: use logic for "graying/darken" "normal" colors
 * enforced aspect ratio
 * only update geometry after workspace change (or on current) after monitor-updates
 * _NET_WM_FULL_PLACEMENT

## Check
 * for client messages I might check for source type:
	if (e->data.data32[1] == XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER
		|| e->data.data32[1] == XCB_EWMH_CLIENT_SOURCE_TYPE_NONE)
 * save geometry from before tiling?
 * don't allow containers with only one client (delete one from a pair, not being able to toggle tiling the parent)
