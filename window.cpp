#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include "monitor.h"
#include "window.h"

#include <vector>
#include <algorithm>
#include <cstring>
#include <unordered_map>

extern Display* display;
extern std::vector<Window> managed_windows;
extern Window root;


const int TITLE_BAR_HEIGHT = 24;
const int BORDER_WIDTH = 1;

const int CLOSE_BUTTON_SIZE = 16;
const int CLOSE_BUTTON_MARGIN = 4;

const int MAXIMIZE_BUTTON_SIZE = 16;
const int MAXIMIZE_BUTTON_MARGIN = CLOSE_BUTTON_MARGIN + CLOSE_BUTTON_SIZE + 4;


Atom net_wm_state;
Atom net_wm_state_fullscreen;
Atom net_wm_state_maximized_vert;
Atom net_wm_state_maximized_horz;
Atom net_client_list;

struct WindowPair {
    Window frame;
    Window client;
    GC gc;
};

std::vector<WindowPair> framed_windows;

bool drag_in_progress = false;
Window drag_window = None;
int drag_offset_x = 0;
int drag_offset_y = 0;

struct MotifWmHints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
};
#define MWM_HINTS_DECORATIONS (1L << 1)

enum ResizeDirection {
    RESIZE_NONE = 0,
    RESIZE_LEFT = 1 << 0,
    RESIZE_RIGHT = 1 << 1,
    RESIZE_TOP = 1 << 2,
    RESIZE_BOTTOM = 1 << 3
};

bool resize_in_progress = false;
Window resize_window = None;
ResizeDirection resize_dir = RESIZE_NONE;
int resize_start_x = 0;
int resize_start_y = 0;
int orig_win_x = 0;
int orig_win_y = 0;
unsigned int orig_win_w = 0;
unsigned int orig_win_h = 0;

const int RESIZE_BORDER_WIDTH = 6; 

void draw_title_bar(Window frame);
void handle_expose(XExposeEvent* ev);
void handle_pointer_motion(Window frame, int x, int y);
WindowPair* find_framed_window(Window w);
ResizeDirection get_resize_direction(WindowPair* wp, int x, int y);
void update_cursor(Window frame, ResizeDirection dir);
void start_window_resize(Window win, int x_root, int y_root, ResizeDirection dir);
void end_window_resize();

void init_atoms() {
    net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom net_wm_moveresize = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
    net_wm_state_fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_state_maximized_vert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    net_wm_state_maximized_horz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
}

void start_window_drag(Window win, int x_root, int y_root) {
    drag_in_progress = true;
    drag_window = win;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(display, win, net_wm_state, 0, (~0L), False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        bool is_max_vert = false;
        bool is_max_horz = false;

        Atom* atoms = (Atom*)prop;
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == net_wm_state_maximized_vert) is_max_vert = true;
            if (atoms[i] == net_wm_state_maximized_horz) is_max_horz = true;
        }

        XFree(prop);

        if (is_max_vert || is_max_horz) {
            Window client = win;
            for (const auto& wp : framed_windows) {
                if (wp.frame == win) {
                    client = wp.client;
                    break;
                }
            }

            Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);

            XEvent e = {};
            e.xclient.type = ClientMessage;
            e.xclient.window = client;
            e.xclient.message_type = wm_state;
            e.xclient.format = 32;
            e.xclient.data.l[0] = 0;
            e.xclient.data.l[1] = net_wm_state_maximized_vert;
            e.xclient.data.l[2] = net_wm_state_maximized_horz;
            e.xclient.data.l[3] = 1;
            e.xclient.data.l[4] = 0;

            XSendEvent(display, root, False,
                       SubstructureRedirectMask | SubstructureNotifyMask, &e);
        }
    }

    XWindowAttributes attr;
    XGetWindowAttributes(display, win, &attr);
    drag_offset_x = x_root - attr.x;
    drag_offset_y = y_root - attr.y;

    XGrabPointer(display, drag_window, True,
                 PointerMotionMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync,
                 None, None, CurrentTime);
}

void end_window_drag() {
    drag_in_progress = false;
    drag_window = None;
     
    XUngrabPointer(display, CurrentTime);
}


