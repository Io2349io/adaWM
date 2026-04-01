/* adaWM — minimal X11 window manager
 * Copyright 2026 io2349io
 * Licensed under the Apache License, Version 2.0. See LICENSE file in repository root.*/

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#define TITLE           30 
#define BTN             14
#define MAX_CLIENTS     256
#define FONT            "Sans:size=10:bold"
#define CORNER_RADIUS   12
#define SNAP_THRESHOLD  25

#define COLOR_BLUE       0x3b82f6
#define COLOR_FRAME_BG   "#1a1a1a"
#define COLOR_TEXT       "#e0e0e0"
#define COLOR_INACTIVE   0x444444

typedef struct {
    Window win, frame;
    int x, y, w, h, screen;
    char title[256];
} Client;

Display  *dpy = NULL;
Window    root;
Atom      wm_proto, wm_delete, net_wm_name, utf8_string;
Atom      net_supported, net_client_list, net_active_win, net_supporting_wm_check;
Client   *clients[MAX_CLIENTS];
int       nclients = 0;
Visual   *visual;
Colormap  colormap;
XftFont  *xfont = NULL;
XftColor  xft_fg;

static unsigned long frame_bg_pixel, btn_red_pixel, btn_yellow_pixel, btn_green_pixel;
static int     dragging = 0;
static int     start_x, start_y, win_x, win_y;
static Window  focused_win = None;
static KeyCode key_f4, key_space, key_d, key_f11, key_tab;

Client *get_client(Window w) {
    for (int i = 0; i < nclients; i++)
        if (clients[i] && (clients[i]->win == w || clients[i]->frame == w))
            return clients[i];
    return NULL;
}

void update_client_list() {
    Window wins[MAX_CLIENTS];
    for (int i = 0; i < nclients; i++) wins[i] = clients[i]->win;
    XChangeProperty(dpy, root, net_client_list, XA_WINDOW, 32, PropModeReplace, (unsigned char *)wins, nclients);
}

void set_focus(Window w) {
    Client *old_c = get_client(focused_win);
    focused_win = w;
    Client *new_c = get_client(focused_win);
    if (old_c) { XClearWindow(dpy, old_c->frame); }
    if (new_c) {
        XSetInputFocus(dpy, new_c->win, RevertToParent, CurrentTime);
        XRaiseWindow(dpy, new_c->frame);
        XClearWindow(dpy, new_c->frame);
        XChangeProperty(dpy, root, net_active_win, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
    } else {
        XSetInputFocus(dpy, root, RevertToParent, CurrentTime);
        XDeleteProperty(dpy, root, net_active_win);
    }
}

void remove_client(Window w) {
    for (int i = 0; i < nclients; i++) {
        if (clients[i] && (clients[i]->win == w || clients[i]->frame == w)) {
            free(clients[i]);
            clients[i] = clients[--nclients];
            clients[nclients] = NULL;
            update_client_list();
            if (focused_win == w) {
                if (nclients > 0) set_focus(clients[nclients-1]->win);
                else focused_win = None;
            }
            return;
        }
    }
}

void fetch_title(Client *c) {
    Atom actual; int fmt; unsigned long n, left;
    unsigned char *data = NULL;
    c->title[0] = '\0';
    if (XGetWindowProperty(dpy, c->win, net_wm_name, 0, 255, False, utf8_string,
                           &actual, &fmt, &n, &left, &data) == Success && data) {
        strncpy(c->title, (char *)data, sizeof(c->title)-1);
        XFree(data);
    } else {
        XTextProperty tp;
        if (XGetWMName(dpy, c->win, &tp) && tp.value) {
            strncpy(c->title, (char *)tp.value, sizeof(c->title)-1);
            XFree(tp.value);
        } else strncpy(c->title, "Window", sizeof(c->title)-1);
    }
    c->title[sizeof(c->title)-1] = '\0';
}

void set_shape(Client *c, int rad) {
    int win_w = c->w, win_h = c->h + TITLE;
    if (rad <= 0) { XShapeCombineMask(dpy, c->frame, ShapeBounding, 0, 0, None, ShapeSet); return; }
    int dia = 2 * rad;
    Pixmap mask = XCreatePixmap(dpy, root, win_w, win_h, 1);
    GC gc = XCreateGC(dpy, mask, 0, NULL);
    XSetForeground(dpy, gc, 0); XFillRectangle(dpy, mask, gc, 0, 0, win_w, win_h);
    XSetForeground(dpy, gc, 1);
    XFillRectangle(dpy, mask, gc, rad, 0, win_w - dia, win_h);
    XFillRectangle(dpy, mask, gc, 0, rad, win_w, win_h - dia);
    XFillArc(dpy, mask, gc, 0, 0, dia, dia, 90*64, 90*64);
    XFillArc(dpy, mask, gc, win_w - dia, 0, dia, dia, 0*64, 90*64);
    XFillArc(dpy, mask, gc, 0, win_h - dia, dia, dia, 180*64, 90*64);
    XFillArc(dpy, mask, gc, win_w - dia, win_h - dia, dia, dia, 270*64, 90*64);
    XShapeCombineMask(dpy, c->frame, ShapeBounding, 0, 0, mask, ShapeSet);
    XFreePixmap(dpy, mask); XFreeGC(dpy, gc);
}

void draw_decorations(Client *c) {
    if (!c) return;
    int is_focused = (c->win == focused_win);
    GC gc = DefaultGC(dpy, c->screen);
    XSetForeground(dpy, gc, frame_bg_pixel);
    XFillRectangle(dpy, c->frame, gc, 0, 0, c->w, TITLE);
    XSetForeground(dpy, gc, btn_red_pixel); XFillArc(dpy, c->frame, gc, 12, (TITLE-BTN)/2, BTN, BTN, 0, 360*64);
    XSetForeground(dpy, gc, btn_yellow_pixel); XFillArc(dpy, c->frame, gc, 34, (TITLE-BTN)/2, BTN, BTN, 0, 360*64);
    XSetForeground(dpy, gc, btn_green_pixel); XFillArc(dpy, c->frame, gc, 56, (TITLE-BTN)/2, BTN, BTN, 0, 360*64);
    XftDraw *draw = XftDrawCreate(dpy, c->frame, visual, colormap);
    if (draw) {
        if (xfont) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, xfont, (FcChar8 *)c->title, strlen(c->title), &ext);
            int tx = (c->w - ext.xOff) / 2; if (tx < 80) tx = 80;
            int ty = (TITLE - (xfont->ascent + xfont->descent)) / 2 + xfont->ascent;
            XftDrawStringUtf8(draw, &xft_fg, xfont, tx, ty, (FcChar8 *)c->title, strlen(c->title));
        }
        XftDrawDestroy(draw);
    }
    XSetForeground(dpy, gc, is_focused ? COLOR_BLUE : COLOR_INACTIVE);
    XSetLineAttributes(dpy, gc, is_focused ? 2 : 1, LineSolid, CapButt, JoinRound);
    XDrawRectangle(dpy, c->frame, gc, 0, 0, c->w-1, c->h+TITLE-1);
    set_shape(c, (c->x == 0 && c->y == 0) ? 0 : CORNER_RADIUS);
}

