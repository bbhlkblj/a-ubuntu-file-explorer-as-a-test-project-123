#include "FileView.h"
#include "PinStore.h"
#include "X11Clipboard.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_XPM_Image.H>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// App-wide clipboard for file operations. A static instance is shared across
// all FileView instances in the same process; the contents are also pushed
// to the system clipboard as a text/uri-list so other apps can paste.
// ---------------------------------------------------------------------------
struct AppClipboard {
    enum Op { COPY, CUT };
    Op                       op = COPY;
    std::vector<std::string> paths;
};

AppClipboard& app_clipboard() {
    static AppClipboard c;
    return c;
}

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

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char buf[3] = { s[i+1], s[i+2], 0 };
            out += (char)std::strtol(buf, nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string build_uri_list(const std::vector<std::string>& paths) {
    std::string out;
    for (const auto& p : paths) {
        out += url_encode_path(p);
        out += "\r\n";
    }
    return out;
}

std::vector<std::string> parse_uri_list(const char* text, int len) {
    std::vector<std::string> out;
    if (!text || len <= 0) return out;

    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        if (cur[0] == '#') { cur.clear(); return; }  // uri-list comments
        if (cur.rfind("file://", 0) == 0) {
            // Strip optional host portion: file://host/path
            std::string rest = cur.substr(7);
            size_t slash = rest.find('/');
            std::string path = (slash == std::string::npos) ? rest : rest.substr(slash);
            out.push_back(url_decode(path));
        } else if (cur[0] == '/') {
            out.push_back(cur);  // Already an absolute path
        }
        cur.clear();
    };

    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') { flush(); }
        else           { cur += c; }
    }
    flush();
    return out;
}

void push_to_system_clipboard(const std::vector<std::string>& paths, bool cut) {
    X11Clipboard::set(cut ? X11Clipboard::CUT : X11Clipboard::COPY, paths);
}

fs::path unique_destination(fs::path target) {
    std::error_code ec;
    if (!fs::exists(target, ec)) return target;

    fs::path parent = target.parent_path();
    std::string stem = target.stem().string();
    std::string ext  = target.extension().string();

    // Files with no extension keep an empty ext
    for (int i = 2; i < 10000; i++) {
        fs::path cand = parent / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!fs::exists(cand, ec)) return cand;
    }
    return target;  // Give up after 10000 attempts
}

