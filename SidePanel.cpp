#include "SidePanel.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Menu_Item.H>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <set>

#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

struct DriveInfo {
    std::string device;      // e.g. /dev/sdb2
    std::string mountpoint;  // empty if not currently mounted
    std::string fstype;
    std::string label;
    std::string size;        // human-readable, as reported by lsblk
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

// Parse one line of `lsblk -pPno ...` output, which has the form
// `KEY1="value1" KEY2="value2" ...`. Values are double-quoted; spaces in
// values are preserved.
std::map<std::string, std::string> parse_lsblk_pairs(const std::string& line) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= line.size()) break;

        size_t key_start = i;
        while (i < line.size() && line[i] != '=' && line[i] != ' ') i++;
        if (i >= line.size() || line[i] != '=') break;
        std::string key = line.substr(key_start, i - key_start);
        i++;  // '='

        if (i >= line.size() || line[i] != '"') break;
        i++;  // opening quote
        std::string val;
        while (i < line.size() && line[i] != '"') {
            if (line[i] == '\\' && i + 1 < line.size()) {
                val += line[i + 1];
                i += 2;
            } else {
                val += line[i++];
            }
        }
        if (i < line.size()) i++;  // closing quote
        out[key] = val;
    }
    return out;
}

// Enumerate partitions with a known filesystem (mounted or not). Returns
// nothing if `lsblk` is unavailable.
std::vector<DriveInfo> enumerate_drives() {
    std::vector<DriveInfo> result;
    FILE* p = popen("lsblk -pPno NAME,TYPE,FSTYPE,SIZE,LABEL,MOUNTPOINT "
                    "2>/dev/null", "r");
    if (!p) return result;

    std::set<std::string> seen_devices;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), p)) {
        std::string line = buf;
        if (!line.empty() && line.back() == '\n') line.pop_back();

        auto pairs = parse_lsblk_pairs(line);
        if (pairs["TYPE"] != "part") continue;
        const std::string& fst = pairs["FSTYPE"];
        if (!is_user_fs(fst)) continue;

        const std::string& mp = pairs["MOUNTPOINT"];
        if (mp == "/") continue;                              // shown as Root
        if (!mp.empty() && is_skippable_path(mp)) continue;   // /boot etc.

        const std::string& dev = pairs["NAME"];
        if (!seen_devices.insert(dev).second) continue;
        result.push_back({dev, mp, fst, pairs["LABEL"], pairs["SIZE"]});
    }
    pclose(p);
    return result;
}

// Look up the mountpoint for a given device in /proc/mounts. Used after a
// successful `udisksctl mount` to find where the device landed.
std::string mountpoint_for_device(const std::string& device) {
    std::ifstream f("/proc/mounts");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string dev, mnt;
        if (!(ss >> dev >> mnt)) continue;
        if (unescape_mount(dev) == device) return unescape_mount(mnt);
    }
    return "";
}

bool run_udisksctl_mount(const std::string& device, std::string& message_out) {
    std::string cmd = "udisksctl mount -b '";
    for (char c : device) {
        if (c == '\'') cmd += "'\\''";
        else           cmd += c;
    }
    cmd += "' 2>&1";

    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { message_out = "Could not invoke udisksctl."; return false; }

    char buf[512];
    while (std::fgets(buf, sizeof(buf), p)) message_out += buf;
    int code = pclose(p);
    return WIFEXITED(code) && WEXITSTATUS(code) == 0;
}

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? std::string(h) : "/";
}

}  // namespace

SidePanel::SidePanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    box(FL_NO_BOX);
    scrollbar = new Fl_Scrollbar(x + w - SB_W, y, SB_W, h);
    scrollbar->callback(scrollbar_cb, this);
    end();
    rebuild_items();
}

void SidePanel::scrollbar_cb(Fl_Widget* w, void* ud) {
    auto* self = static_cast<SidePanel*>(ud);
    self->scroll_y = static_cast<Fl_Scrollbar*>(w)->value();
    self->redraw();
}

int SidePanel::content_height() const {
    int total = TOP_PAD;
    for (int i = 0; i < (int)items.size(); i++) total += item_height(i);
    return total + TOP_PAD;
}

void SidePanel::sync_scrollbar() {
    int total = content_height();
    int vis   = h();
    int max_s = std::max(0, total - vis);
    if (scroll_y > max_s) scroll_y = max_s;
    if (scroll_y < 0)     scroll_y = 0;
    scrollbar->bounds(0, max_s);
    scrollbar->slider_size(total > 0 ? std::min(1.0, (double)vis / total) : 1.0);
    scrollbar->linesize(ENTRY_H);
    scrollbar->value(scroll_y);
    if (max_s == 0) scrollbar->deactivate();
    else            scrollbar->activate();
}

void SidePanel::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    scrollbar->resize(X + W - SB_W, Y, SB_W, H);
    sync_scrollbar();
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

    auto drives = enumerate_drives();
    // Mounted entries first, then unmounted ones (shown grayed out).
    std::sort(drives.begin(), drives.end(),
              [](const DriveInfo& a, const DriveInfo& b) {
                  if (a.mountpoint.empty() != b.mountpoint.empty())
                      return !a.mountpoint.empty();
                  return a.device < b.device;
              });

    for (const auto& d : drives) {
        std::string label = d.label;
        if (label.empty()) {
            label = d.mountpoint.empty()
                ? fs::path(d.device).filename().string()
                : fs::path(d.mountpoint).filename().string();
        }
        if (label.empty()) label = d.device;
        if (!d.size.empty()) label += "  (" + d.size + ")";

        Item it;
        it.type       = Item::ENTRY;
        it.label      = label;
        it.icon_color = fl_rgb_color(20, 184, 166);
        it.path       = d.mountpoint;
        it.device     = d.device;
        it.unmounted  = d.mountpoint.empty();
        items.push_back(std::move(it));
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
    sync_scrollbar();
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
    int iy = y() + TOP_PAD - scroll_y;
    for (int j = 0; j < i; j++) iy += item_height(j);
    return iy;
}

