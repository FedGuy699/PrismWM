#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>


#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <csignal>
#include <cstring>

#include "config.h"
#include "launch.h"
#include "monitor.h"
#include "window.h"
#include "lock.h"

Display* display = nullptr;
Window root;

const char* display_name = nullptr;

bool running = true;
bool resize_mode = false;

std::vector<Window> managed_windows;


int parse_modifier(const std::string& mod) {
    if (mod == "Mod4") return Mod4Mask;
    if (mod == "Shift") return ShiftMask;
    if (mod == "Control") return ControlMask;
    return 0;
}

void cleanup() {
    if (display) XCloseDisplay(display);
}

void signal_handler(int) {
    running = false;
}

bool check_app_position_request(Display* dpy, Window w) {
    XSizeHints hints;
    long supplied_return;
    if (XGetWMNormalHints(dpy, w, &hints, &supplied_return)) {
        if (hints.flags & PPosition) {
            return true;
        }
    }
    return false;
}

Window get_toplevel_window(Display* dpy, Window w) {
    Window root_return, parent_return;
    Window *children_return;
    unsigned int nchildren;

    Window current = w;
    while (true) {
        if (!XQueryTree(dpy, current, &root_return, &parent_return, &children_return, &nchildren)) {
            break;
        }
        if (children_return) XFree(children_return);
        if (parent_return == root_return || parent_return == None) {
            break; 
        }
        current = parent_return;
    }
    return current;
}

void handle_key_press(XKeyEvent* ev) {
    KeyCode keycode = ev->keycode;
    unsigned int state = ev->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);

    KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
    if (keysym == XK_l && state == Mod4Mask) {
        lock();
        return;
    }

    auto it = keybindings.find({keycode, state});
    if (it != keybindings.end()) {
        const std::string& cmd = it->second;
        launch(cmd.c_str(), 0, 0, display_name);
    }
}

int main() {
    display_name = getenv("DISPLAY");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, SIG_IGN);
    atexit(cleanup);
    display = XOpenDisplay(display_name);
    if (!display) {
        std::cerr << "Failed to open X display.\n";
        return 1;
    }

    root = DefaultRootWindow(display);

    XSetErrorHandler([](Display*, XErrorEvent* e) -> int {
        if (e->error_code == BadAccess) {
            std::cerr << "Another WM is running.\n";
            std::exit(1);
        }
        return 0;
    });


    XSelectInput(display, root, 
        SubstructureRedirectMask | SubstructureNotifyMask | 
        KeyPressMask | PointerMotionMask | StructureNotifyMask |
        ClientMessage);  


    load_config(display, root);
    if (!xrandr_command.empty()) {
        std::string full_cmd = "xrandr " + xrandr_command;
        system(full_cmd.c_str());
    }

    for (const std::string& cmd : startup_commands) {
        launch(cmd.c_str(), 0, 0, display_name);
    }

    init_atoms();

    XFlush(display);
    Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, cursor);

    while(running){
        XEvent ev;
        XNextEvent(display, &ev);

        switch (ev.type) {
            case MapRequest:
                handle_map_request(&ev.xmaprequest);
                break;
            case DestroyNotify:
                handle_destroy_notify(&ev.xdestroywindow);
                break;
            case KeyPress:
                handle_key_press(&ev.xkey);
                break;
            case ButtonPress:
                handle_button_press(&ev.xbutton);
                break;
            case ButtonRelease:
                handle_button_release(&ev.xbutton);
                break;
            case MotionNotify:
                handle_motion_notify(&ev.xmotion);
                break;
            case Expose:
                handle_expose(&ev.xexpose);
                break;
            case ClientMessage:
                handle_client_message(&ev.xclient);
                break;
            case PropertyNotify:
                handle_property_notify(&ev.xproperty);
                break;
            case ConfigureRequest: {
                XConfigureRequestEvent* req = &ev.xconfigurerequest;
                XWindowChanges changes;
                changes.x = req->x;
                changes.y = req->y;
                changes.width = req->width;
                changes.height = req->height;
                changes.border_width = req->border_width;
                changes.sibling = req->above;
                changes.stack_mode = req->detail;

                XConfigureWindow(display, req->window, req->value_mask, &changes);
                break;
            }

        }

    }

    return 0;
}