Window create_frame_window(Window client, int x, int y, int width, int height) {
    XSetWindowAttributes frame_attrs;
    frame_attrs.background_pixel = BlackPixel(display, DefaultScreen(display));
    frame_attrs.border_pixel = WhitePixel(display, DefaultScreen(display));
    frame_attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | 
                              PointerMotionMask | SubstructureRedirectMask |
                              StructureNotifyMask;

    Window frame = XCreateWindow(display, DefaultRootWindow(display),
                                 x, y, width, height + TITLE_BAR_HEIGHT,
                                 BORDER_WIDTH, DefaultDepth(display, DefaultScreen(display)),
                                 CopyFromParent, DefaultVisual(display, DefaultScreen(display)),
                                 CWBackPixel | CWBorderPixel | CWEventMask, &frame_attrs);

    GC gc = XCreateGC(display, frame, 0, NULL);
    XSetWindowBorderWidth(display, frame, BORDER_WIDTH);
    XReparentWindow(display, client, frame, 0, TITLE_BAR_HEIGHT);
    XResizeWindow(display, client, width, height);

    //Shit function
    XSelectInput(display, client, StructureNotifyMask | PropertyChangeMask); 

    XGrabButton(display, Button1, AnyModifier, client,
            False, ButtonPressMask,
            GrabModeSync, GrabModeSync,
            None, None);

    Atom client_leader = XInternAtom(display, "WM_CLIENT_LEADER", False);
    XChangeProperty(display, frame, client_leader, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&frame, 1);

    draw_title_bar(frame);

    XMapWindow(display, frame);
    XMapWindow(display, client);

    framed_windows.push_back({frame, client, gc});
    return frame;
}

void draw_title_bar(Window frame) {
    auto it = std::find_if(framed_windows.begin(), framed_windows.end(),
                           [frame](const WindowPair& wp) { return wp.frame == frame; });
    if (it == framed_windows.end()) return;

    XWindowAttributes attr;
    XGetWindowAttributes(display, frame, &attr);

    int width = attr.width;

     
    XSetForeground(display, it->gc, BlackPixel(display, DefaultScreen(display)));
    XFillRectangle(display, frame, it->gc, 0, 0, width, TITLE_BAR_HEIGHT);

     
    int max_x = width - MAXIMIZE_BUTTON_MARGIN - MAXIMIZE_BUTTON_SIZE;
    int max_y = (TITLE_BAR_HEIGHT - MAXIMIZE_BUTTON_SIZE) / 2;

    unsigned long gray = 0x888888;
    XSetForeground(display, it->gc, gray);
    XFillRectangle(display, frame, it->gc, max_x, max_y, MAXIMIZE_BUTTON_SIZE, MAXIMIZE_BUTTON_SIZE);

    XSetForeground(display, it->gc, BlackPixel(display, DefaultScreen(display)));
     
    XDrawRectangle(display, frame, it->gc, max_x + 3, max_y + 3, MAXIMIZE_BUTTON_SIZE - 6, MAXIMIZE_BUTTON_SIZE - 6);

     
    int close_x = width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN;
    int close_y = (TITLE_BAR_HEIGHT - CLOSE_BUTTON_SIZE) / 2;

    unsigned long red = 0xff0000;
    XSetForeground(display, it->gc, red);
    XFillRectangle(display, frame, it->gc, close_x, close_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE);

    XSetForeground(display, it->gc, WhitePixel(display, DefaultScreen(display)));
    XDrawLine(display, frame, it->gc, close_x, close_y, close_x + CLOSE_BUTTON_SIZE, close_y + CLOSE_BUTTON_SIZE);
    XDrawLine(display, frame, it->gc, close_x + CLOSE_BUTTON_SIZE, close_y, close_x, close_y + CLOSE_BUTTON_SIZE);

     
    char* name = nullptr;
    if (XFetchName(display, it->client, &name) && name) {
        XFontStruct* font = XLoadQueryFont(display, "fixed");
        if (font) {
            XSetFont(display, it->gc, font->fid);
            XSetForeground(display, it->gc, WhitePixel(display, DefaultScreen(display)));
            XDrawString(display, frame, it->gc, 10, TITLE_BAR_HEIGHT / 2 + 5, name, strlen(name));
            XFreeFont(display, font);
        }
        XFree(name);
    }
}



