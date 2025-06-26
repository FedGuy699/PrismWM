#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

Display* display = nullptr;
Window root;
bool running = true;
const char* display_name = nullptr;

std::string xrandr_command;
std::vector<std::string> startup_commands;

std::map<std::pair<int, unsigned int>, std::string> keybindings;

bool resize_mode = false;



std::string get_config_path() {
    const char* home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    return std::string(home) + "/.config/brooklynn/config";
}

void ensure_config_exists(const std::string& path) {
    size_t last_slash = path.rfind('/');
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash);
        struct stat st;
        if (stat(dir.c_str(), &st) == -1) {
            mkdir(dir.c_str(), 0755);
        }
    }

    std::ifstream infile(path);
    if (infile.good()) return;

    std::ofstream out(path);
    out << "\n# Startup apps here\n";
    out << "\n# feh --bg-scale Pictures/background.png\n";

    out << "# Example keybindings\n";
    out << "Mod4+B=firefox\n";
    out << "Mod4+T=ghostty\n";

    out << "Mod4+Shift+Up=move_up\n";
    out << "Mod4+Shift+Down=move_down\n";
    out << "Mod4+Shift+Left=move_left\n";
    out << "Mod4+Shift+Right=move_right\n";
    out << "Mod4+Shift+F=fullscreen\n";
    out << "\n# When holding this key resize mode is on when in resize mode you can use arrow keys to size the window\n";
    out << "Mod4+Shift+R=resize_mode\n";

    out << "\n# xrandr example command to run at startup\n";
    out << "# xrandr=--output HDMI-1 --mode 1920x1080 --rate 60\n";

    out.close();
}


int parse_modifier(const std::string& mod) {
    if (mod == "Mod4") return Mod4Mask;
    if (mod == "Shift") return ShiftMask;
    if (mod == "Control") return ControlMask;
    return 0;
}

void load_config(Display* dpy, Window root) {
    std::string path = get_config_path();
    ensure_config_exists(path);
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("xrandr=", 0) == 0) {
            xrandr_command = line.substr(7);
            continue;
        }

        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string combo = line.substr(0, eq);
            std::string command = line.substr(eq + 1);
            unsigned int mods = 0;
            std::string keyname;
            std::istringstream ss(combo);
            std::string token;
            while (std::getline(ss, token, '+')) {
                if (token.length() == 1 || token == "Left" || token == "Right" || token == "Up" || token == "Down" || token == "F") {
                    keyname = token;
                } else {
                    mods |= parse_modifier(token);
                }
            }
            KeyCode kc = XKeysymToKeycode(dpy, XStringToKeysym(keyname.c_str()));
            if (kc) {
                keybindings[{kc, mods}] = command;
                XGrabKey(dpy, kc, mods, root, True, GrabModeAsync, GrabModeAsync);
            }
            continue;
        }

        startup_commands.push_back(line);
    }
}


bool get_monitor_geometry(Display* dpy, Window win, int* x, int* y, int* w, int* h) {
    Window root = DefaultRootWindow(dpy);
    XWindowAttributes win_attr;
    if (!XGetWindowAttributes(dpy, win, &win_attr)) return false;
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res) return false;
    XRRCrtcInfo* crtc = nullptr;
    int cx = win_attr.x + win_attr.width / 2;
    int cy = win_attr.y + win_attr.height / 2;
    for (int i = 0; i < res->ncrtc; ++i) {
        crtc = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
        if (!crtc) continue;
        if (cx >= crtc->x && cx < crtc->x + crtc->width &&
            cy >= crtc->y && cy < crtc->y + crtc->height) {
            *x = crtc->x; *y = crtc->y; *w = crtc->width; *h = crtc->height;
            XRRFreeCrtcInfo(crtc);
            XRRFreeScreenResources(res);
            return true;
        }
        XRRFreeCrtcInfo(crtc);
    }
    XRRFreeScreenResources(res);
    return false;
}

bool get_primary_monitor_geometry(Display* dpy, int* x, int* y, int* w, int* h) {
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res) return false;

    RROutput primary_output = XRRGetOutputPrimary(dpy, root);
    if (primary_output == None) {
        if (res->ncrtc < 1) {
            XRRFreeScreenResources(res);
            return false;
        }
        XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, res->crtcs[0]);
        if (!crtc) {
            XRRFreeScreenResources(res);
            return false;
        }
        *x = crtc->x;
        *y = crtc->y;
        *w = crtc->width;
        *h = crtc->height;
        XRRFreeCrtcInfo(crtc);
        XRRFreeScreenResources(res);
        return true;
    }

    XRRCrtcInfo* crtc = nullptr;
    for (int i = 0; i < res->ncrtc; ++i) {
        XRRCrtcInfo* info = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
        if (!info) continue;
        for (int j = 0; j < info->noutput; ++j) {
            if (info->outputs[j] == primary_output) {
                crtc = info;
                break;
            }
        }
        if (crtc) {
            for (int k = i + 1; k < res->ncrtc; ++k) {
                XRRCrtcInfo* temp = XRRGetCrtcInfo(dpy, res, res->crtcs[k]);
                if (temp) XRRFreeCrtcInfo(temp);
            }
            break;
        } else {
            XRRFreeCrtcInfo(info);
        }
    }

    if (!crtc) {
        crtc = XRRGetCrtcInfo(dpy, res, res->crtcs[0]);
        if (!crtc) {
            XRRFreeScreenResources(res);
            return false;
        }
    }

    *x = crtc->x;
    *y = crtc->y;
    *w = crtc->width;
    *h = crtc->height;

    XRRFreeCrtcInfo(crtc);
    XRRFreeScreenResources(res);
    return true;
}