void send_close(Window w) {
    XEvent msg = {0};
    msg.xclient.type = ClientMessage; msg.xclient.window = w;
    msg.xclient.message_type = wm_proto; msg.xclient.format = 32;
    msg.xclient.data.l[0] = (long)wm_delete; msg.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &msg);
}

int x_error_handler(Display *d, XErrorEvent *ev) { return 0; }

void setup_ewmh() {
    net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    net_active_win = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    net_supporting_wm_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Window check_win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    Atom supported[] = { net_client_list, net_active_win, net_supporting_wm_check, net_wm_name };
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, 4);
    XChangeProperty(dpy, root, net_supporting_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, net_supporting_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)"adaWM", 5);
}

int main(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i] = NULL;
    if (!(dpy = XOpenDisplay(NULL))) return 1;
    XSetErrorHandler(x_error_handler);
    root = DefaultRootWindow(dpy);
    int scr = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, scr); colormap = DefaultColormap(dpy, scr);
    wm_proto = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    setup_ewmh();
    XColor xc;
    if (XParseColor(dpy, colormap, COLOR_FRAME_BG, &xc) && XAllocColor(dpy, colormap, &xc)) frame_bg_pixel = xc.pixel;
    if (XParseColor(dpy, colormap, "#ff5f56", &xc) && XAllocColor(dpy, colormap, &xc)) btn_red_pixel = xc.pixel;
    if (XParseColor(dpy, colormap, "#ffbd2e", &xc) && XAllocColor(dpy, colormap, &xc)) btn_yellow_pixel = xc.pixel;
    if (XParseColor(dpy, colormap, "#27c93f", &xc) && XAllocColor(dpy, colormap, &xc)) btn_green_pixel = xc.pixel;
    xfont = XftFontOpenName(dpy, scr, FONT);
    XftColorAllocName(dpy, visual, colormap, COLOR_TEXT, &xft_fg);
    key_f4 = XKeysymToKeycode(dpy, XK_F4); key_space = XKeysymToKeycode(dpy, XK_space);
    key_d = XKeysymToKeycode(dpy, XK_d); key_f11 = XKeysymToKeycode(dpy, XK_F11);
    key_tab = XKeysymToKeycode(dpy, XK_Tab);
    XGrabKey(dpy, key_f4, Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, key_space, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, key_d, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, key_f11, Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, key_tab, Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XSelectInput(dpy, root, SubstructureRedirectMask|SubstructureNotifyMask|KeyPressMask);
    signal(SIGCHLD, SIG_IGN);
    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == MapRequest) {
            Window w = ev.xmaprequest.window;
            XWindowAttributes a; if (!XGetWindowAttributes(dpy, w, &a)) continue;
            Client *c = calloc(1, sizeof(Client));
            c->win = w; c->x = a.x; c->y = a.y; c->w = a.width; c->h = a.height; c->screen = scr;
            fetch_title(c);
            c->frame = XCreateSimpleWindow(dpy, root, c->x, c->y, c->w, c->h+TITLE, 0, 0, frame_bg_pixel);
            XSelectInput(dpy, c->frame, ExposureMask|ButtonPressMask|Button1MotionMask|ButtonReleaseMask);
            XSelectInput(dpy, w, PropertyChangeMask);
            XReparentWindow(dpy, w, c->frame, 0, TITLE);
            XMapWindow(dpy, w); XMapWindow(dpy, c->frame);
            if (nclients < MAX_CLIENTS) { clients[nclients++] = c; update_client_list(); set_focus(w); }
        }
        else if (ev.type == Expose && ev.xexpose.count == 0) draw_decorations(get_client(ev.xexpose.window));
        else if (ev.type == PropertyNotify && (ev.xproperty.atom == net_wm_name || ev.xproperty.atom == XA_WM_NAME)) {
            Client *c = get_client(ev.xproperty.window); if (c) { fetch_title(c); draw_decorations(c); }
        }
        else if (ev.type == ButtonPress) {
            Client *c = get_client(ev.xbutton.window);
            if (c) {
                set_focus(c->win);
                if (ev.xbutton.y < TITLE) {
                    if (ev.xbutton.x >= 12 && ev.xbutton.x <= 12+BTN) send_close(c->win);
                    else if (ev.xbutton.x >= 34 && ev.xbutton.x <= 34+BTN) XUnmapWindow(dpy, c->frame);
                    else if (ev.xbutton.x >= 56 && ev.xbutton.x <= 56+BTN) {
                        int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
                        XMoveResizeWindow(dpy, c->frame, 0, 0, sw, sh);
                        XMoveResizeWindow(dpy, c->win, 0, TITLE, sw, sh - TITLE);
                        c->x = 0; c->y = 0; c->w = sw; c->h = sh - TITLE;
                        draw_decorations(c);
                    } else { dragging = 1; start_x = ev.xbutton.x_root; start_y = ev.xbutton.y_root; win_x = c->x; win_y = c->y; }
                }
            }
        }
        else if (ev.type == MotionNotify && dragging) {
            Client *c = get_client(ev.xmotion.window);
            if (c) {
                int nx = win_x + (ev.xmotion.x_root - start_x), ny = win_y + (ev.xmotion.y_root - start_y);
                int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
                if (nx < SNAP_THRESHOLD && nx > -SNAP_THRESHOLD) nx = 0;
                if (nx + c->w > sw - SNAP_THRESHOLD && nx + c->w < sw + SNAP_THRESHOLD) nx = sw - c->w;
                if (ny < SNAP_THRESHOLD && ny > -SNAP_THRESHOLD) ny = 0;
                if (ny + c->h + TITLE > sh - SNAP_THRESHOLD && ny + c->h + TITLE < sh + SNAP_THRESHOLD) ny = sh - (c->h + TITLE);
                XMoveWindow(dpy, c->frame, nx, ny); c->x = nx; c->y = ny;
            }
        }
        else if (ev.type == ButtonRelease) dragging = 0;
        else if (ev.type == KeyPress) {
            if (ev.xkey.keycode == key_tab && (ev.xkey.state & Mod1Mask)) {
                if (nclients > 1) {
                    int next = 0;
                    for (int i = 0; i < nclients; i++) {
                        if (clients[i]->win == focused_win) {
                            next = (i + 1) % nclients;
                            break;
                        }
                    }
                    set_focus(clients[next]->win);
                }
            }
            else if ((ev.xkey.keycode == key_f4 && (ev.xkey.state & Mod1Mask)) || (ev.xkey.keycode == key_d && (ev.xkey.state & Mod4Mask))) {
                if (focused_win != None) send_close(focused_win);
            } 
            else if (ev.xkey.keycode == key_f11 && (ev.xkey.state & Mod4Mask)) {
                if (fork() == 0) { if (dpy) close(ConnectionNumber(dpy)); setsid(); execlp("xfce4-terminal", "xfce4-terminal", NULL); _exit(1); }
            }
            else if (ev.xkey.keycode == key_space && (ev.xkey.state & Mod4Mask)) {
                if (fork() == 0) { if (dpy) close(ConnectionNumber(dpy)); setsid(); execlp("rofi", "rofi", "-show", "drun", NULL); _exit(1); }
            }
        }
        else if (ev.type == DestroyNotify) {
            Window w = ev.xdestroywindow.window;
            if (get_client(w)) remove_client(w);
        }
    }
    return 0;
}
