#pragma once

#include <X11/Xlib.h>
 
void handle_map_request(XMapRequestEvent* ev);
void handle_destroy_notify(XDestroyWindowEvent* ev);
void handle_button_press(XButtonEvent* ev);
void handle_motion_notify(XMotionEvent* ev);
void handle_button_release(XButtonEvent* ev);
void handle_expose(XExposeEvent* ev);
void init_atoms();
void handle_client_message(XClientMessageEvent* ev);
void handle_property_notify(XPropertyEvent* ev);

void start_window_drag(Window win, int x_root, int y_root);
void end_window_drag();

extern bool drag_in_progress;
extern Window drag_window;
extern int drag_offset_x;
extern int drag_offset_y;

extern Atom net_wm_state;
extern Atom net_wm_state_fullscreen;
extern Atom net_wm_state_maximized_vert;
extern Atom net_wm_state_maximized_horz;



