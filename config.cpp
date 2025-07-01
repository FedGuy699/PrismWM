#include <X11/Xlib.h>
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <X11/cursorfont.h>

#include "config.h"

std::string xrandr_command;
std::vector<std::string> startup_commands;
std::map<std::pair<int, unsigned int>, std::string> keybindings;


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
    out << "\n# Uncomment this and install a polkit agent if you want to have permissions in certain apps\n";
    out << "# If you would like to use this example (sudo pacman -S polkit-gnome)\n";
    out << "# /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1\n";

    out << "\n# Example keybindings\n";
    out << "Mod4+B=firefox\n";
    out << "Mod4+T=alacritty\n";
    out << "# Uncomment for rofi\n";
    out << "# Mod4+R=rofi -show drun\n";

    out << "\n# xrandr example command to run at startup\n";
    out << "# xrandr=--output HDMI-1 --mode 1920x1080 --rate 60\n";
    out.close();

    std::string msg = "Brooklynn config created at " + path;
    show_config_created_bar(msg);
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

std::string get_config_path() {
    const char* home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    return std::string(home) + "/.config/brooklynn/config";
}

void show_config_created_bar(const std::string& message) {
    int screen = DefaultScreen(display);
    Window bar = XCreateSimpleWindow(display, RootWindow(display, screen),
                                     0, 0, DisplayWidth(display, screen), 30, 0,
                                     BlackPixel(display, screen), WhitePixel(display, screen));

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(display, bar, CWOverrideRedirect, &attrs);

    XSelectInput(display, bar, ExposureMask | ButtonPressMask);
    XMapWindow(display, bar);

    Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, bar, cursor);

    GC gc = XCreateGC(display, bar, 0, nullptr);
    XSetForeground(display, gc, BlackPixel(display, screen));

    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, bar, &wm_delete, 1);

    XEvent ev;
    bool showing = true;

    while (showing) {
        XNextEvent(display, &ev);
        if (ev.type == Expose) {
            XDrawString(display, bar, gc, 10, 20, message.c_str(), message.size());
            XDrawString(display, bar, gc, DisplayWidth(display, screen) - 30, 20, "[X]", 3);
        } else if (ev.type == ButtonPress) {
            int mx = ev.xbutton.x;
            int w = DisplayWidth(display, screen);
            if (mx >= w - 40) {
                showing = false;
            }
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) {
                showing = false;
            }
        }
    }

    XDestroyWindow(display, bar);
    XFreeCursor(display, cursor);
    XFreeGC(display, gc);
    XFlush(display);
}