WindowPair* find_framed_window(Window w) {
    for (auto& wp : framed_windows) {
        if (wp.frame == w) return &wp;
    }
    return nullptr;
}

ResizeDirection get_resize_direction(WindowPair* wp, int x, int y) {
    if (!wp) return RESIZE_NONE;

    XWindowAttributes attr;
    XGetWindowAttributes(display, wp->frame, &attr);

    ResizeDirection dir = RESIZE_NONE;
    if (x >= 0 && x <= RESIZE_BORDER_WIDTH)
        dir = (ResizeDirection)(dir | RESIZE_LEFT);
    else if (x >= (int)attr.width - RESIZE_BORDER_WIDTH)   
        dir = (ResizeDirection)(dir | RESIZE_RIGHT);

    if (y >= 0 && y <= RESIZE_BORDER_WIDTH)
        dir = (ResizeDirection)(dir | RESIZE_TOP);
    else if (y >= (int)attr.height - RESIZE_BORDER_WIDTH)
        dir = (ResizeDirection)(dir | RESIZE_BOTTOM);

    return dir;
}


void update_cursor(Window frame, ResizeDirection dir) {
    static Cursor cursor = None;

    if (cursor != None) {
        XFreeCursor(display, cursor);
        cursor = None;
    }

    unsigned int shape = None;

    switch (dir) {
        case RESIZE_LEFT: shape = XC_left_side; break;
        case RESIZE_RIGHT: shape = XC_right_side; break;
        case RESIZE_TOP: shape = XC_top_side; break;
        case RESIZE_BOTTOM: shape = XC_bottom_side; break;
        case ResizeDirection(RESIZE_LEFT | RESIZE_TOP): shape = XC_top_left_corner; break;
        case ResizeDirection(RESIZE_RIGHT | RESIZE_TOP): shape = XC_top_right_corner; break;
        case ResizeDirection(RESIZE_LEFT | RESIZE_BOTTOM): shape = XC_bottom_left_corner; break;
        case ResizeDirection(RESIZE_RIGHT | RESIZE_BOTTOM): shape = XC_bottom_right_corner; break;
        default: shape = XC_left_ptr; break;
    }

    cursor = XCreateFontCursor(display, shape);
    XDefineCursor(display, frame, cursor);
}

void start_window_resize(Window win, int x_root, int y_root, ResizeDirection dir) {
    resize_in_progress = true;
    resize_window = win;
    resize_dir = dir;
    resize_start_x = x_root;
    resize_start_y = y_root;

    XWindowAttributes attr;
    XGetWindowAttributes(display, win, &attr);
    orig_win_x = attr.x;
    orig_win_y = attr.y;
    orig_win_w = attr.width;
    orig_win_h = attr.height;
}

void end_window_resize() {
    resize_in_progress = false;
    resize_window = None;
    resize_dir = RESIZE_NONE;
}

void update_net_client_list() {
    if (managed_windows.empty()) {
        XDeleteProperty(display, root, net_client_list);
        return;
    }

    XChangeProperty(display, root, net_client_list, XA_WINDOW, 32,
                    PropModeReplace,
                    (unsigned char*)managed_windows.data(),
                    managed_windows.size());
}

