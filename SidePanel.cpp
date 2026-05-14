#include "SidePanel.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace fs = std::filesystem;

namespace {

struct MountInfo {
    std::string device;
    std::string path;
    std::string fstype;
};

std::string unescape_mount(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 3 < s.size()
            && std::isdigit((unsigned char)s[i+1])
            && std::isdigit((unsigned char)s[i+2])
            && std::isdigit((unsigned char)s[i+3])) {
            int c = (s[i+1] - '0') * 64 + (s[i+2] - '0') * 8 + (s[i+3] - '0');
            out += (char)c;
            i += 3;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool is_user_fs(const std::string& fst) {
    static const std::set<std::string> good = {
        "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "reiserfs", "jfs",
        "ntfs", "ntfs3", "fuseblk",
        "vfat", "exfat", "msdos",
        "iso9660", "udf",
        "cifs", "nfs", "nfs4", "smbfs",
    };
    return good.count(fst) > 0;
}

bool is_skippable_path(const std::string& p) {
    static const char* prefixes[] = {
        "/boot", "/snap", "/var/snap", "/var/lib/snapd",
        "/var/lib/docker", "/run/", "/proc", "/sys", "/dev", "/tmp",
    };
    for (const char* pre : prefixes) {
        size_t n = std::strlen(pre);
        if (p.size() >= n && p.compare(0, n, pre) == 0) {
            // Allow /run/media specifically (where many distros mount removables)
            if (std::string(pre) == "/run/" && p.rfind("/run/media/", 0) == 0)
                return false;
            return true;
        }
    }
    return false;
}

std::vector<MountInfo> enumerate_mounts() {
    std::vector<MountInfo> result;
    std::ifstream f("/proc/mounts");
    if (!f) return result;

    std::set<std::string> seen;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string dev, mnt, fst;
        if (!(ss >> dev >> mnt >> fst)) continue;
        if (!is_user_fs(fst)) continue;

        std::string path = unescape_mount(mnt);
        if (path == "/") continue;             // shown explicitly as Root
        if (is_skippable_path(path)) continue;
        if (!seen.insert(path).second) continue;

        result.push_back({unescape_mount(dev), path, fst});
    }
    return result;
}

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? std::string(h) : "/";
}

}  // namespace

SidePanel::SidePanel(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h)
{
    rebuild_items();
}

void SidePanel::set_pinned_paths(const std::vector<std::string>& paths) {
    pinned_paths = paths;
    rebuild_items();
}

void SidePanel::rebuild_items() {
    items.clear();

    auto push_header    = [&](const char* label) {
        items.push_back({Item::HEADER, label, 0, "", false, false});
    };
    auto push_separator = [&] {
        items.push_back({Item::SEPARATOR, "", 0, "", false, false});
    };
    auto push_entry = [&](const std::string& label, Fl_Color c,
                          const std::string& p, bool pinned = false,
                          bool missing = false) {
        items.push_back({Item::ENTRY, label, c, p, pinned, missing});
    };

    std::string home = home_dir();

    // Favourites
    push_header("Recent");
    push_separator();
    push_entry("Home",      fl_rgb_color( 99, 102, 241), home);
    push_entry("Desktop",   fl_rgb_color( 59, 130, 246), home + "/Desktop");
    push_entry("Downloads", fl_rgb_color( 34, 197,  94), home + "/Downloads");
    push_entry("Documents", fl_rgb_color(249, 115,  22), home + "/Documents");
    push_entry("Music",     fl_rgb_color(236,  72, 153), home + "/Music");
    push_entry("Pictures",  fl_rgb_color(  6, 182, 212), home + "/Pictures");
    push_entry("Videos",    fl_rgb_color(239,  68,  68), home + "/Videos");
    push_entry("Trash",     fl_rgb_color(156, 163, 175), home + "/.Trash");
    push_separator();

    // Other Locations
    push_header("Other Locations");
    push_entry("Root", fl_rgb_color(75, 85, 99), "/");

    for (const auto& m : enumerate_mounts()) {
        std::string label = fs::path(m.path).filename().string();
        if (label.empty()) label = m.path;
        push_entry(label, fl_rgb_color(20, 184, 166), m.path);
    }
    push_separator();

    // Pinned
    push_header("Pinned");

    static const Fl_Color palette[] = {
        fl_rgb_color(168,  85, 247),
        fl_rgb_color(244, 114, 182),
        fl_rgb_color( 14, 165, 233),
        fl_rgb_color( 16, 185, 129),
        fl_rgb_color(245, 158,  11),
    };

    for (size_t i = 0; i < pinned_paths.size(); i++) {
        const std::string& p = pinned_paths[i];
        std::string label = fs::path(p).filename().string();
        if (label.empty()) label = p;

        std::error_code ec;
        bool exists = fs::exists(p, ec);
        push_entry(label,
                   palette[i % (sizeof(palette) / sizeof(palette[0]))],
                   p, /*pinned=*/true, /*missing=*/!exists);
    }

    if (selected >= (int)items.size()) selected = -1;
    if (hovered  >= (int)items.size()) hovered  = -1;
    redraw();
}

