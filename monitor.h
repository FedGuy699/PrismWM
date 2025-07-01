#pragma once
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
bool get_monitor_geometry(Display* dpy, Window win, int* x, int* y, int* w, int* h);
bool get_primary_monitor_geometry(Display* dpy, int* x, int* y, int* w, int* h);
bool get_monitor_geometry_for_window(Display* dpy, Window win, int* x, int* y, int* w, int* h);