// Returns the destination path actually used (after possible rename), or empty
// on failure.
std::string transfer_one(const fs::path& src, const fs::path& dst_dir, bool move) {
    std::error_code ec;
    fs::path target = dst_dir / src.filename();

    if (move) {
        // Refuse moves onto self / into a subdirectory of self
        std::error_code _ec;
        fs::path canon_src = fs::weakly_canonical(src, _ec);
        fs::path canon_tgt = fs::weakly_canonical(target, _ec);
        if (canon_src == canon_tgt) return "";  // no-op
        if (canon_tgt.string().rfind(canon_src.string() + "/", 0) == 0) {
            fl_alert("Cannot move into a subdirectory of itself:\n%s", src.string().c_str());
            return "";
        }
        if (fs::exists(target, ec)) {
            fl_alert("Move target exists, skipping:\n%s", target.string().c_str());
            return "";
        }
        fs::rename(src, target, ec);
        if (ec) {
            // Cross-device fallback: copy then remove
            ec.clear();
            fs::copy(src, target,
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (!ec) fs::remove_all(src, ec);
        }
    } else {
        target = unique_destination(target);
        fs::copy(src, target,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    }
    if (ec) {
        fl_alert("Error: %s\n%s", src.string().c_str(), ec.message().c_str());
        return "";
    }
    return target.string();
}

void launch_detached(const char* program, const char* arg, const char* workdir) {
    pid_t pid = fork();
    if (pid == 0) {
        // First child: fork again so the grandchild is reparented to init
        if (fork() == 0) {
            if (workdir && *workdir) {
                if (chdir(workdir) != 0) { /* ignore */ }
            }
            if (arg)
                execlp(program, program, arg, (char*)nullptr);
            else
                execlp(program, program, (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

void open_with_default(const std::string& path) {
    launch_detached("xdg-open", path.c_str(), nullptr);
}

struct DesktopApp {
    std::string name;
    std::string exec;
    std::string icon;  // Icon= value: either absolute path or theme name
};

// Locate an installed icon by Icon= value, returning a loadable file path.
// Absolute paths are returned as-is when they exist. Theme names are resolved
// against a one-shot cache built from icon-theme roots and /usr/share/pixmaps.
// We can only load PNG/JPEG/XPM at runtime (FLTK 1.3 has no SVG), so we skip
// SVG matches.
const std::map<std::string, std::string>& icon_path_cache() {
    static std::map<std::string, std::string> cache;
    static bool built = false;
    if (built) return cache;
    built = true;

    auto loadable = [](const fs::path& p) {
        if (!p.has_extension()) return false;
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return ext == ".png" || ext == ".xpm" ||
               ext == ".jpg" || ext == ".jpeg";
    };

    // Preferred sizes — larger sources downscale cleaner than upscaling 16px.
    static const char* size_pref[] = {
        "48x48", "64x64", "32x32", "128x128", "24x24", "256x256", "16x16"
    };

    std::vector<std::string> roots;
    if (const char* home = std::getenv("HOME"))
        roots.push_back(std::string(home) + "/.local/share/icons");
    roots.emplace_back("/usr/share/icons");
    roots.emplace_back("/usr/local/share/icons");

    auto try_record = [&](const std::string& stem, const fs::path& full) {
        if (cache.find(stem) == cache.end()) cache[stem] = full.string();
    };

    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (auto& theme_entry : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!theme_entry.is_directory(ec)) continue;
            for (const char* sz : size_pref) {
                fs::path apps_dir = theme_entry.path() / sz / "apps";
                if (!fs::is_directory(apps_dir, ec)) continue;
                for (auto& f : fs::directory_iterator(apps_dir, ec)) {
                    if (ec) break;
                    if (!f.is_regular_file(ec)) continue;
                    if (!loadable(f.path())) continue;
                    try_record(f.path().stem().string(), f.path());
                }
            }
        }
    }

    // Pixmaps fallback — flat directory, no size buckets.
    for (auto pm : {"/usr/share/pixmaps", "/usr/local/share/pixmaps"}) {
        std::error_code ec;
        if (!fs::is_directory(pm, ec)) continue;
        for (auto& f : fs::directory_iterator(pm, ec)) {
            if (ec) break;
            if (!f.is_regular_file(ec)) continue;
            if (!loadable(f.path())) continue;
            try_record(f.path().stem().string(), f.path());
        }
    }
    return cache;
}

std::string resolve_icon_path(const std::string& icon_spec) {
    if (icon_spec.empty()) return "";
    std::error_code ec;
    if (!icon_spec.empty() && icon_spec.front() == '/') {
        return fs::is_regular_file(icon_spec, ec) ? icon_spec : "";
    }
    const auto& cache = icon_path_cache();
    auto it = cache.find(icon_spec);
    return (it != cache.end()) ? it->second : "";
}

Fl_Image* load_icon_image(const std::string& path, int size) {
    if (path.empty()) return nullptr;
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    Fl_Image* raw = nullptr;
    if      (ext == ".png")                       raw = new Fl_PNG_Image(path.c_str());
    else if (ext == ".jpg" || ext == ".jpeg")     raw = new Fl_JPEG_Image(path.c_str());
    else if (ext == ".xpm")                       raw = new Fl_XPM_Image(path.c_str());
    else                                          return nullptr;

    if (!raw || raw->fail() || raw->w() == 0 || raw->h() == 0) {
        delete raw;
        return nullptr;
    }
    Fl_Image* scaled = raw->copy(size, size);
    delete raw;
    return scaled;
}

// Shell-quote a single argument by wrapping it in single quotes and escaping
// any embedded single quotes the POSIX way: '...'\''...'.
std::string sh_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

// True if the Exec line contains a path-bearing field code (%f %F %u %U).
// Apps without one cannot meaningfully receive a file/folder argument and
// should not be offered in the "Open With" list.
bool exec_accepts_path(const std::string& exec) {
    for (size_t i = 0; i + 1 < exec.size(); i++) {
        if (exec[i] != '%') continue;
        char code = exec[i + 1];
        if (code == 'f' || code == 'F' || code == 'u' || code == 'U') return true;
        if (code == '%') i++;  // escaped percent: skip both chars
    }
    return false;
}

// Substitute FreeDesktop field codes in a desktop file Exec= line with the
// chosen path. Strip the deprecated/no-arg codes (%i %c %k %d %D %n %N %v %m).
std::string apply_field_codes(const std::string& exec, const std::string& path) {
    std::string quoted = sh_quote(path);
    std::string out;
    out.reserve(exec.size() + quoted.size());
    for (size_t i = 0; i < exec.size(); i++) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            char code = exec[i + 1];
            i++;
            switch (code) {
                case 'f': case 'F': case 'u': case 'U':
                    out += quoted;
                    break;
                case '%':
                    out += '%';
                    break;
                default:
                    // %i %c %k %d %D %n %N %v %m and any unknown: drop
                    break;
            }
        } else {
            out += exec[i];
        }
    }
    return out;
}

void launch_desktop_app(const DesktopApp& app, const std::string& path) {
    std::string cmd = apply_field_codes(app.exec, path);
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execlp("sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

std::vector<DesktopApp> scan_desktop_apps() {
    // Build the directory list from XDG_DATA_HOME + XDG_DATA_DIRS per spec,
    // and tack on snap/flatpak roots so apps from those package systems show
    // up even when distros don't include them in XDG_DATA_DIRS.
    std::vector<std::string> data_roots;
    if (const char* xdh = std::getenv("XDG_DATA_HOME"); xdh && *xdh) {
        data_roots.emplace_back(xdh);
    } else if (const char* home = std::getenv("HOME")) {
        data_roots.push_back(std::string(home) + "/.local/share");
    }
    if (const char* xdd = std::getenv("XDG_DATA_DIRS"); xdd && *xdd) {
        std::string s(xdd);
        size_t start = 0;
        while (start <= s.size()) {
            size_t end = s.find(':', start);
            if (end == std::string::npos) end = s.size();
            if (end > start) data_roots.emplace_back(s.substr(start, end - start));
            start = end + 1;
        }
    } else {
        data_roots.emplace_back("/usr/local/share");
        data_roots.emplace_back("/usr/share");
    }
    data_roots.emplace_back("/var/lib/snapd/desktop");
    data_roots.emplace_back("/var/lib/flatpak/exports/share");
    if (const char* home = std::getenv("HOME"))
        data_roots.emplace_back(std::string(home) + "/.local/share/flatpak/exports/share");

    std::vector<std::string> dirs;
    std::set<std::string> dirs_seen;
    for (const auto& root : data_roots) {
        std::string d = root + "/applications";
        if (dirs_seen.insert(d).second) dirs.push_back(d);
    }

    std::vector<DesktopApp> result;
    std::set<std::string> seen;  // dedupe earlier directories shadow later ones

    for (const auto& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (auto& entry : fs::directory_iterator(d, ec)) {
            if (ec) break;
            if (entry.path().extension() != ".desktop") continue;

            std::string basename = entry.path().filename().string();
            if (!seen.insert(basename).second) continue;

            std::ifstream f(entry.path());
            if (!f) continue;

            std::string line, name, exec, type, icon;
            bool in_entry = false, no_display = false, hidden = false;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                if (line[0] == '[') {
                    if (in_entry) break;
                    in_entry = (line == "[Desktop Entry]");
                    continue;
                }
                if (!in_entry) continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                if      (key == "Name")      name = val;
                else if (key == "Exec")      exec = val;
                else if (key == "Type")      type = val;
                else if (key == "Icon")      icon = val;
                else if (key == "NoDisplay") no_display = (val == "true");
                else if (key == "Hidden")    hidden     = (val == "true");
            }

            if (type != "Application")        continue;
            if (no_display || hidden)         continue;
            if (name.empty() || exec.empty()) continue;
            if (!exec_accepts_path(exec))     continue;  // e.g. Steam game launchers
            result.push_back({name, exec, icon});
        }
    }

    std::sort(result.begin(), result.end(),
              [](const DesktopApp& a, const DesktopApp& b) {
                  return std::lexicographical_compare(
                      a.name.begin(), a.name.end(),
                      b.name.begin(), b.name.end(),
                      [](char x, char y) {
                          return std::tolower((unsigned char)x) <
                                 std::tolower((unsigned char)y);
                      });
              });
    return result;
}

// A scrollable single-select list of (icon, label) rows. Used by the "Open
// With" dialog so we can show each application's icon next to its name —
// Fl_Hold_Browser doesn't natively render per-row images. The widget does
// NOT own the icons; the caller manages their lifetime.
class IconListView : public Fl_Group {
public:
    struct Row {
        std::string label;
        Fl_Image*   icon = nullptr;  // borrowed
    };

    IconListView(int x, int y, int w, int h)
        : Fl_Group(x, y, w, h)
    {
        box(FL_DOWN_BOX);
        color(FL_BACKGROUND2_COLOR);
        sb_ = new Fl_Scrollbar(x + w - SB_W, y, SB_W, h);
        sb_->callback(scrollbar_cb, this);
        end();
    }

    void clear() {
        rows_.clear();
        selected_ = -1;
        scroll_ = 0;
        sync_scrollbar();
        redraw();
    }

    void add_row(const std::string& label, Fl_Image* icon) {
        rows_.push_back({label, icon});
        sync_scrollbar();
        redraw();
    }

    int value() const { return selected_; }
    void set_value(int idx) {
        if (idx < -1 || idx >= (int)rows_.size()) idx = -1;
        if (selected_ == idx) return;
        selected_ = idx;
        if (idx >= 0) ensure_visible(idx);
        redraw();
    }

    void set_double_click_cb(std::function<void()> cb) { dbl_cb_ = std::move(cb); }
    void set_select_cb      (std::function<void()> cb) { sel_cb_ = std::move(cb); }

    void resize(int X, int Y, int W, int H) override {
        Fl_Group::resize(X, Y, W, H);
        sb_->resize(X + W - SB_W, Y, SB_W, H);
        sync_scrollbar();
    }

    void draw() override {
        draw_box();
        int inner_x = x() + Fl::box_dx(box());
        int inner_y = y() + Fl::box_dy(box());
        int inner_w = w() - Fl::box_dw(box()) - SB_W;
        int inner_h = h() - Fl::box_dh(box());

        fl_push_clip(inner_x, inner_y, inner_w, inner_h);

        int first = scroll_ / ROW_H;
        int last  = std::min((int)rows_.size(),
                             (scroll_ + inner_h) / ROW_H + 1);

        for (int i = first; i < last; i++) {
            int ry = inner_y + i * ROW_H - scroll_;
            bool sel = (i == selected_);
            Fl_Color bg = sel ? FL_SELECTION_COLOR : color();
            Fl_Color fg = sel ? fl_contrast(FL_FOREGROUND_COLOR, bg)
                              : FL_FOREGROUND_COLOR;
            fl_color(bg);
            fl_rectf(inner_x, ry, inner_w, ROW_H);

            const Row& r = rows_[i];
            int icon_x = inner_x + PAD;
            int icon_y = ry + (ROW_H - ICON_SZ) / 2;
            if (r.icon) {
                r.icon->draw(icon_x, icon_y);
            } else {
                // Placeholder: simple grey square so layout stays aligned
                fl_color(fl_rgb_color(210, 210, 210));
                fl_rectf(icon_x, icon_y, ICON_SZ, ICON_SZ);
                fl_color(fl_rgb_color(170, 170, 170));
                fl_rect(icon_x, icon_y, ICON_SZ, ICON_SZ);
            }

            int text_x = icon_x + ICON_SZ + PAD;
            int text_w = inner_x + inner_w - text_x - PAD;
            fl_color(fg);
            fl_font(FL_HELVETICA, 13);
            fl_push_clip(text_x, ry, text_w, ROW_H);
            fl_draw(r.label.c_str(),
                    text_x, ry, text_w, ROW_H,
                    (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_INSIDE));
            fl_pop_clip();
        }

        fl_pop_clip();
        draw_child(*sb_);
    }

    int handle(int event) override {
        switch (event) {
            case FL_PUSH: {
                if (Fl::event_inside(sb_)) return Fl_Group::handle(event);
                take_focus();
                int idx = row_at(Fl::event_y());
                if (idx >= 0) {
                    set_value(idx);
                    if (sel_cb_) sel_cb_();
                    if (Fl::event_clicks() > 0 && dbl_cb_) {
                        Fl::event_clicks(0);
                        dbl_cb_();
                    }
                }
                return 1;
            }
            case FL_MOUSEWHEEL: {
                if (Fl::event_inside(sb_)) return Fl_Group::handle(event);
                scroll_ += Fl::event_dy() * ROW_H;
                clamp_scroll();
                sb_->value(scroll_);
                redraw();
                return 1;
            }
            case FL_FOCUS:
            case FL_UNFOCUS:
                return 1;
            case FL_KEYBOARD: {
                int k = Fl::event_key();
                if (k == FL_Down)  { move_selection(+1);                 return 1; }
                if (k == FL_Up)    { move_selection(-1);                 return 1; }
                if (k == FL_Page_Down) {
                    move_selection(std::max(1, (h() / ROW_H) - 1));     return 1;
                }
                if (k == FL_Page_Up) {
                    move_selection(-std::max(1, (h() / ROW_H) - 1));    return 1;
                }
                if (k == FL_Home)  { if (!rows_.empty()) set_value(0);   return 1; }
                if (k == FL_End)   { if (!rows_.empty())
                                         set_value((int)rows_.size()-1); return 1; }
                if (k == FL_Enter || k == FL_KP_Enter) {
                    if (selected_ >= 0 && dbl_cb_) dbl_cb_();           return 1;
                }
                break;
            }
        }
        return Fl_Group::handle(event);
    }

private:
    static constexpr int ROW_H   = 32;
    static constexpr int ICON_SZ = 24;
    static constexpr int PAD     = 6;
    static constexpr int SB_W    = 14;

    std::vector<Row>      rows_;
    int                   selected_ = -1;
    int                   scroll_   = 0;
    Fl_Scrollbar*         sb_;
    std::function<void()> dbl_cb_;
    std::function<void()> sel_cb_;

    int content_h() const { return (int)rows_.size() * ROW_H; }
    int viewport_h() const { return h() - Fl::box_dh(box()); }

    void clamp_scroll() {
        int max_s = std::max(0, content_h() - viewport_h());
        if (scroll_ < 0)     scroll_ = 0;
        if (scroll_ > max_s) scroll_ = max_s;
    }

    void sync_scrollbar() {
        int total = content_h();
        int vis   = viewport_h();
        clamp_scroll();
        sb_->bounds(0, std::max(0, total - vis));
        sb_->slider_size(total > 0 ? std::min(1.0, (double)vis / total) : 1.0);
        sb_->linesize(ROW_H);
        sb_->value(scroll_);
    }

    void ensure_visible(int idx) {
        int top    = idx * ROW_H;
        int bottom = top + ROW_H;
        if (top < scroll_)                       scroll_ = top;
        else if (bottom > scroll_ + viewport_h()) scroll_ = bottom - viewport_h();
        clamp_scroll();
        sb_->value(scroll_);
    }

    int row_at(int my) const {
        int inner_y = y() + Fl::box_dy(box());
        int local   = my - inner_y + scroll_;
        if (local < 0) return -1;
        int idx = local / ROW_H;
        return (idx < 0 || idx >= (int)rows_.size()) ? -1 : idx;
    }

    void move_selection(int delta) {
        if (rows_.empty()) return;
        int n = (int)rows_.size();
        int cur = (selected_ < 0) ? 0 : selected_ + delta;
        if (cur < 0)  cur = 0;
        if (cur >= n) cur = n - 1;
        set_value(cur);
        if (sel_cb_) sel_cb_();
    }

    static void scrollbar_cb(Fl_Widget* w, void* ud) {
        auto* self = (IconListView*)ud;
        self->scroll_ = ((Fl_Scrollbar*)w)->value();
        self->redraw();
    }
};

// Runs a modal "Open With" chooser. The cached icons are loaded once and
// freed when the dialog closes.
struct OpenWithDialog {
    Fl_Double_Window*        win    = nullptr;
    Fl_Input*                filter = nullptr;
    IconListView*            list   = nullptr;
    std::vector<DesktopApp>  all;
    std::vector<Fl_Image*>   icons;     // index-aligned with `all`, owned here
    std::vector<int>         visible;
    int                      chosen = -1;

    ~OpenWithDialog() {
        for (auto* img : icons) delete img;
    }

    void refresh() {
        std::string q = filter->value() ? filter->value() : "";
        std::transform(q.begin(), q.end(), q.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        list->clear();
        visible.clear();
        for (size_t i = 0; i < all.size(); i++) {
            std::string n = all[i].name;
            std::transform(n.begin(), n.end(), n.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (q.empty() || n.find(q) != std::string::npos) {
                visible.push_back((int)i);
                list->add_row(all[i].name, icons[i]);
            }
        }
        if (!visible.empty()) list->set_value(0);
    }

    void accept() {
        int sel = list->value();
        if (sel < 0 || sel >= (int)visible.size()) { cancel(); return; }
        chosen = visible[sel];
        win->hide();
    }
    void cancel() { chosen = -1; win->hide(); }

    static void filter_cb(Fl_Widget*, void* ud) {
        ((OpenWithDialog*)ud)->refresh();
    }
    static void ok_cb    (Fl_Widget*, void* ud) { ((OpenWithDialog*)ud)->accept(); }
    static void cancel_cb(Fl_Widget*, void* ud) { ((OpenWithDialog*)ud)->cancel(); }
};

void show_open_with(const std::string& path) {
    auto apps = scan_desktop_apps();
    if (apps.empty()) {
        fl_alert("No applications found in the desktop entry directories.");
        return;
    }

    OpenWithDialog d;
    d.all = std::move(apps);

    // Load icons up front. Names referenced by multiple apps share a loader
    // result so we don't decode the same PNG twice.
    std::map<std::string, Fl_Image*> shared;
    d.icons.reserve(d.all.size());
    for (const auto& app : d.all) {
        std::string p = resolve_icon_path(app.icon);
        if (p.empty()) { d.icons.push_back(nullptr); continue; }
        auto it = shared.find(p);
        if (it != shared.end()) {
            d.icons.push_back(it->second ? it->second->copy() : nullptr);
        } else {
            Fl_Image* img = load_icon_image(p, 24);
            shared[p] = img;
            d.icons.push_back(img ? img->copy() : nullptr);
        }
    }
    for (auto& kv : shared) delete kv.second;  // originals freed; copies in d.icons

    const int W = 460, H = 520;
    d.win = new Fl_Double_Window(W, H, "Open With");
    d.win->set_modal();

    d.filter = new Fl_Input(12, 14, W - 24, 26);
    d.filter->textsize(13);
    d.filter->when(FL_WHEN_CHANGED);
    d.filter->callback(OpenWithDialog::filter_cb, &d);

    d.list = new IconListView(12, 50, W - 24, H - 100);
    d.list->set_double_click_cb([&d]() { d.accept(); });

    auto* ok = new Fl_Return_Button(W - 184, H - 40, 80, 28, "Open");
    ok->callback(OpenWithDialog::ok_cb, &d);
    auto* cancel = new Fl_Button(W - 94, H - 40, 80, 28, "Cancel");
    cancel->callback(OpenWithDialog::cancel_cb, &d);

    d.win->end();
    d.refresh();
    d.filter->take_focus();
    d.win->show();
    while (d.win->shown()) Fl::wait();

    int chosen = d.chosen;
    DesktopApp app;
    if (chosen >= 0 && chosen < (int)d.all.size()) app = d.all[chosen];
    delete d.win;  // also deletes children

    if (chosen >= 0) launch_desktop_app(app, path);
}

void open_terminal_at(const std::string& dir) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            if (chdir(dir.c_str()) != 0) { /* ignore */ }
            execlp("x-terminal-emulator", "x-terminal-emulator", (char*)nullptr);
            execlp("gnome-terminal",     "gnome-terminal",     (char*)nullptr);
            execlp("konsole",            "konsole",            (char*)nullptr);
            execlp("xfce4-terminal",     "xfce4-terminal",     (char*)nullptr);
            execlp("kitty",              "kitty",              (char*)nullptr);
            execlp("alacritty",          "alacritty",          (char*)nullptr);
            execlp("xterm",              "xterm",              (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

}  // namespace

// ---------- View-mode geometry ---------------------------------------------

int FileView::row_h() const { return 28; }

int FileView::icon_sz() const {
    switch (mode) {
        case SMALL_ICONS:       return 20;
        case MEDIUM_ICONS:      return 48;
        case LARGE_ICONS:       return 80;
        case EXTRA_LARGE_ICONS: return 120;
        default:                return 16;
    }
}

int FileView::cell_w() const {
    switch (mode) {
        case SMALL_ICONS:       return 180;
        case MEDIUM_ICONS:      return 110;
        case LARGE_ICONS:       return 150;
        case EXTRA_LARGE_ICONS: return 200;
        default:                return 0;
    }
}

int FileView::cell_h() const {
    switch (mode) {
        case SMALL_ICONS:       return 32;
        case MEDIUM_ICONS:      return 90;
        case LARGE_ICONS:       return 130;
        case EXTRA_LARGE_ICONS: return 180;
        default:                return 0;
    }
}

int FileView::columns() const {
    if (mode == DETAILS) return 1;
    int avail = content_w() - 16;
    int cw    = cell_w();
    return std::max(1, avail / cw);
}

int FileView::content_h() const {
    if (mode == DETAILS) {
        return (int)entries.size() * row_h() + header_h() + 8;
    }
    int cols = columns();
    int rows = ((int)entries.size() + cols - 1) / cols;
    return rows * cell_h() + 16;
}

int FileView::name_col_x() const {
    return x() + 16 + 18 + 10;  // icon_col + icon_w + pad
}

int FileView::boundary_x(int b) const {
    int bx = name_col_x() + col_w_name;
    if (b == 0) return bx;
    bx += col_w_type;
    if (b == 1) return bx;
    bx += col_w_date;
    return bx;  // b == 2
}

int FileView::boundary_at(int mx, int my) const {
    if (mode != DETAILS) return -1;
    if (my < y() || my >= y() + h()) return -1;
    for (int b = 0; b < 3; b++) {
        int bx = boundary_x(b);
        if (mx >= bx - 3 && mx <= bx + 3) return b;
    }
    return -1;
}

int FileView::header_column_at(int mx, int my) const {
    if (mode != DETAILS) return -1;
    if (my < y() || my >= y() + header_h()) return -1;

    int nx = name_col_x();
    if (mx >= nx               && mx < boundary_x(0)) return SORT_NAME;
    if (mx >= boundary_x(0)    && mx < boundary_x(1)) return SORT_TYPE;
    if (mx >= boundary_x(1)    && mx < boundary_x(2)) return SORT_DATE;
    if (mx >= boundary_x(2))                           return SORT_SIZE;
    return -1;
}

// ---------- Item hit-testing -----------------------------------------------

void FileView::item_rect(int idx, int& ix, int& iy, int& iw, int& ih) const {
    if (mode == DETAILS) {
        ix = x();
        iy = y() + header_h() + idx * row_h() - scroll_y;
        iw = content_w();
        ih = row_h();
    } else {
        int cols = columns();
        int col  = idx % cols;
        int row  = idx / cols;
        iw = cell_w();
        ih = cell_h();
        ix = x() + 8 + col * iw;
        iy = y() + 8 + row * ih - scroll_y;
    }
}

int FileView::item_index_at(int mx, int my) const {
    if (mode == DETAILS) {
        int hy = y() + header_h();
        if (my < hy) return -1;
        int idx = (my - hy + scroll_y) / row_h();
        if (idx < 0 || idx >= (int)entries.size()) return -1;
        return idx;
    } else {
        int cols = columns();
        int rel_x = mx - x() - 8;
        int rel_y = my - y() - 8 + scroll_y;
        if (rel_x < 0 || rel_y < 0) return -1;
        int col = rel_x / cell_w();
        int row = rel_y / cell_h();
        if (col >= cols) return -1;
        int idx = row * cols + col;
        if (idx < 0 || idx >= (int)entries.size()) return -1;
        return idx;
    }
}

void FileView::clamp_scroll() {
    int max_scroll = std::max(0, content_h() - h());
    if (scroll_y < 0)          scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;
    if (scrollbar) sync_scrollbar();
}

// ---------- Construction ---------------------------------------------------

FileView::FileView(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    box(FL_FLAT_BOX);
    scrollbar = new Fl_Scrollbar(x + w - SB_W, y, SB_W, h);
    scrollbar->callback(scrollbar_cb, this);
    end();
}

void FileView::scrollbar_cb(Fl_Widget* w, void* ud) {
    auto* self = static_cast<FileView*>(ud);
    self->scroll_y = static_cast<Fl_Scrollbar*>(w)->value();
    self->redraw();
}

int FileView::content_w() const {
    return w() - SB_W;
}

void FileView::sync_scrollbar() {
    int total = content_h();
    int vis   = h();
    int max_s = std::max(0, total - vis);
    if (scroll_y > max_s) scroll_y = max_s;
    if (scroll_y < 0)     scroll_y = 0;
    scrollbar->bounds(0, max_s);
    scrollbar->slider_size(total > 0 ? std::min(1.0, (double)vis / total) : 1.0);
    scrollbar->linesize(row_h() ? row_h() : 40);
    scrollbar->value(scroll_y);
    if (max_s == 0) scrollbar->deactivate();
    else            scrollbar->activate();
}

void FileView::resize(int nx, int ny, int nw, int nh) {
    Fl_Group::resize(nx, ny, nw, nh);
    scrollbar->resize(nx + nw - SB_W, ny, SB_W, nh);
    sync_scrollbar();
}

// ---------- Directory loading ----------------------------------------------

static int icmp_compare(const std::string& a, const std::string& b) {
    auto la = a.size(), lb = b.size();
    auto n  = std::min(la, lb);
    for (size_t i = 0; i < n; i++) {
        int ca = std::tolower((unsigned char)a[i]);
        int cb = std::tolower((unsigned char)b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
}

void FileView::set_location(const std::string& path) {
    current_path = path;
    entries.clear();
    scroll_y = 0;
    selection.clear();
    anchor   = -1;
    hovered  = -1;

    fs::path p(path);
    std::error_code ec;

    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        redraw();
        notify_counts();
        return;
    }

    // ".." entry if not at root
    if (p.has_relative_path() || p != p.root_path()) {
        fs::path parent = p.parent_path();
        if (!parent.empty() && parent != p) {
            Entry up;
            up.name  = "..";
            up.path  = parent.string();
            up.kind  = Entry::PARENT_K;
            up.size  = 0;
            up.mtime = 0;
            entries.push_back(up);
        }
    }

    for (auto it = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator();
         it.increment(ec))
    {
        if (ec) break;

        Entry e;
        e.name = it->path().filename().string();
        e.path = it->path().string();

        std::error_code ec2;
        if (it->is_symlink(ec2))       e.kind = Entry::LINK_K;
        else if (it->is_directory(ec2)) e.kind = Entry::FOLDER_K;
        else                            e.kind = Entry::FILE_K;

        if (e.kind == Entry::FILE_K) {
            auto sz = it->file_size(ec2);
            e.size = ec2 ? 0 : sz;
        }

        auto ft = it->last_write_time(ec2);
        if (!ec2) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            e.mtime = std::chrono::system_clock::to_time_t(sctp);
        }

        entries.push_back(e);
    }

    sort_entries();
    sync_scrollbar();
    redraw();
    notify_counts();
}

void FileView::sort_entries() {
    auto sc   = sort_col;
    auto desc = sort_desc;

    std::sort(entries.begin(), entries.end(),
        [sc, desc](const Entry& a, const Entry& b) -> bool {
            if (a.kind == Entry::PARENT_K) return true;
            if (b.kind == Entry::PARENT_K) return false;
            bool ad = (a.kind == Entry::FOLDER_K);
            bool bd = (b.kind == Entry::FOLDER_K);
            if (ad != bd) return ad;

            int r = 0;
            switch (sc) {
                case SORT_NAME:
                    r = icmp_compare(a.name, b.name);
                    break;
                case SORT_TYPE: {
                    std::string ka = kind_string(a.kind);
                    std::string kb = kind_string(b.kind);
                    r = icmp_compare(ka, kb);
                    if (r == 0) r = icmp_compare(a.name, b.name);
                    break;
                }
                case SORT_DATE:
                    if      (a.mtime < b.mtime) r = -1;
                    else if (a.mtime > b.mtime) r =  1;
                    if (r == 0) r = icmp_compare(a.name, b.name);
                    break;
                case SORT_SIZE:
                    if      (a.size < b.size) r = -1;
                    else if (a.size > b.size) r =  1;
                    if (r == 0) r = icmp_compare(a.name, b.name);
                    break;
            }
            return desc ? r > 0 : r < 0;
        });
}

// ---------- Mode --------------------------------------------------------------

void FileView::set_view_mode(ViewMode m) {
    if (m == mode) return;
    mode     = m;
    scroll_y = 0;
    clamp_scroll();
    redraw();
}

void FileView::cycle_view_mode(int direction) {
    int next = (int)mode + direction;
    if (next < 0)                 next = 0;
    if (next >= (int)MODE_COUNT)  next = MODE_COUNT - 1;
    set_view_mode((ViewMode)next);
}

// ---------- Formatting helpers ---------------------------------------------

std::string FileView::format_size(std::uintmax_t bytes) {
    constexpr std::array<const char*, 5> units = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < (int)units.size() - 1) { v /= 1024.0; u++; }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}

std::string FileView::format_time(std::time_t t) {
    if (t == 0) return "";
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string FileView::kind_string(Entry::Kind k) {
    switch (k) {
        case Entry::FOLDER_K: return "Folder";
        case Entry::LINK_K:   return "Link";
        case Entry::PARENT_K: return "Folder";
        default:              return "File";
    }
}

// ---------- Icons -----------------------------------------------------------

void FileView::draw_folder_icon(int ix, int iy, int sz) {
    int tab_x = ix + (int)(sz * 0.06);
    int tab_y = iy + (int)(sz * 0.20);
    int tab_w = (int)(sz * 0.42);
    int tab_h = std::max(3, (int)(sz * 0.16));

    int body_x = ix + (int)(sz * 0.06);
    int body_y = iy + (int)(sz * 0.30);
    int body_w = (int)(sz * 0.88);
    int body_h = (int)(sz * 0.56);

    fl_color(fl_rgb_color(245, 158, 11));
    fl_rectf(tab_x, tab_y, tab_w, tab_h);

    fl_color(fl_rgb_color(251, 191, 36));
    fl_rectf(body_x, body_y, body_w, body_h);

    fl_color(fl_rgb_color(252, 211, 77));
    fl_rectf(body_x, body_y, body_w, std::max(2, sz / 18));

    fl_color(fl_rgb_color(180, 110, 8));
    fl_rect(body_x, body_y, body_w, body_h);
}

void FileView::draw_file_icon(int ix, int iy, int sz) {
    int bx = ix + (int)(sz * 0.18);
    int by = iy + (int)(sz * 0.08);
    int bw = (int)(sz * 0.64);
    int bh = (int)(sz * 0.84);

    fl_color(fl_rgb_color(255, 255, 255));
    fl_rectf(bx, by, bw, bh);

    fl_color(fl_rgb_color(99, 102, 241));
    fl_rectf(bx, by, bw, std::max(2, sz / 10));

    fl_color(fl_rgb_color(209, 213, 219));
    fl_rect(bx, by, bw, bh);

    // Folded corner
    int fold = std::max(3, sz / 8);
    fl_color(fl_rgb_color(229, 231, 235));
    fl_polygon(bx + bw - fold, by, bx + bw, by + fold, bx + bw, by);
    fl_color(fl_rgb_color(156, 163, 175));
    fl_line(bx + bw - fold, by, bx + bw, by + fold);
}

void FileView::draw_link_icon(int ix, int iy, int sz) {
    draw_file_icon(ix, iy, sz);
    // small arrow badge bottom-left
    int badge = std::max(6, sz / 3);
    int bx = ix + (int)(sz * 0.18);
    int by = iy + (int)(sz * 0.08) + (int)(sz * 0.84) - badge;
    fl_color(fl_rgb_color(59, 130, 246));
    fl_rectf(bx, by, badge, badge);
    fl_color(fl_rgb_color(255, 255, 255));
    fl_font(FL_HELVETICA_BOLD, std::max(8, badge - 2));
    fl_draw("\xe2\x86\x97", bx, by, badge, badge, FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
}

void FileView::draw_parent_icon(int ix, int iy, int sz) {
    draw_folder_icon(ix, iy, sz);
    int badge = std::max(8, sz / 3);
    int bx = ix + (int)(sz * 0.5) - badge / 2;
    int by = iy + (int)(sz * 0.45) - badge / 2;
    fl_color(fl_rgb_color(99, 102, 241));
    fl_rectf(bx, by, badge, badge);
    fl_color(fl_rgb_color(255, 255, 255));
    fl_font(FL_HELVETICA_BOLD, std::max(8, badge - 2));
    fl_draw("\xe2\x86\x91", bx, by, badge, badge, FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
}

void FileView::draw_icon(const Entry& e, int ix, int iy, int sz) {
    switch (e.kind) {
        case Entry::FOLDER_K: draw_folder_icon(ix, iy, sz); break;
        case Entry::FILE_K:   draw_file_icon  (ix, iy, sz); break;
        case Entry::LINK_K:   draw_link_icon  (ix, iy, sz); break;
        case Entry::PARENT_K: draw_parent_icon(ix, iy, sz); break;
    }
}

// ---------- Drawing ---------------------------------------------------------

void FileView::draw_header_label(const char* text, int hx, int hw,
                                 bool is_sort_col, int align) {
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(is_sort_col ? fl_rgb_color(55, 48, 163) : fl_rgb_color(107, 114, 128));

    fl_push_clip(hx, y(), hw, header_h());
    fl_draw(text, hx + 4, y(), hw - 22, header_h(), align | FL_ALIGN_INSIDE);

    if (is_sort_col) {
        const char* arrow = sort_desc ? "\xe2\x96\xbc" : "\xe2\x96\xb2";
        fl_font(FL_HELVETICA, 10);
        fl_draw(arrow, hx + hw - 18, y(), 14, header_h(),
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
    fl_pop_clip();
}

void FileView::draw_details() {
    const int icon_col = 16;
    const int icon_w   = 18;
    const int hh       = header_h();

    int name_x = name_col_x();
    int type_x = boundary_x(0);
    int date_x = boundary_x(1);
    int size_x = boundary_x(2);
    int total_right = x() + content_w() - 6;
    int size_col_visible = std::max(0, total_right - size_x);

    // Header background + bottom border
    fl_color(fl_rgb_color(249, 250, 251));
    fl_rectf(x(), y(), content_w(), hh);
    fl_color(fl_rgb_color(229, 231, 235));
    fl_line(x(), y() + hh - 1, x() + content_w() - 1, y() + hh - 1);

    draw_header_label("Name",          name_x, col_w_name, sort_col == SORT_NAME, FL_ALIGN_LEFT);
    draw_header_label("Type",          type_x, col_w_type, sort_col == SORT_TYPE, FL_ALIGN_LEFT);
    draw_header_label("Date modified", date_x, col_w_date, sort_col == SORT_DATE, FL_ALIGN_LEFT);
    draw_header_label("Size",          size_x, size_col_visible,
                                       sort_col == SORT_SIZE, FL_ALIGN_RIGHT);

    // Column separators in header
    fl_color(fl_rgb_color(229, 231, 235));
    for (int b = 0; b < 3; b++) {
        int bx = boundary_x(b);
        fl_line(bx, y() + 6, bx, y() + hh - 6);
    }

    // Rows
    fl_push_clip(x(), y() + hh, content_w(), h() - hh);

    for (int i = 0; i < (int)entries.size(); i++) {
        int rx, ry, rw, rh;
        item_rect(i, rx, ry, rw, rh);

        if (ry + rh < y() + hh) continue;
        if (ry > y() + h())     break;

        bool sel = selection.count(i) > 0;
        bool hov = (i == hovered && !sel);

        if (sel) {
            fl_color(fl_rgb_color(224, 231, 255));
            fl_rectf(rx, ry, rw, rh);
        } else if (hov) {
            fl_color(fl_rgb_color(243, 244, 246));
            fl_rectf(rx, ry, rw, rh);
        }

        draw_icon(entries[i], rx + icon_col, ry + (rh - icon_w) / 2, icon_w);

        bool cut = is_cut(entries[i].path);
        Fl_Color primary   = sel ? fl_rgb_color(55, 48, 163)
                                 : (cut ? fl_rgb_color(156, 163, 175) : fl_rgb_color(31, 41, 55));
        Fl_Color secondary = sel ? fl_rgb_color(67, 56, 202)
                                 : (cut ? fl_rgb_color(196, 200, 210) : fl_rgb_color(107, 114, 128));

        fl_font(FL_HELVETICA, 13);
        fl_color(primary);
        fl_push_clip(name_x, ry, col_w_name - 6, rh);
        fl_draw(entries[i].name.c_str(),
                name_x, ry, col_w_name - 6, rh, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_pop_clip();

        fl_color(secondary);
        fl_font(FL_HELVETICA, 12);

        fl_push_clip(type_x, ry, col_w_type - 6, rh);
        fl_draw(kind_string(entries[i].kind).c_str(),
                type_x + 4, ry, col_w_type - 10, rh, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_pop_clip();

        fl_push_clip(date_x, ry, col_w_date - 6, rh);
        fl_draw(format_time(entries[i].mtime).c_str(),
                date_x + 4, ry, col_w_date - 10, rh, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_pop_clip();

        if (entries[i].kind == Entry::FILE_K || entries[i].kind == Entry::LINK_K) {
            fl_push_clip(size_x, ry, size_col_visible, rh);
            fl_draw(format_size(entries[i].size).c_str(),
                    size_x, ry, size_col_visible - 6, rh, FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
            fl_pop_clip();
        }
    }

    fl_pop_clip();
}

void FileView::draw_grid() {
    fl_push_clip(x(), y(), content_w(), h());

    const int sz = icon_sz();
    const bool inline_name = (mode == SMALL_ICONS);

    for (int i = 0; i < (int)entries.size(); i++) {
        int cx, cy, cw, ch;
        item_rect(i, cx, cy, cw, ch);

        if (cy + ch < y()) continue;
        if (cy > y() + h()) break;

        bool sel = selection.count(i) > 0;
        bool hov = (i == hovered && !sel);

        int pad = 4;
        if (sel) {
            fl_color(fl_rgb_color(224, 231, 255));
            fl_rectf(cx + pad, cy + pad, cw - 2 * pad, ch - 2 * pad);
        } else if (hov) {
            fl_color(fl_rgb_color(243, 244, 246));
            fl_rectf(cx + pad, cy + pad, cw - 2 * pad, ch - 2 * pad);
        }

        bool cut = is_cut(entries[i].path);
        Fl_Color text_col = sel ? fl_rgb_color(55, 48, 163)
                                : (cut ? fl_rgb_color(156, 163, 175) : fl_rgb_color(31, 41, 55));

        if (inline_name) {
            int icon_y = cy + (ch - sz) / 2;
            int icon_x = cx + 10;
            draw_icon(entries[i], icon_x, icon_y, sz);

            fl_font(FL_HELVETICA, 13);
            fl_color(text_col);
            int tx = icon_x + sz + 8;
            int tw = cw - (sz + 22);
            fl_push_clip(tx, cy, tw, ch);
            fl_draw(entries[i].name.c_str(), tx, cy, tw, ch, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            fl_pop_clip();
        } else {
            int icon_x = cx + (cw - sz) / 2;
            int icon_y = cy + 10;
            draw_icon(entries[i], icon_x, icon_y, sz);

            int label_y = icon_y + sz + 6;
            int label_h = ch - (label_y - cy) - 6;
            int font_sz = (mode == EXTRA_LARGE_ICONS) ? 13 : 12;

            fl_font(FL_HELVETICA, font_sz);
            fl_color(text_col);
            fl_push_clip(cx + 4, label_y, cw - 8, label_h);
            fl_draw(entries[i].name.c_str(),
                    cx + 4, label_y, cw - 8, label_h,
                    (Fl_Align)(FL_ALIGN_TOP | FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP));
            fl_pop_clip();
        }
    }

    fl_pop_clip();
}

void FileView::draw() {
    fl_color(fl_rgb_color(255, 255, 255));
    fl_rectf(x(), y(), w(), h());

    if (entries.empty()) {
        fl_font(FL_HELVETICA, 13);
        fl_color(fl_rgb_color(156, 163, 175));
        const char* msg = current_path.empty()
            ? "No location"
            : "This folder is empty or cannot be read";
        fl_draw(msg, x(), y(), content_w(), h(),
                FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    } else if (mode == DETAILS) {
        draw_details();
    } else {
        draw_grid();
    }

    draw_child(*scrollbar);
}

// ---------- Events ----------------------------------------------------------

bool FileView::is_cut(const std::string& path) const {
    const auto& c = app_clipboard();
    if (c.op != AppClipboard::CUT) return false;
    return std::find(c.paths.begin(), c.paths.end(), path) != c.paths.end();
}

std::vector<std::string> FileView::selected_paths() const {
    std::vector<std::string> out;
    for (int i : selection) {
        if (i < 0 || i >= (int)entries.size()) continue;
        if (entries[i].kind == Entry::PARENT_K) continue;
        out.push_back(entries[i].path);
    }
    return out;
}

void FileView::clear_selection() {
    if (selection.empty()) return;
    selection.clear();
    redraw();
    notify_counts();
}

void FileView::select_single(int idx) {
    selection.clear();
    if (idx >= 0 && idx < (int)entries.size()) {
        selection.insert(idx);
        anchor = idx;
    }
    notify_counts();
}

void FileView::notify_counts() const {
    if (!on_counts_cb) return;
    int total = 0, sel = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].kind == Entry::PARENT_K) continue;
        total++;
        if (selection.count((int)i)) sel++;
    }
    on_counts_cb(sel, total);
}

void FileView::apply_click_selection(int idx, int state) {
    if (idx < 0 || idx >= (int)entries.size()) return;
    bool ctrl  = (state & FL_CTRL)  != 0;
    bool shift = (state & FL_SHIFT) != 0;

    if (ctrl) {
        if (selection.count(idx)) selection.erase(idx);
        else                      { selection.insert(idx); anchor = idx; }
    } else if (shift && anchor >= 0) {
        selection.clear();
        int lo = std::min(anchor, idx), hi = std::max(anchor, idx);
        for (int i = lo; i <= hi; i++) selection.insert(i);
    } else {
        // Plain click: if not in selection, replace; otherwise keep (for drag).
        if (!selection.count(idx)) {
            selection.clear();
            selection.insert(idx);
            anchor = idx;
        }
    }
    notify_counts();
}

void FileView::clipboard_copy_selected() {
    auto paths = selected_paths();
    if (paths.empty()) return;

    auto& c = app_clipboard();
    c.op    = AppClipboard::COPY;
    c.paths = paths;
    push_to_system_clipboard(paths, /*cut=*/false);
    redraw();
}

void FileView::clipboard_cut_selected() {
    auto paths = selected_paths();
    if (paths.empty()) return;

    auto& c = app_clipboard();
    c.op    = AppClipboard::CUT;
    c.paths = paths;
    push_to_system_clipboard(paths, /*cut=*/true);
    redraw();
}

void FileView::clipboard_request_paste(const std::string& dest_dir) {
    pending_paste = PASTE_CLIPBOARD;
    pending_dest  = dest_dir.empty() ? current_path : dest_dir;
    Fl::paste(*this, 1);
}

void FileView::clipboard_paste_into(const std::string& dest_dir) {
    clipboard_request_paste(dest_dir);
}

void FileView::handle_paste_event() {
    auto uris = parse_uri_list(Fl::event_text(), Fl::event_length());

    std::string dest = pending_dest.empty() ? current_path : pending_dest;
    bool is_dnd = (pending_paste == PASTE_DND);

    // For clipboard paste, honor cut semantics if the URIs match our internal
    // clipboard. Otherwise (or for DND), default to copy.
    bool move = false;
    if (!is_dnd) {
        auto& c = app_clipboard();
        if (c.op == AppClipboard::CUT && !c.paths.empty()) {
            bool all_match = uris.size() == c.paths.size();
            for (size_t i = 0; all_match && i < uris.size(); i++)
                if (uris[i] != c.paths[i]) all_match = false;
            if (all_match) move = true;
        }
    }

    pending_paste = PASTE_NONE;
    pending_dest.clear();

    if (uris.empty()) return;
    if (dest.empty()) return;

    std::error_code ec;
    if (!fs::is_directory(dest, ec)) return;

    for (const auto& src : uris) {
        if (src == dest) continue;
        transfer_one(src, dest, move);
    }

    if (move) {
        app_clipboard().paths.clear();
        X11Clipboard::clear();
    }

    set_location(current_path);
}

void FileView::start_dnd() {
    auto paths = selected_paths();
    if (paths.empty()) return;

    std::string text = build_uri_list(paths);
    Fl::copy(text.c_str(), (int)text.size(), 0);  // selection buffer (FLTK fallback)
    X11Clipboard::prepare_dnd(paths);

    dnd_in_progress = true;
    Fl::dnd();
    dnd_in_progress = false;
    X11Clipboard::done_dnd();
}

void FileView::open_entry(int idx) {
    if (idx < 0 || idx >= (int)entries.size()) return;
    const Entry& e = entries[idx];
    if (e.kind == Entry::FOLDER_K || e.kind == Entry::PARENT_K) {
        if (on_navigate_cb) on_navigate_cb(e.path);
    } else {
        open_with_default(e.path);
    }
}

void FileView::copy_entry_path(int idx) {
    if (idx < 0 || idx >= (int)entries.size()) return;
    const std::string& p = entries[idx].path;
    Fl::copy(p.c_str(), (int)p.size(), 1);
}

void FileView::open_terminal_here() {
    if (!current_path.empty()) open_terminal_at(current_path);
}

void FileView::create_new(bool folder) {
    const char* prompt  = folder ? "New folder name:" : "New file name:";
    const char* defname = folder ? "New folder"       : "New file.txt";
    const char* name = fl_input("%s", defname, prompt);
    if (!name || !*name) return;

    fs::path target = fs::path(current_path) / name;
    std::error_code ec;
    if (folder) {
        fs::create_directory(target, ec);
    } else {
        std::ofstream f(target);
        if (!f) ec = std::make_error_code(std::errc::io_error);
    }
    if (ec) {
        fl_alert("Could not create:\n%s\n\n%s", target.string().c_str(), ec.message().c_str());
    }
    set_location(current_path);
}

void FileView::show_entry_menu(int idx, int mx, int my) {
    if (idx < 0 || idx >= (int)entries.size()) return;
    const Entry& e = entries[idx];
    bool is_dir    = (e.kind == Entry::FOLDER_K || e.kind == Entry::PARENT_K);
    bool is_parent = (e.kind == Entry::PARENT_K);

    enum { A_OPEN = 1, A_OPEN_TAB, A_OPEN_WITH, A_OPEN_TERM, A_CUT, A_COPY,
           A_PASTE_HERE, A_COPY_PATH, A_PIN, A_UNPIN };

    bool single   = selection.size() == 1;
    bool pinned   = single && PinStore::instance().contains(e.path);

    Fl_Menu_Item items[16];
    int n = 0;
    items[n++] = { "Open", 0, nullptr, (void*)(intptr_t)A_OPEN, 0, 0, 0, 13, 0 };
    if (is_dir && on_open_tab_cb)
        items[n++] = { "Open in new tab", 0, nullptr, (void*)(intptr_t)A_OPEN_TAB,
                       0, 0, 0, 13, 0 };
    int open_with_flags = is_parent ? FL_MENU_INACTIVE : 0;
    items[n++] = { "Open with...", 0, nullptr, (void*)(intptr_t)A_OPEN_WITH,
                   open_with_flags, 0, 0, 13, 0 };
    if (is_dir)
        items[n++] = { "Open in Terminal", 0, nullptr, (void*)(intptr_t)A_OPEN_TERM,
                       FL_MENU_DIVIDER, 0, 0, 13, 0 };
    else
        items[n - 1].flags |= FL_MENU_DIVIDER;

    int cut_flags  = is_parent ? FL_MENU_INACTIVE : 0;
    int copy_flags = is_parent ? FL_MENU_INACTIVE : 0;
    items[n++] = { "Cut",   0, nullptr, (void*)(intptr_t)A_CUT,        cut_flags,  0, 0, 13, 0 };
    items[n++] = { "Copy",  0, nullptr, (void*)(intptr_t)A_COPY,       copy_flags, 0, 0, 13, 0 };
    int paste_flags = (is_dir && !app_clipboard().paths.empty()) ? FL_MENU_DIVIDER
                                                                 : (FL_MENU_DIVIDER | FL_MENU_INACTIVE);
    items[n++] = { is_dir ? "Paste into folder" : "Paste",
                   0, nullptr, (void*)(intptr_t)A_PASTE_HERE, paste_flags, 0, 0, 13, 0 };

    if (single && !is_parent) {
        if (pinned)
            items[n++] = { "Unpin from sidebar", 0, nullptr,
                           (void*)(intptr_t)A_UNPIN, FL_MENU_DIVIDER, 0, 0, 13, 0 };
        else
            items[n++] = { "Pin to sidebar",     0, nullptr,
                           (void*)(intptr_t)A_PIN,   FL_MENU_DIVIDER, 0, 0, 13, 0 };
    }

    items[n++] = { "Copy path", 0, nullptr, (void*)(intptr_t)A_COPY_PATH, 0, 0, 0, 13, 0 };
    items[n]   = { 0 };

    const Fl_Menu_Item* sel = items->popup(mx, my);
    if (!sel) return;
    switch ((int)(intptr_t)sel->user_data()) {
        case A_OPEN:       open_entry(idx); break;
        case A_OPEN_TAB:   if (on_open_tab_cb) on_open_tab_cb(e.path); break;
        case A_OPEN_WITH:  show_open_with(e.path); break;
        case A_OPEN_TERM:  open_terminal_at(e.path); break;
        case A_CUT:        clipboard_cut_selected(); break;
        case A_COPY:       clipboard_copy_selected(); break;
        case A_PASTE_HERE: clipboard_request_paste(is_dir ? e.path : current_path); break;
        case A_PIN:        PinStore::instance().add(e.path); break;
        case A_UNPIN:      PinStore::instance().remove(e.path); break;
        case A_COPY_PATH:  copy_entry_path(idx); break;
    }
}

void FileView::show_empty_menu(int mx, int my) {
    enum { A_NEW_FILE = 1, A_NEW_FOLDER, A_PASTE, A_OPEN_TERM };

    int paste_flags = app_clipboard().paths.empty()
        ? (FL_MENU_DIVIDER | FL_MENU_INACTIVE)
        : FL_MENU_DIVIDER;

    Fl_Menu_Item items[] = {
        { "New File",         0, nullptr, (void*)(intptr_t)A_NEW_FILE,   0,              0, 0, 13, 0 },
        { "New Folder",       0, nullptr, (void*)(intptr_t)A_NEW_FOLDER, FL_MENU_DIVIDER, 0, 0, 13, 0 },
        { "Paste",            0, nullptr, (void*)(intptr_t)A_PASTE,      paste_flags,     0, 0, 13, 0 },
        { "Open in Terminal", 0, nullptr, (void*)(intptr_t)A_OPEN_TERM,  0,              0, 0, 13, 0 },
        { 0 }
    };

    const Fl_Menu_Item* sel = items->popup(mx, my);
    if (!sel) return;
    switch ((int)(intptr_t)sel->user_data()) {
        case A_NEW_FILE:   create_new(false); break;
        case A_NEW_FOLDER: create_new(true);  break;
        case A_PASTE:      clipboard_request_paste(current_path); break;
        case A_OPEN_TERM:  open_terminal_here(); break;
    }
}

void FileView::apply_column_drag(int mx) {
    constexpr int MIN_W = 60;
    int delta = mx - drag_anchor_x;
    int new_w = std::max(MIN_W, drag_anchor_w + delta);
    switch (dragging_boundary) {
        case 0: col_w_name = std::max(120, new_w); break;
        case 1: col_w_type = new_w; break;
        case 2: col_w_date = new_w; break;
    }
    redraw();
}

void FileView::update_cursor(int mx, int my) {
    if (mode == DETAILS && boundary_at(mx, my) >= 0) {
        fl_cursor(FL_CURSOR_WE);
    } else {
        fl_cursor(FL_CURSOR_DEFAULT);
    }
}

int FileView::handle(int event) {
    // Forward to scrollbar when the pointer is over it or it has been grabbed.
    if (event == FL_PUSH || event == FL_DRAG || event == FL_RELEASE) {
        if (scrollbar->active() && Fl::event_inside(scrollbar)) {
            return Fl_Group::handle(event);
        }
    }

    switch (event) {
        case FL_ENTER:
            return 1;

        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;

        case FL_MOVE: {
            int mx = Fl::event_x(), my = Fl::event_y();
            update_cursor(mx, my);

            int prev = hovered;
            // Suppress row-hover when on a boundary so the resize cue is clean
            hovered = (mode == DETAILS && boundary_at(mx, my) >= 0)
                ? -1
                : item_index_at(mx, my);
            if (hovered != prev) redraw();
            return 1;
        }

        case FL_LEAVE:
            fl_cursor(FL_CURSOR_DEFAULT);
            if (hovered != -1) { hovered = -1; redraw(); }
            return 1;

        case FL_KEYDOWN: {
            if (Fl::event_state() & FL_CTRL) {
                switch (Fl::event_key()) {
                    case 'c': clipboard_copy_selected(); return 1;
                    case 'x': clipboard_cut_selected();  return 1;
                    case 'v': clipboard_request_paste(current_path); return 1;
                }
            }
            return 0;
        }

        case FL_DND_ENTER:
        case FL_DND_DRAG:
        case FL_DND_LEAVE:
            return 1;

        case FL_DND_RELEASE: {
            drop_x = Fl::event_x();
            drop_y = Fl::event_y();
            int idx = item_index_at(drop_x, drop_y);
            std::string dest = current_path;
            if (idx >= 0 && (entries[idx].kind == Entry::FOLDER_K
                          || entries[idx].kind == Entry::PARENT_K)) {
                dest = entries[idx].path;
            }
            pending_paste = PASTE_DND;
            pending_dest  = dest;
            return 1;
        }

        case FL_PASTE: {
            if (pending_paste != PASTE_NONE) handle_paste_event();
            return 1;
        }

        case FL_PUSH: {
            int mx = Fl::event_x(), my = Fl::event_y();
            int btn = Fl::event_button();
            int st  = Fl::event_state();

            take_focus();
            press_on_item = false;

            // Right click: context menu (no column-resize handling)
            if (btn == FL_RIGHT_MOUSE) {
                int idx = item_index_at(mx, my);
                if (idx >= 0) {
                    if (!selection.count(idx)) {
                        select_single(idx);
                    }
                    redraw();
                    show_entry_menu(idx, mx, my);
                } else {
                    clear_selection();
                    show_empty_menu(mx, my);
                }
                return 1;
            }

            // 1) Column boundary drag (details only)
            if (mode == DETAILS) {
                int b = boundary_at(mx, my);
                if (b >= 0) {
                    dragging_boundary = b;
                    drag_anchor_x     = mx;
                    drag_anchor_w     = (b == 0) ? col_w_name
                                      : (b == 1) ? col_w_type
                                                 : col_w_date;
                    return 1;
                }
            }

            // 2) Header click → sort
            if (mode == DETAILS) {
                int col = header_column_at(mx, my);
                if (col >= 0) {
                    if ((SortColumn)col == sort_col) sort_desc = !sort_desc;
                    else { sort_col = (SortColumn)col; sort_desc = false; }
                    sort_entries();
                    redraw();
                    return 1;
                }
            }

            // 3) Row click → select / double-click open / remember press
            int idx = item_index_at(mx, my);
            if (idx >= 0) {
                apply_click_selection(idx, st);
                redraw();
                if (Fl::event_clicks() > 0) {
                    open_entry(idx);
                    Fl::event_clicks(0);
                } else {
                    press_on_item = (entries[idx].kind != Entry::PARENT_K)
                                  && selection.count(idx) > 0;
                    press_x = mx;
                    press_y = my;
                }
            } else {
                clear_selection();
            }
            return 1;
        }

        case FL_DRAG: {
            if (dragging_boundary >= 0) {
                apply_column_drag(Fl::event_x());
                return 1;
            }
            if (press_on_item && !dnd_in_progress) {
                int dx = Fl::event_x() - press_x;
                int dy = Fl::event_y() - press_y;
                if (dx*dx + dy*dy > 25) {  // 5 px threshold
                    press_on_item = false;
                    start_dnd();
                    return 1;
                }
            }
            return 0;
        }

        case FL_RELEASE: {
            if (dragging_boundary >= 0) {
                dragging_boundary = -1;
                update_cursor(Fl::event_x(), Fl::event_y());
                return 1;
            }
            press_on_item = false;
            return 0;
        }

        case FL_MOUSEWHEEL: {
            int dy = Fl::event_dy();
            if (Fl::event_state() & FL_CTRL) {
                cycle_view_mode(dy < 0 ? 1 : -1);
            } else {
                scroll_y += dy * 40;
                clamp_scroll();
                redraw();
            }
            return 1;
        }
    }
    return Fl_Group::handle(event);
}
