#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <malloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

struct Monitor {
    int x, y;
    unsigned int width, height;
};

void setpaper(const std::string& imagePath) {
    int img_w, img_h, channels;
    unsigned char* data = stbi_load(imagePath.c_str(), &img_w, &img_h, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load image: " << imagePath << "\n";
        return;
    }

    Display* display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "Cannot open X display\n";
        stbi_image_free(data);
        return;
    }

    Window root = DefaultRootWindow(display);
    XRRScreenResources* screenRes = XRRGetScreenResources(display, root);
    if (!screenRes) {
        std::cerr << "Failed to get screen resources\n";
        stbi_image_free(data);
        XCloseDisplay(display);
        return;
    }

    std::vector<Monitor> monitors;
    unsigned int total_width = 0;
    unsigned int total_height = 0;

    for (int i = 0; i < screenRes->ncrtc; ++i) {
        XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(display, screenRes, screenRes->crtcs[i]);
        if (!crtcInfo || crtcInfo->width == 0 || crtcInfo->height == 0) {
            if (crtcInfo) XRRFreeCrtcInfo(crtcInfo);
            continue;
        }
        
        monitors.push_back({
            static_cast<int>(crtcInfo->x),
            static_cast<int>(crtcInfo->y),
            crtcInfo->width,
            crtcInfo->height
        });
        
        total_width = std::max(total_width, static_cast<unsigned int>(crtcInfo->x) + crtcInfo->width);
        total_height = std::max(total_height, static_cast<unsigned int>(crtcInfo->y) + crtcInfo->height);
        
        XRRFreeCrtcInfo(crtcInfo);
    }

    XRRFreeScreenResources(screenRes);

    unsigned char* final_img = (unsigned char*)calloc(total_width * total_height * 4, 1);
    if (!final_img) {
        std::cerr << "Memory allocation failed\n";
        stbi_image_free(data);
        XCloseDisplay(display);
        return;
    }

    for (const Monitor& m : monitors) {
        unsigned char* resized = (unsigned char*)malloc(m.width * m.height * 4);
        if (!resized) {
            std::cerr << "Memory allocation failed for monitor " << m.width << "x" << m.height << "\n";
            continue;
        }

        stbir_resize_uint8(data, img_w, img_h, 0, resized, m.width, m.height, 0, 4);

        for (unsigned int y = 0; y < m.height; ++y) {
            for (unsigned int x = 0; x < m.width; ++x) {
                size_t src_idx = (y * m.width + x) * 4;
                size_t dst_idx = ((m.y + y) * total_width + (m.x + x)) * 4;
                memcpy(&final_img[dst_idx], &resized[src_idx], 4);
            }
        }

        free(resized);
    }

    stbi_image_free(data);
    data = nullptr;

    int screen = DefaultScreen(display);
    char* ximg_data = (char*)malloc(total_width * total_height * 4);
    if (!ximg_data) {
        std::cerr << "Failed to allocate XImage data\n";
        free(final_img);
        XCloseDisplay(display);
        return;
    }

    XImage* img = XCreateImage(display, DefaultVisual(display, screen), 24, ZPixmap, 0,
                             ximg_data, total_width, total_height, 32, 0);
    if (!img) {
        std::cerr << "Failed to create XImage\n";
        free(ximg_data);
        free(final_img);
        XCloseDisplay(display);
        return;
    }

    for (unsigned int y = 0; y < total_height; ++y) {
        for (unsigned int x = 0; x < total_width; ++x) {
            size_t idx = (y * total_width + x) * 4;
            unsigned char r = final_img[idx + 0];
            unsigned char g = final_img[idx + 1];
            unsigned char b = final_img[idx + 2];
            unsigned long pixel = (r << 16) | (g << 8) | b;
            XPutPixel(img, x, y, pixel);
        }
    }

    free(final_img);
    final_img = nullptr;

    Pixmap pixmap = XCreatePixmap(display, root, total_width, total_height, DefaultDepth(display, screen));
    if (pixmap) {
        GC gc = XCreateGC(display, pixmap, 0, NULL);
        XPutImage(display, pixmap, gc, img, 0, 0, 0, 0, total_width, total_height);
        XFreeGC(display, gc);

        XSetWindowBackgroundPixmap(display, root, pixmap);
        XClearWindow(display, root);
        XFlush(display);
        XFreePixmap(display, pixmap);
    }

    if (img) {
        free(img->data);
        img->data = nullptr;
        XDestroyImage(img);
    }

    XCloseDisplay(display);
    malloc_trim(0);
}