int SidePanel::item_height(int i) const {
    switch (items[i].type) {
        case Item::HEADER:    return HEADER_H;
        case Item::SEPARATOR: return SEP_H;
        case Item::ENTRY:     return ENTRY_H;
    }
    return 0;
}

int SidePanel::item_y(int i) const {
    int iy = y() + TOP_PAD;
    for (int j = 0; j < i; j++) iy += item_height(j);
    return iy;
}

int SidePanel::entry_at(int my) const {
    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].type != Item::ENTRY) continue;
        int iy = item_y(i);
        if (my >= iy && my < iy + ENTRY_H) return i;
    }
    return -1;
}

void SidePanel::set_selected(int i) {
    selected = i;
    redraw();
}

void SidePanel::draw() {
    fl_color(fl_rgb_color(249, 250, 251));
    fl_rectf(x(), y(), w(), h());

    for (int i = 0; i < (int)items.size(); i++) {
        const auto& item = items[i];
        int iy = item_y(i);

        switch (item.type) {
            case Item::HEADER:
                fl_font(FL_HELVETICA, 11);
                fl_color(fl_rgb_color(156, 163, 175));
                fl_draw(item.label.c_str(),
                        x() + ICON_X, iy, w() - ICON_X * 2, HEADER_H,
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                break;

            case Item::SEPARATOR:
                fl_color(fl_rgb_color(229, 231, 235));
                fl_line(x() + 8, iy + SEP_H / 2, x() + w() - 8, iy + SEP_H / 2);
                break;

            case Item::ENTRY: {
                bool sel = (i == selected);
                bool hov = (i == hovered && !sel);

                if (sel) {
                    fl_color(fl_rgb_color(224, 231, 255));
                    fl_rectf(x() + 4, iy + 2, w() - 8, ENTRY_H - 4);
                    fl_color(fl_rgb_color(99, 102, 241));
                    fl_rectf(x() + 4, iy + 2, 3, ENTRY_H - 4);
                } else if (hov) {
                    fl_color(fl_rgb_color(238, 242, 255));
                    fl_rectf(x() + 4, iy + 2, w() - 8, ENTRY_H - 4);
                }

                int icon_y = iy + (ENTRY_H - ICON_SIZE) / 2;
                Fl_Color icon_c = item.icon_color;
                if (item.missing) icon_c = fl_rgb_color(209, 213, 219);
                fl_color(icon_c);
                fl_rectf(x() + ICON_X + 6, icon_y, ICON_SIZE, ICON_SIZE);

                fl_font(FL_HELVETICA, 14);
                Fl_Color tc;
                if (item.missing) tc = fl_rgb_color(156, 163, 175);
                else if (sel)     tc = fl_rgb_color(55, 48, 163);
                else              tc = fl_rgb_color(55, 65, 81);
                fl_color(tc);
                fl_draw(item.label.c_str(),
                        x() + TEXT_X + 4, iy, w() - TEXT_X - 12, ENTRY_H,
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                break;
            }
        }
    }

    // Right border
    fl_color(fl_rgb_color(229, 231, 235));
    fl_line(x() + w() - 1, y(), x() + w() - 1, y() + h() - 1);
}

void SidePanel::show_pinned_menu(int idx, int mx, int my) {
    if (idx < 0 || idx >= (int)items.size()) return;
    const Item& it = items[idx];
    if (!it.is_pinned) return;

    enum { A_OPEN = 1, A_UNPIN };

    Fl_Menu_Item m[] = {
        { "Open",  0, nullptr, (void*)(intptr_t)A_OPEN,  FL_MENU_DIVIDER, 0, 0, 13, 0 },
        { "Unpin", 0, nullptr, (void*)(intptr_t)A_UNPIN, 0,              0, 0, 13, 0 },
        { 0 }
    };

    const Fl_Menu_Item* sel = m->popup(mx, my);
    if (!sel) return;
    switch ((int)(intptr_t)sel->user_data()) {
        case A_OPEN:
            if (on_select_cb) on_select_cb(it.path);
            break;
        case A_UNPIN:
            if (on_unpin_cb) on_unpin_cb(it.path);
            break;
    }
}

int SidePanel::handle(int event) {
    switch (event) {
        case FL_ENTER:
        case FL_MOVE: {
            int prev = hovered;
            hovered = entry_at(Fl::event_y());
            if (hovered != prev) redraw();
            return 1;
        }
        case FL_LEAVE:
            if (hovered != -1) { hovered = -1; redraw(); }
            return 1;
        case FL_PUSH: {
            int idx = entry_at(Fl::event_y());
            if (idx < 0) break;

            if (Fl::event_button() == FL_RIGHT_MOUSE) {
                if (items[idx].is_pinned) {
                    show_pinned_menu(idx, Fl::event_x(), Fl::event_y());
                }
                return 1;
            }

            selected = idx;
            redraw();
            if (on_select_cb && !items[idx].path.empty())
                on_select_cb(items[idx].path);
            return 1;
        }
    }
    return Fl_Widget::handle(event);
}