void handle_map_request(XMapRequestEvent* ev) {
    Window w = ev->window;

    XWindowAttributes attr;
    XGetWindowAttributes(display, w, &attr);
    if (attr.override_redirect) {
        XMapWindow(display, w);
        return;
    }

    for (const auto& wp : framed_windows) {
        if (wp.client == w) {
            XMapWindow(display, wp.frame);
            return;
        }
    }

    bool wants_no_decor = false;
    bool wants_fullscreen = false;

     
    Atom motifHints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(display, w, motifHints, 0, 5, False, motifHints,
                           &actual_type, &actual_format, &nitems,
                           &bytes_after, &prop) == Success && prop) {
        if (nitems >= 5) {
            unsigned long* hints = (unsigned long*)prop;
            if ((hints[0] & MWM_HINTS_DECORATIONS) && hints[2] == 0) {
                wants_no_decor = true;
            }
        }
        XFree(prop);
    }

     
    if (XGetWindowProperty(display, w, net_wm_state, 0, (~0L), False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        Atom* atoms = (Atom*)prop;
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == net_wm_state_fullscreen) {
                wants_fullscreen = true;
                break;
            }
        }
        XFree(prop);
    }

    int width = attr.width;
    int height = attr.height;
    if (width < 100 || height < 100) {
        width = 800;
        height = 600;
    }

    XSizeHints hints;
    long supplied;
    if (XGetWMNormalHints(display, w, &hints, &supplied)) {
        if (hints.flags & PSize) {
            width = hints.width;
            height = hints.height;
        } else if (hints.flags & PMinSize) {
            width = std::max(width, hints.min_width);
            height = std::max(height, hints.min_height);
        }
    }

    int mon_x, mon_y, mon_w, mon_h;
    if (!get_primary_monitor_geometry(display, &mon_x, &mon_y, &mon_w, &mon_h)) {
        mon_x = mon_y = 0;
        mon_w = DisplayWidth(display, DefaultScreen(display));
        mon_h = DisplayHeight(display, DefaultScreen(display));
    }

    int win_x = mon_x + (mon_w - width) / 2;
    int win_y = mon_y + (mon_h - height) / 2;

    if (wants_no_decor || wants_fullscreen) {
        XMoveResizeWindow(display, w, win_x, win_y, width, height);
        XMapWindow(display, w);
        XSelectInput(display, w, StructureNotifyMask);
        managed_windows.push_back(w);
        update_net_client_list();
    } else {
        Window frame = create_frame_window(w, win_x, win_y, width, height);
        managed_windows.push_back(frame);
        update_net_client_list();
    }
}

void handle_destroy_notify(XDestroyWindowEvent* ev) {
    auto it = std::find_if(framed_windows.begin(), framed_windows.end(),
        [ev](const WindowPair& wp) { return wp.frame == ev->window || wp.client == ev->window; });

    if (it != framed_windows.end()) {
        if (it->frame != ev->window) {
            XDestroyWindow(display, it->frame);
        }
        if (it->client != ev->window) {
            XDestroyWindow(display, it->client);
        }

        XFreeGC(display, it->gc);
        framed_windows.erase(it);
        update_net_client_list();
    }

    managed_windows.erase(
        std::remove(managed_windows.begin(), managed_windows.end(), ev->window),
        managed_windows.end()
    );
}


void raise_and_focus_window(Window win) {
    WindowPair* wp = find_framed_window(win);
    Window client = win;
    Window frame = win;

    if (wp) {
        client = wp->client;
        frame = wp->frame;
        XRaiseWindow(display, frame);
    } else {
        // For undecorated windows, also try to raise them using XConfigureWindow
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, win, &attr)) {
            if (!attr.override_redirect) {
                // Try restacking
                XWindowChanges changes;
                changes.sibling = None;
                changes.stack_mode = Above;
                XConfigureWindow(display, win, CWSibling | CWStackMode, &changes);
            }
            // Just also call XRaiseWindow in case
            XRaiseWindow(display, win);
        }
    }

    XSetInputFocus(display, client, RevertToPointerRoot, CurrentTime);

    Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XChangeProperty(display, DefaultRootWindow(display), net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char*)&client, 1);
}


