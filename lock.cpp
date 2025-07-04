#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h> 

#include <security/pam_appl.h> 

#include <ctime>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <pwd.h>
#include <string>
#include <algorithm>
#include <cctype>

extern Display* display;
extern Window root;

Window lock_win;
GC gc;
XftDraw* xft_draw;
XftFont* font;
XftColor xft_color;
Visual* visual;
Colormap colormap;
int screen;

Pixmap back_buffer;

const int refresh_interval = 0.1;

int primary_x = 0, primary_y = 0, primary_width = 0, primary_height = 0;

int virtual_width = 0;
int virtual_height = 0;

std::string input_password;

static void handle_button_press(XButtonEvent* ev) {}
static void handle_button_release(XButtonEvent* ev) {}
static void handle_motion_notify(XMotionEvent* ev) {}

std::string get_time_string() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", t);
    return buf;
}

void clear_back_buffer() {
    XSetForeground(display, gc, BlackPixel(display, screen));
    XFillRectangle(display, back_buffer, gc, 0, 0, virtual_width, virtual_height);
}


void draw_time() {
    std::string time_str = get_time_string();
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, font, (FcChar8*)time_str.c_str(), time_str.length(), &extents);

    int x = primary_x + (primary_width - extents.xOff) / 2;
    int y = primary_y + (primary_height + font->ascent) / 2;


    XftDrawStringUtf8(xft_draw, &xft_color, font, x, y, (FcChar8*)time_str.c_str(), time_str.length());
}

void draw_password_box() {
    int box_width = 400;
    int box_height = 50;
    int box_x = primary_x + (primary_width - box_width) / 2;
    int box_y = primary_y + (primary_height + font->ascent) / 2 + 60;


    XSetForeground(display, gc, BlackPixel(display, screen));
    XFillRectangle(display, back_buffer, gc, box_x, box_y, box_width, box_height);

    XSetForeground(display, gc, WhitePixel(display, screen));
    XDrawRectangle(display, back_buffer, gc, box_x, box_y, box_width, box_height);

    std::string masked(input_password.size(), '*');

    XGlyphInfo extents;
    XftTextExtentsUtf8(display, font, (FcChar8*)masked.c_str(), masked.length(), &extents);

    int text_x = box_x + 10;
    int text_y = box_y + (box_height + font->ascent) / 2 - 5;

    XftDrawStringUtf8(xft_draw, &xft_color, font, text_x, text_y,
                      (FcChar8*)masked.c_str(), masked.length());
}

static int pam_conv_func(int num_msg, const struct pam_message** msg,
                        struct pam_response** resp, void* appdata_ptr) {
    if (num_msg <= 0)
        return PAM_CONV_ERR;

    struct pam_response* responses = (struct pam_response*)calloc(num_msg, sizeof(struct pam_response));
    if (!responses)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            responses[i].resp = strdup((const char*)appdata_ptr);
            responses[i].resp_retcode = 0;
        } else {
            responses[i].resp = nullptr;
            responses[i].resp_retcode = 0;
        }
    }

    *resp = responses;
    return PAM_SUCCESS;
}

bool verify_password(const std::string& password) {
    const char* user = getlogin();
    if (!user) return false;

    struct pam_conv pam_conv = { pam_conv_func, (void*)password.c_str() };
    pam_handle_t* pamh = nullptr;

    int pam_err = pam_start("login", user, &pam_conv, &pamh);
    if (pam_err != PAM_SUCCESS)
        return false;

    pam_err = pam_authenticate(pamh, 0);
    pam_end(pamh, pam_err);

    return pam_err == PAM_SUCCESS;
}

void lock() {
    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    visual = DefaultVisual(display, screen);
    colormap = DefaultColormap(display, screen);

    int width = 0, height = 0;

    XRRScreenResources* res = XRRGetScreenResources(display, root);
    RROutput primary = XRRGetOutputPrimary(display, root);

    for (int i = 0; i < res->noutput; ++i) {
        if (res->outputs[i] == primary) {
            XRROutputInfo* output_info = XRRGetOutputInfo(display, res, primary);
            if (output_info && output_info->crtc != 0) {
                XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, res, output_info->crtc);
                primary_x = crtc_info->x;
                primary_y = crtc_info->y;
                primary_width = crtc_info->width;
                primary_height = crtc_info->height;
                XRRFreeCrtcInfo(crtc_info);
            }
            XRRFreeOutputInfo(output_info);
        }

        XRROutputInfo* info = XRRGetOutputInfo(display, res, res->outputs[i]);
        if (info && info->crtc != 0) {
            XRRCrtcInfo* crtc = XRRGetCrtcInfo(display, res, info->crtc);
            int right = crtc->x + crtc->width;
            int bottom = crtc->y + crtc->height;
            width = std::max(width, right);
            height = std::max(height, bottom);
            virtual_width = width;
            virtual_height = height;

            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeOutputInfo(info);
        
    }
    XRRFreeScreenResources(res);

    lock_win = XCreateSimpleWindow(display, root, 0, 0, width, height, 0,
                                BlackPixel(display, screen), BlackPixel(display, screen));



    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(display, lock_win, CWOverrideRedirect, &attrs);

    XMapRaised(display, lock_win);
    XSelectInput(display, lock_win, ExposureMask | KeyPressMask);

    gc = XCreateGC(display, lock_win, 0, nullptr);
    font = XftFontOpenName(display, screen, "monospace-36");

    back_buffer = XCreatePixmap(display, lock_win, width, height, DefaultDepth(display, screen));

    xft_draw = XftDrawCreate(display, back_buffer, visual, colormap);

    XRenderColor render_color = { 0xffff, 0xffff, 0xffff, 0xffff };
    XftColorAllocValue(display, visual, colormap, &render_color, &xft_color);

    while (true) {
        while (XPending(display)) {
            XEvent ev;
            XNextEvent(display, &ev);

            if (ev.type == Expose) {
                clear_back_buffer();
                draw_time();
                draw_password_box();

                XCopyArea(display, back_buffer, lock_win, gc, 0, 0, virtual_width, virtual_height, 0, 0);
                XFlush(display);
            } else if (ev.type == KeyPress) {
                char buf[32];
                KeySym keysym;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &keysym, nullptr);
                buf[len] = '\0';

                if (keysym == XK_Return) {
                    if (verify_password(input_password)) {
                        input_password.clear();
                        XFreePixmap(display, back_buffer);
                        XDestroyWindow(display, lock_win);
                        return;
                    } else {
                        input_password.clear(); 
                    }
                } else if (keysym == XK_BackSpace) {
                    if (!input_password.empty())
                        input_password.pop_back();
                } else if (len > 0 && isprint(buf[0])) {
                    input_password += buf[0];
                }

                clear_back_buffer();
                draw_time();
                draw_password_box();

                XCopyArea(display, back_buffer, lock_win, gc, 0, 0, width, height, 0, 0);
                XFlush(display);
            }
        }

        clear_back_buffer();
        draw_time();
        draw_password_box();
        XCopyArea(display, back_buffer, lock_win, gc, 0, 0, width, height, 0, 0);
        XFlush(display);
        usleep((int)(refresh_interval * 1e6));
    }
}