int SidePanel::entry_at(int my) const {
    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].type != Item::ENTRY) continue;
        int iy = item_y(i);
        if (iy + ENTRY_H <= y()) continue;       // above viewport
        if (iy >= y() + h())     break;          // below viewport (and beyond)
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

    int content_w = w() - (scrollbar->active() ? SB_W : 0);
    fl_push_clip(x(), y(), content_w, h());

    for (int i = 0; i < (int)items.size(); i++) {
        const auto& item = items[i];
        int iy = item_y(i);

        switch (item.type) {
            case Item::HEADER:
                fl_font(FL_HELVETICA, 11);
                fl_color(fl_rgb_color(156, 163, 175));
                fl_draw(item.label.c_str(),
                        x() + ICON_X, iy, content_w - ICON_X * 2, HEADER_H,
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                break;

            case Item::SEPARATOR:
                fl_color(fl_rgb_color(229, 231, 235));
                fl_line(x() + 8, iy + SEP_H / 2,
                        x() + content_w - 8, iy + SEP_H / 2);
                break;

            case Item::ENTRY: {
                bool sel = (i == selected);
                bool hov = (i == hovered && !sel);

                if (sel) {
                    fl_color(fl_rgb_color(224, 231, 255));
                    fl_rectf(x() + 4, iy + 2, content_w - 8, ENTRY_H - 4);
                    fl_color(fl_rgb_color(99, 102, 241));
                    fl_rectf(x() + 4, iy + 2, 3, ENTRY_H - 4);
                } else if (hov) {
                    fl_color(fl_rgb_color(238, 242, 255));
                    fl_rectf(x() + 4, iy + 2, content_w - 8, ENTRY_H - 4);
                }

                int icon_y = iy + (ENTRY_H - ICON_SIZE) / 2;
                Fl_Color icon_c = item.icon_color;
                if (item.missing) {
                    icon_c = fl_rgb_color(209, 213, 219);
                } else if (item.unmounted) {
                    // Blend toward white so the drive's accent colour is
                    // still visible, just muted.
                    uchar r, g, b;
                    Fl::get_color(icon_c, r, g, b);
                    icon_c = fl_rgb_color((uchar)((r + 235) / 2),
                                          (uchar)((g + 235) / 2),
                                          (uchar)((b + 235) / 2));
                }
                fl_color(icon_c);
                fl_rectf(x() + ICON_X + 6, icon_y, ICON_SIZE, ICON_SIZE);

                fl_font(FL_HELVETICA, 14);
                Fl_Color tc;
                if (item.missing)        tc = fl_rgb_color(156, 163, 175);
                else if (item.unmounted) tc = fl_rgb_color(140, 150, 165);
                else if (sel)            tc = fl_rgb_color(55, 48, 163);
                else                     tc = fl_rgb_color(55, 65, 81);
                fl_color(tc);
                fl_draw(item.label.c_str(),
                        x() + TEXT_X + 4, iy,
                        content_w - TEXT_X - 12, ENTRY_H,
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                break;
            }
        }
    }

    fl_pop_clip();

    if (scrollbar->active()) draw_child(*scrollbar);

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

void SidePanel::try_mount_drive(int idx) {
    if (idx < 0 || idx >= (int)items.size()) return;
    const Item snapshot = items[idx];  // copy: items get rebuilt below
    if (!snapshot.unmounted || snapshot.device.empty()) return;

    int choice = fl_choice("Mount %s?\nDevice %s is currently unmounted.",
                           "Cancel", "Mount", nullptr,
                           snapshot.label.c_str(), snapshot.device.c_str());
    if (choice != 1) return;

    std::string message;
    bool ok = run_udisksctl_mount(snapshot.device, message);
    if (!ok) {
        fl_alert("Could not mount %s.\n\n%s",
                 snapshot.device.c_str(),
                 message.empty() ? "(no output from udisksctl)" : message.c_str());
        return;
    }

    std::string new_mp = mountpoint_for_device(snapshot.device);
    rebuild_items();
    if (!new_mp.empty() && on_select_cb) on_select_cb(new_mp);
}

int SidePanel::handle(int event) {
    // Forward to the scrollbar when the cursor is over it (or it has been
    // grabbed for a drag).
    if (event == FL_PUSH || event == FL_DRAG || event == FL_RELEASE) {
        if (scrollbar->active() && Fl::event_inside(scrollbar)) {
            return Fl_Group::handle(event);
        }
    }

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
        case FL_MOUSEWHEEL: {
            if (!scrollbar->active()) return 1;
            scroll_y += Fl::event_dy() * ENTRY_H;
            sync_scrollbar();
            redraw();
            return 1;
        }
        case FL_PUSH: {
            int idx = entry_at(Fl::event_y());
            if (idx < 0) break;

            if (Fl::event_button() == FL_RIGHT_MOUSE) {
                if (items[idx].is_pinned) {
                    show_pinned_menu(idx, Fl::event_x(), Fl::event_y());
                }
                return 1;
            }

            if (items[idx].unmounted) {
                try_mount_drive(idx);
                return 1;
            }
            selected = idx;
            redraw();
            if (on_select_cb && !items[idx].path.empty())
                on_select_cb(items[idx].path);
            return 1;
        }
    }
    return Fl_Group::handle(event);
}