void handle_button_press(XButtonEvent* ev) {
    WindowPair* wp = find_framed_window(ev->window);

     
    if (!wp) {
        for (auto& wpair : framed_windows) {
            if (wpair.client == ev->window) {
                wp = &wpair;
                break;
            }
        }
    }

    if (wp) {
         
        raise_and_focus_window(wp->frame);

                
        XSetInputFocus(display, wp->client, RevertToPointerRoot, CurrentTime);
        XAllowEvents(display, ReplayPointer, CurrentTime);


         
        Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
        XChangeProperty(display, DefaultRootWindow(display), net_active_window,
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char*)&wp->client, 1);

         
        if (ev->window == wp->frame && ev->button == Button1) {
            ResizeDirection dir = get_resize_direction(wp, ev->x, ev->y);
            if (dir != RESIZE_NONE) {
                start_window_resize(ev->window, ev->x_root, ev->y_root, dir);
                return;
            }

            if (ev->y < TITLE_BAR_HEIGHT) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, wp->frame, &attr);

                 
                int max_x = attr.width - MAXIMIZE_BUTTON_MARGIN - MAXIMIZE_BUTTON_SIZE;
                int max_y = (TITLE_BAR_HEIGHT - MAXIMIZE_BUTTON_SIZE) / 2;

                 
                if (ev->x >= max_x && ev->x <= max_x + MAXIMIZE_BUTTON_SIZE &&
                    ev->y >= max_y && ev->y <= max_y + MAXIMIZE_BUTTON_SIZE) {

                    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
                    Atom max_vert = net_wm_state_maximized_vert;
                    Atom max_horz = net_wm_state_maximized_horz;

                    XEvent e = {};
                    e.xclient.type = ClientMessage;
                    e.xclient.window = wp->client;
                    e.xclient.message_type = wm_state;
                    e.xclient.format = 32;
                    e.xclient.data.l[0] = 2;  
                    e.xclient.data.l[1] = max_vert;
                    e.xclient.data.l[2] = max_horz;
                    e.xclient.data.l[3] = 1;  
                    e.xclient.data.l[4] = 0;

                    XSendEvent(display, DefaultRootWindow(display), False,
                               SubstructureRedirectMask | SubstructureNotifyMask, &e);
                    return;
                }

                 
                int close_x = attr.width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN;
                int close_y = (TITLE_BAR_HEIGHT - CLOSE_BUTTON_SIZE) / 2;

                if (ev->x >= close_x && ev->x <= close_x + CLOSE_BUTTON_SIZE &&
                    ev->y >= close_y && ev->y <= close_y + CLOSE_BUTTON_SIZE) {

                    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
                    Atom wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);

                    XEvent msg = {};
                    msg.xclient.type = ClientMessage;
                    msg.xclient.message_type = wm_protocols;
                    msg.xclient.display = display;
                    msg.xclient.window = wp->client;
                    msg.xclient.format = 32;
                    msg.xclient.data.l[0] = wm_delete;
                    msg.xclient.data.l[1] = CurrentTime;

                    XSendEvent(display, wp->client, False, NoEventMask, &msg);
                    return;
                }

                 
                start_window_drag(ev->window, ev->x_root, ev->y_root);
                return;
            }
        }
    } else {
         
        XWindowAttributes attr;
        if (XGetWindowAttributes(display, ev->window, &attr) && attr.override_redirect) {
            raise_and_focus_window(ev->window);
            XSetInputFocus(display, ev->window, RevertToPointerRoot, CurrentTime);
            XAllowEvents(display, ReplayPointer, CurrentTime);

            if (ev->button == Button1) {
                 
                ResizeDirection dir = RESIZE_NONE;
                if (ev->x <= RESIZE_BORDER_WIDTH) dir = ResizeDirection(dir | RESIZE_LEFT);
                if (ev->x >= (int)attr.width - RESIZE_BORDER_WIDTH) dir = ResizeDirection(dir | RESIZE_RIGHT);
                if (ev->y <= RESIZE_BORDER_WIDTH) dir = ResizeDirection(dir | RESIZE_TOP);
                if (ev->y >= (int)attr.height - RESIZE_BORDER_WIDTH) dir = ResizeDirection(dir | RESIZE_BOTTOM);

                if (dir != RESIZE_NONE) {
                    start_window_resize(ev->window, ev->x_root, ev->y_root, dir);
                } else {
                     
                    start_window_drag(ev->window, ev->x_root, ev->y_root);
                }
            }
        }
    }
}



void handle_button_release(XButtonEvent* ev) {
    if (ev->button == Button1) {
        if (resize_in_progress) {
            end_window_resize();
        }
        if (drag_window != None) {
            XWindowAttributes attr;
            XGetWindowAttributes(display, drag_window, &attr);

            int mon_x, mon_y, mon_w, mon_h;
            if (!get_monitor_geometry_for_window(display, drag_window, &mon_x, &mon_y, &mon_w, &mon_h)) {
                mon_x = 0;
                mon_y = 0;
                mon_w = DisplayWidth(display, DefaultScreen(display));
                mon_h = DisplayHeight(display, DefaultScreen(display));
            }

            if (attr.y <= mon_y + 5) {
                Window client = drag_window;

                for (const auto& wp : framed_windows) {
                    if (wp.frame == drag_window) {
                        client = wp.client;
                        break;
                    }
                }

                Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);

                XEvent e = {};
                e.xclient.type = ClientMessage;
                e.xclient.window = client;
                e.xclient.message_type = wm_state;
                e.xclient.format = 32;
                e.xclient.data.l[0] = 1;
                e.xclient.data.l[1] = net_wm_state_maximized_vert;
                e.xclient.data.l[2] = net_wm_state_maximized_horz;
                e.xclient.data.l[3] = 1;
                e.xclient.data.l[4] = 0;

                XSendEvent(display, root, False,
                        SubstructureRedirectMask | SubstructureNotifyMask, &e);
            }
        }
        if (drag_in_progress) {
            end_window_drag();
        }
    }
}