void launch(const char* cmd, int px, int py) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (display_name) setenv("DISPLAY", display_name, 1);
        const char* xauth = getenv("XAUTHORITY");
        if (xauth) setenv("XAUTHORITY", xauth, 1);
        char bufx[32], bufy[32];
        snprintf(bufx, sizeof(bufx), "%d", px);
        snprintf(bufy, sizeof(bufy), "%d", py);
        setenv("BROOKLYNN_LAUNCH_X", bufx, 1);
        setenv("BROOKLYNN_LAUNCH_Y", bufy, 1);
        int fd = open("/tmp/brooklynn.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) fd = open("/dev/null", O_WRONLY);
        if (fd != -1) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        execlp("sh", "sh", "-c", cmd, nullptr);
        std::exit(1);
    }
}

void cleanup() {
    if (display) XCloseDisplay(display);
}

void signal_handler(int) {
    running = false;
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

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask | PointerMotionMask);
    load_config(display, root);
    if (!xrandr_command.empty()) {
        std::string full_cmd = "xrandr " + xrandr_command;
        system(full_cmd.c_str());
    }

    for (const std::string& cmd : startup_commands) {
        launch(cmd.c_str(), 0, 0); 
    }



    int key_left = XKeysymToKeycode(display, XK_Left);
    int key_right = XKeysymToKeycode(display, XK_Right);
    int key_up = XKeysymToKeycode(display, XK_Up);
    int key_down = XKeysymToKeycode(display, XK_Down);

    XGrabKey(display, key_left, Mod4Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, key_right, Mod4Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, key_up, Mod4Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, key_down, Mod4Mask | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);

    XFlush(display);
    Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, cursor);

    while (running) {
        XEvent ev;
        XNextEvent(display, &ev);
        switch (ev.type) {
            case MapRequest: {
                Window w = ev.xmaprequest.window;

                int mx = 0, my = 0, mw = 0, mh = 0;
                if (!get_primary_monitor_geometry(display, &mx, &my, &mw, &mh)) {
                    mx = 100;
                    my = 100;
                }

                XReparentWindow(display, w, root, 0, 0);
                XWindowChanges ch{.x = mx, .y = my, .width = 800, .height = 600};
                XConfigureWindow(display, w, CWX | CWY | CWWidth | CWHeight, &ch);
                XMapWindow(display, w);
                XSelectInput(display, w, EnterWindowMask);
                XSetInputFocus(display, w, RevertToPointerRoot, CurrentTime);
                XRaiseWindow(display, w);
                break;
            }
            case EnterNotify:
                XSetInputFocus(display, ev.xcrossing.window, RevertToPointerRoot, CurrentTime);
                XRaiseWindow(display, ev.xcrossing.window);
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
            case KeyPress: {
                XKeyEvent key = ev.xkey;
                Window focused;
                int revert;
                XGetInputFocus(display, &focused, &revert);
                int px = 0, py = 0, pw = 0, ph = 0;
                get_primary_monitor_geometry(display, &px, &py, &pw, &ph);

                unsigned int mods = key.state & (ShiftMask | ControlMask | Mod4Mask | LockMask);
                KeySym keysym = XLookupKeysym(&key, 0);


                if (resize_mode && (key.state & Mod4Mask) && (key.state & ShiftMask)) {
                    XWindowAttributes attr;
                    if (XGetWindowAttributes(display, focused, &attr)) {
                        int dw = 0, dh = 0;
                        if (keysym == XK_Left) dw = -30;
                        if (keysym == XK_Right) dw = 30;
                        if (keysym == XK_Up) dh = -30;
                        if (keysym == XK_Down) dh = 30;
                        if (dw != 0 || dh != 0) {
                            XResizeWindow(display, focused,
                                        std::max(100, attr.width + dw),
                                        std::max(100, attr.height + dh));
                        }
                    }
                    break;
                }

                auto it = keybindings.find({key.keycode, mods});
                if (it != keybindings.end()) {
                    const std::string& action = it->second;

                    if (action == "move_left" || action == "move_right" || action == "move_up" || action == "move_down") {
                        XWindowAttributes attr;
                        if (XGetWindowAttributes(display, focused, &attr)) {
                            int dx = 0, dy = 0;
                            if (action == "move_left") dx = -30;
                            if (action == "move_right") dx = 30;
                            if (action == "move_up") dy = -30;
                            if (action == "move_down") dy = 30;
                            XMoveWindow(display, focused, attr.x + dx, attr.y + dy);
                        }
                    } else if (action == "fullscreen") {
                        if (focused != None && focused != PointerRoot) {
                            int mx, my, mw, mh;
                            if (!get_monitor_geometry(display, focused, &mx, &my, &mw, &mh)) {
                                XWindowAttributes ra;
                                XGetWindowAttributes(display, root, &ra);
                                mx = 0; my = 0; mw = ra.width; mh = ra.height;
                            }
                            XMoveResizeWindow(display, focused, mx, my, mw, mh);
                            XRaiseWindow(display, focused);
                            XFlush(display);
                        }
                    } else if (action == "resize_mode") {
                        resize_mode = true;
                        break;
                    } else {
                        launch(action.c_str(), px, py);
                    }
                }
                break;
            }
            case KeyRelease: {
                XKeyEvent key = ev.xkey;
                KeySym keysym = XLookupKeysym(&key, 0);
                if (keysym == XK_r) {
                    resize_mode = false;
                }
                break;
            }

        }
    }

    return 0;
}
