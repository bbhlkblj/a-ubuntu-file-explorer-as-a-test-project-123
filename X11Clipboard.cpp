#include "X11Clipboard.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace X11Clipboard {

namespace {

Atom a_CLIPBOARD;
Atom a_TARGETS;
Atom a_MULTIPLE;
Atom a_text_uri_list;
Atom a_gnome_copied_files;
Atom a_UTF8_STRING;
Atom a_STRING;
Atom a_text_plain;
Atom a_text_plain_utf8;
Atom a_XdndSelection;
Atom a_XdndTypeList;

std::string g_uri_list;
std::string g_gnome_data;
bool        g_clip_valid = false;
bool        g_dnd_valid  = false;
bool        g_initialized = false;

std::string url_encode_path(const std::string& path) {
    std::string out = "file://";
    for (unsigned char c : path) {
        bool safe = std::isalnum(c) || c == '/' || c == '-' || c == '_' ||
                    c == '.'        || c == '~';
        if (safe) {
            out += (char)c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

Window selection_window() {
    Fl_Window* win = Fl::first_window();
    if (!win) return None;
    return fl_xid(win);
}

bool respond_target(const XSelectionRequestEvent& req, Atom target,
                    const void* data, int len)
{
    XChangeProperty(req.display, req.requestor, req.property,
                    target, 8, PropModeReplace,
                    (const unsigned char*)data, len);
    return true;
}

int system_handler(void* event, void* /*data*/) {
    XEvent* xev = (XEvent*)event;

    if (xev->type == SelectionRequest) {
        XSelectionRequestEvent& req = xev->xselectionrequest;

        bool is_clip = (req.selection == a_CLIPBOARD)     && g_clip_valid;
        bool is_dnd  = (req.selection == a_XdndSelection) && g_dnd_valid;
        if (!is_clip && !is_dnd) return 0;  // not ours

        XSelectionEvent reply;
        std::memset(&reply, 0, sizeof(reply));
        reply.type      = SelectionNotify;
        reply.display   = req.display;
        reply.requestor = req.requestor;
        reply.selection = req.selection;
        reply.target    = req.target;
        reply.property  = req.property;
        reply.time      = req.time;

        Atom prop = req.property == None ? req.target : req.property;

        if (req.target == a_TARGETS) {
            Atom targets[8];
            int n = 0;
            targets[n++] = a_TARGETS;
            targets[n++] = a_text_uri_list;
            if (is_clip) targets[n++] = a_gnome_copied_files;
            targets[n++] = a_UTF8_STRING;
            targets[n++] = a_STRING;
            targets[n++] = a_text_plain;
            targets[n++] = a_text_plain_utf8;

            XChangeProperty(req.display, req.requestor, prop,
                            XA_ATOM, 32, PropModeReplace,
                            (unsigned char*)targets, n);
        }
        else if (req.target == a_text_uri_list) {
            respond_target(req, a_text_uri_list,
                           g_uri_list.data(), (int)g_uri_list.size());
        }
        else if (req.target == a_gnome_copied_files && is_clip) {
            respond_target(req, a_gnome_copied_files,
                           g_gnome_data.data(), (int)g_gnome_data.size());
        }
        else if (req.target == a_UTF8_STRING || req.target == a_STRING ||
                 req.target == a_text_plain  || req.target == a_text_plain_utf8) {
            respond_target(req, req.target,
                           g_uri_list.data(), (int)g_uri_list.size());
        }
        else {
            reply.property = None;  // refuse
        }

        XSendEvent(req.display, req.requestor, False, 0, (XEvent*)&reply);
        XFlush(req.display);
        return 1;  // consumed
    }

    if (xev->type == SelectionClear) {
        const XSelectionClearEvent& clr = xev->xselectionclear;
        if (clr.selection == a_CLIPBOARD) g_clip_valid = false;
        return 0;
    }

    return 0;
}

}  // namespace

void init() {
    if (g_initialized) return;
    fl_open_display();
    if (!fl_display) return;

    Display* d = fl_display;
    a_CLIPBOARD          = XInternAtom(d, "CLIPBOARD",                    False);
    a_TARGETS            = XInternAtom(d, "TARGETS",                      False);
    a_MULTIPLE           = XInternAtom(d, "MULTIPLE",                     False);
    a_text_uri_list      = XInternAtom(d, "text/uri-list",                False);
    a_gnome_copied_files = XInternAtom(d, "x-special/gnome-copied-files", False);
    a_UTF8_STRING        = XInternAtom(d, "UTF8_STRING",                  False);
    a_STRING             = XInternAtom(d, "STRING",                       False);
    a_text_plain         = XInternAtom(d, "text/plain",                   False);
    a_text_plain_utf8    = XInternAtom(d, "text/plain;charset=utf-8",     False);
    a_XdndSelection      = XInternAtom(d, "XdndSelection",                False);
    a_XdndTypeList       = XInternAtom(d, "XdndTypeList",                 False);

    Fl::add_system_handler(system_handler, nullptr);
    g_initialized = true;
}

static std::string build_uri_list(const std::vector<std::string>& paths) {
    std::string out;
    for (const auto& p : paths) {
        out += url_encode_path(p);
        out += "\r\n";
    }
    return out;
}

void set(Op op, const std::vector<std::string>& paths) {
    init();
    if (paths.empty() || !fl_display) return;

    g_uri_list = build_uri_list(paths);

    std::string gnome = (op == CUT ? "cut\n" : "copy\n");
    for (size_t i = 0; i < paths.size(); i++) {
        gnome += url_encode_path(paths[i]);
        if (i + 1 < paths.size()) gnome += "\n";
    }
    g_gnome_data = std::move(gnome);
    g_clip_valid = true;

    Window xid = selection_window();
    if (xid == None) return;
    XSetSelectionOwner(fl_display, a_CLIPBOARD, xid, CurrentTime);
    XFlush(fl_display);
}

void clear() {
    g_clip_valid = false;
    if (fl_display)
        XSetSelectionOwner(fl_display, a_CLIPBOARD, None, CurrentTime);
}

bool owns_clipboard() { return g_clip_valid; }

void prepare_dnd(const std::vector<std::string>& paths) {
    init();
    if (paths.empty() || !fl_display) return;

    g_uri_list  = build_uri_list(paths);
    g_dnd_valid = true;

    Window xid = selection_window();
    if (xid == None) return;
    Atom types[] = { a_text_uri_list, a_UTF8_STRING, a_STRING, a_text_plain };
    XChangeProperty(fl_display, xid, a_XdndTypeList, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)types,
                    sizeof(types) / sizeof(types[0]));
    XFlush(fl_display);
}

void done_dnd() {
    g_dnd_valid = false;
}

}  // namespace X11Clipboard