void handle_motion_notify(XMotionEvent* ev) {
    if (resize_in_progress && resize_window != None) {
        int dx = ev->x_root - resize_start_x;
        int dy = ev->y_root - resize_start_y;

        XWindowAttributes attr;
        XGetWindowAttributes(display, resize_window, &attr);

        int new_x = orig_win_x;
        int new_y = orig_win_y;
        unsigned int new_w = orig_win_w;
        unsigned int new_h = orig_win_h;

        if (resize_dir & RESIZE_LEFT) {
            new_x = orig_win_x + dx;
            new_w = orig_win_w - dx;
            if (new_w < 100) {
                new_w = 100;
                new_x = orig_win_x + (orig_win_w - 100);
            }
        }
        if (resize_dir & RESIZE_RIGHT) {
            new_w = orig_win_w + dx;
            if (new_w < 100) new_w = 100;
        }
        if (resize_dir & RESIZE_TOP) {
            new_y = orig_win_y + dy;
            new_h = orig_win_h - dy;
            if (new_h < 100) {
                new_h = 100;
                new_y = orig_win_y + (orig_win_h - 100);
            }
        }
        if (resize_dir & RESIZE_BOTTOM) {
            new_h = orig_win_h + dy;
            if (new_h < 100) new_h = 100;
        }

         
        bool is_framed = false;
        for (auto& wp : framed_windows) {
            if (wp.frame == resize_window) {
                is_framed = true;
                XMoveResizeWindow(display, wp.frame, new_x, new_y, new_w, new_h);
                XResizeWindow(display, wp.client, new_w, new_h - TITLE_BAR_HEIGHT);
                break;
            }
        }
        
        if (!is_framed) {
             
            XMoveResizeWindow(display, resize_window, new_x, new_y, new_w, new_h);
        }
    } else if (drag_in_progress && drag_window != None) {
        int new_x = ev->x_root - drag_offset_x;
        int new_y = ev->y_root - drag_offset_y;
        XMoveWindow(display, drag_window, new_x, new_y);
    } else {
        handle_pointer_motion(ev->window, ev->x, ev->y);
    }
}

void handle_expose(XExposeEvent* ev) {
    auto it = std::find_if(framed_windows.begin(), framed_windows.end(),
        [ev](const WindowPair& wp) { return wp.frame == ev->window; });

    if (it != framed_windows.end()) {
        draw_title_bar(ev->window);
    }
}


bool is_decorated(Window w) {
    for (const auto& wp : framed_windows) {
        if (wp.client == w) return true;
    }
    return false;
}

void handle_pointer_motion(Window frame, int x, int y) {
    WindowPair* wp = find_framed_window(frame);
    if (!wp) return;

    ResizeDirection dir = get_resize_direction(wp, x, y);
    if (dir != RESIZE_NONE) {
        update_cursor(frame, dir);
    } else {
        XUndefineCursor(display, frame);
    }
}

void handle_property_notify(XPropertyEvent* ev) {
    if (ev->atom == XA_WM_NAME) {
        for (const auto& wp : framed_windows) {
            if (wp.client == ev->window) {
                draw_title_bar(wp.frame);
                break;
            }
        }
    }
}


