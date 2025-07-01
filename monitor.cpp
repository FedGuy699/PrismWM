#include "monitor.h"
#include <algorithm>




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

bool get_monitor_geometry_for_window(Display* dpy, Window win, int* x, int* y, int* w, int* h) {
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, win, &attr)) return false;

    Window root = DefaultRootWindow(dpy);
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res) return false;

    int best_area = 0;
    bool found = false;

    for (int i = 0; i < res->ncrtc; ++i) {
        XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
        if (crtc->mode == None) {
            XRRFreeCrtcInfo(crtc);
            continue;
        }

        int inter_x = std::max(attr.x, static_cast<int>(crtc->x));
        int inter_y = std::max(attr.y, static_cast<int>(crtc->y));
        int inter_w = std::min(attr.x + static_cast<int>(attr.width), static_cast<int>(crtc->x + crtc->width)) - inter_x;
        int inter_h = std::min(attr.y + static_cast<int>(attr.height), static_cast<int>(crtc->y + crtc->height)) - inter_y;


        if (inter_w > 0 && inter_h > 0) {
            int area = inter_w * inter_h;
            if (area > best_area) {
                best_area = area;
                *x = crtc->x;
                *y = crtc->y;
                *w = crtc->width;
                *h = crtc->height;
                found = true;
            }
        }

        XRRFreeCrtcInfo(crtc);
    }

    XRRFreeScreenResources(res);
    return found;
}