void handle_client_message(XClientMessageEvent* ev) {
    if ((Atom)ev->message_type == net_wm_state && ev->format == 32) {
        Window win = ev->window;
        Atom action = ev->data.l[0];
        Atom property = (Atom)ev->data.l[1];
        Atom property2 = (Atom)ev->data.l[2];

        Window target_win = win;
        bool is_framed = false;

        for (const auto& wp : framed_windows) {
            if (wp.client == win) {
                target_win = wp.frame;
                is_framed = true;
                break;
            }
        }

        int mon_x, mon_y, mon_w, mon_h;
        if (!get_monitor_geometry_for_window(display, target_win, &mon_x, &mon_y, &mon_w, &mon_h)) {
            mon_x = mon_y = 0;
            mon_w = DisplayWidth(display, DefaultScreen(display));
            mon_h = DisplayHeight(display, DefaultScreen(display));
        }

        XWindowAttributes attr;
        XGetWindowAttributes(display, target_win, &attr);

        if (property == net_wm_state_maximized_vert || property == net_wm_state_maximized_horz ||
            property2 == net_wm_state_maximized_vert || property2 == net_wm_state_maximized_horz) {

            bool is_maximized = (attr.width == mon_w && attr.height == mon_h);

            if (action == 1 || (action == 2 && !is_maximized)) {
                XMoveResizeWindow(display, target_win, mon_x, mon_y, mon_w, mon_h);
                if (is_framed) {
                    for (auto& wp : framed_windows) {
                        if (wp.frame == target_win) {
                            XResizeWindow(display, wp.client, mon_w, mon_h - TITLE_BAR_HEIGHT);
                            break;
                        }
                    }
                }
                Atom data[2] = { net_wm_state_maximized_vert, net_wm_state_maximized_horz };
                XChangeProperty(display, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<unsigned char*>(data), 2);
            } else if (action == 0 || (action == 2 && is_maximized)) {
                int default_w = 800;
                int default_h = 600;
                int default_x = mon_x + (mon_w - default_w) / 2;
                int default_y = mon_y + (mon_h - default_h) / 2;

                XMoveResizeWindow(display, target_win, default_x, default_y, default_w, default_h);
                if (is_framed) {
                    for (auto& wp : framed_windows) {
                        if (wp.frame == target_win) {
                            XResizeWindow(display, wp.client, default_w, default_h - TITLE_BAR_HEIGHT);
                            break;
                        }
                    }
                }

                XDeleteProperty(display, win, net_wm_state);
            }


        } else if (property == net_wm_state_fullscreen) {
            bool already_fullscreen = (attr.x == mon_x && attr.y == mon_y &&
                                       attr.width == mon_w && attr.height == mon_h);

            if (action == 1 || (action == 2 && !already_fullscreen)) {
                XMoveResizeWindow(display, target_win, mon_x, mon_y, mon_w, mon_h);
                if (is_framed) {
                    for (const auto& wp : framed_windows) {
                        if (wp.frame == target_win) {
                            XResizeWindow(display, wp.client, mon_w, mon_h - TITLE_BAR_HEIGHT);
                            break;
                        }
                    }
                }

                Atom data[2] = { net_wm_state_fullscreen, None };
                XChangeProperty(display, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<unsigned char*>(data), 1);
            } else if (action == 0 || (action == 2 && already_fullscreen)) {
                int default_w = 800;
                int default_h = 600;
                int default_x = mon_x + (mon_w - default_w) / 2;
                int default_y = mon_y + (mon_h - default_h) / 2;

                XMoveResizeWindow(display, target_win, default_x, default_y, default_w, default_h);
                if (is_framed) {
                    for (const auto& wp : framed_windows) {
                        if (wp.frame == target_win) {
                            XResizeWindow(display, wp.client, default_w, default_h - TITLE_BAR_HEIGHT);
                            break;
                        }
                    }
                }

                XDeleteProperty(display, win, net_wm_state);
            }
        }
    }

    static Atom net_wm_moveresize = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
    if ((Atom)ev->message_type == net_wm_moveresize && ev->format == 32) {
        int x_root = ev->data.l[0];
        int y_root = ev->data.l[1];
        int direction = ev->data.l[2];

        Window client = ev->window;
        Window target_window = client;
        bool is_framed = false;

        for (auto& wp : framed_windows) {
            if (wp.client == client) {
                target_window = wp.frame;
                is_framed = true;
                break;
            }
        }

        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, client, &attr)) return;
        if (attr.override_redirect || attr.map_state != IsViewable) return;

        if (direction == 8 /* _NET_WM_MOVERESIZE_MOVE */) {
            start_window_drag(target_window, x_root, y_root);
        } else {
            ResizeDirection resize_dir = static_cast<ResizeDirection>(direction);
            start_window_resize(target_window, x_root, y_root, resize_dir);
        }
    }
}
