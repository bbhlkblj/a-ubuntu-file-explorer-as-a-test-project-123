#include "SmartWindow.h"
#include "PinStore.h"

#include <algorithm>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>
#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace {

void launch_xdg_open_detached(const std::string& path) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

std::string tab_label_for_path(const std::string& path) {
    if (path.empty()) return "Home";
    std::string s = path;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    std::filesystem::path p(s);
    std::string name = p.filename().string();
    if (!name.empty()) return name;
    return s.empty() ? "/" : s;
}
}

SmartWindow::SmartWindow(int w, int h, const char* title)
    : Fl_Window(w, h, title)
{
    color(fl_rgb_color(255, 255, 255));

    const int content_y = RibbonBar::TOTAL_H;
    const int content_h = h - content_y - StatusBar::HEIGHT;
    const int right_x   = left_w + DIVIDER_W;

    ribbon     = new RibbonBar(0, 0, w);
    side_panel = new SidePanel(0, content_y, left_w, content_h);

    divider = new ResizeDivider(left_w, content_y, content_h, [this](int cx) {
        on_divider_drag(cx);
    });

    file_view  = new FileView(right_x, content_y, w - right_x, content_h);
    status_bar = new StatusBar(0, h - StatusBar::HEIGHT, w);
    file_view->set_on_navigate([this](const std::string& path) {
        navigate_to(path);
    });
    file_view->set_on_counts_changed([this](int sel, int total) {
        status_bar->set_counts(sel, total);
    });

    side_panel->set_on_select([this](const std::string& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            navigate_to(path);
        } else if (std::filesystem::exists(path, ec)) {
            launch_xdg_open_detached(path);
        }
    });
    side_panel->set_on_unpin([](const std::string& path) {
        PinStore::instance().remove(path);
    });

    PinStore::instance().load();
    PinStore::instance().set_on_change([this]() {
        side_panel->set_pinned_paths(PinStore::instance().paths());
    });
    side_panel->set_pinned_paths(PinStore::instance().paths());

    ribbon->set_nav_callbacks(
        [this]() { go_back(); },
        [this]() { go_forward(); });

    tab_states.push_back(TabState{});

    ribbon->set_callbacks(
        [this](int idx) { handle_tab_click(idx); },
        [this](int idx) { handle_tab_close(idx); },
        [this]()        { handle_plus_click(); });

    ribbon->location_bar()->when(FL_WHEN_ENTER_KEY);
    ribbon->location_bar()->callback(&SmartWindow::location_cb, this);

    file_view->set_location(tab_states[0].location);
    file_view->set_view_mode(tab_states[0].view_mode);
    ribbon->set_tab_label(0, tab_label_for_path(tab_states[0].location));
    sync_nav_buttons();

    resizable(file_view);
    size_range(640, 400);
}

void SmartWindow::location_cb(Fl_Widget*, void* userdata) {
    static_cast<SmartWindow*>(userdata)->on_location_submitted();
}

void SmartWindow::on_location_submitted() {
    navigate_to(ribbon->location_bar()->value());
}

void SmartWindow::navigate_to(const std::string& path) {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) return;

    TabState& s = tab_states[idx];
    if (s.history_idx >= 0 && s.history_idx < (int)s.history.size()
        && s.history[s.history_idx] == path) {
        // Same location — just refresh
        s.location = path;
        apply_current_location();
        return;
    }

    // Truncate forward history before pushing
    if (s.history_idx + 1 < (int)s.history.size())
        s.history.erase(s.history.begin() + s.history_idx + 1, s.history.end());

    s.history.push_back(path);
    s.history_idx = (int)s.history.size() - 1;
    s.location    = path;

    apply_current_location();
}

void SmartWindow::go_back() {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) return;
    TabState& s = tab_states[idx];
    if (s.history_idx <= 0) return;

    s.history_idx--;
    s.location = s.history[s.history_idx];
    apply_current_location();
}

void SmartWindow::go_forward() {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) return;
    TabState& s = tab_states[idx];
    if (s.history_idx + 1 >= (int)s.history.size()) return;

    s.history_idx++;
    s.location = s.history[s.history_idx];
    apply_current_location();
}

void SmartWindow::apply_current_location() {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) return;
    const TabState& s = tab_states[idx];

    ribbon->location_bar()->value(s.location.c_str());
    ribbon->set_tab_label(idx, tab_label_for_path(s.location));
    file_view->set_location(s.location);
    sync_nav_buttons();
}

void SmartWindow::sync_nav_buttons() {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) {
        ribbon->set_back_enabled(false);
        ribbon->set_forward_enabled(false);
        return;
    }
    const TabState& s = tab_states[idx];
    ribbon->set_back_enabled(s.history_idx > 0);
    ribbon->set_forward_enabled(s.history_idx + 1 < (int)s.history.size());
}

void SmartWindow::save_current_state() {
    int idx = ribbon->active_tab();
    if (idx < 0 || idx >= (int)tab_states.size()) return;
    tab_states[idx].location    = ribbon->location_bar()->value();
    tab_states[idx].sidebar_sel = side_panel->get_selected();
    tab_states[idx].view_mode   = file_view->view_mode();
}

void SmartWindow::load_tab_state(int idx) {
    if (idx < 0 || idx >= (int)tab_states.size()) return;
    ribbon->location_bar()->value(tab_states[idx].location.c_str());
    ribbon->set_tab_label(idx, tab_label_for_path(tab_states[idx].location));
    side_panel->set_selected(tab_states[idx].sidebar_sel);
    file_view->set_view_mode(tab_states[idx].view_mode);
    file_view->set_location(tab_states[idx].location);
    sync_nav_buttons();
}

void SmartWindow::handle_tab_click(int idx) {
    if (idx == ribbon->active_tab()) return;
    save_current_state();
    ribbon->set_active(idx);
    load_tab_state(idx);
}

void SmartWindow::handle_tab_close(int idx) {
    if (ribbon->tab_count() == 1) {
        hide();
        return;
    }

    int was_active = ribbon->active_tab();

    ribbon->remove_tab(idx);
    tab_states.erase(tab_states.begin() + idx);

    int new_count = ribbon->tab_count();
    int new_active;
    if (idx == was_active)
        new_active = (idx < new_count) ? idx : idx - 1;
    else if (idx < was_active)
        new_active = was_active - 1;
    else
        new_active = was_active;

    ribbon->set_active(new_active);
    load_tab_state(new_active);
}

void SmartWindow::handle_plus_click() {
    save_current_state();
    tab_states.push_back(TabState{});
    ribbon->add_tab(tab_label_for_path(tab_states.back().location));
    int new_idx = ribbon->tab_count() - 1;
    ribbon->set_active(new_idx);
    load_tab_state(new_idx);
}

void SmartWindow::on_divider_drag(int cursor_x) {
    left_w = std::clamp(cursor_x, MIN_PANEL_W, w() - DIVIDER_W - MIN_PANEL_W);
    layout(w(), h());
    redraw();
}

void SmartWindow::layout(int win_w, int win_h) {
    const int content_y = RibbonBar::TOTAL_H;
    const int content_h = win_h - content_y - StatusBar::HEIGHT;
    const int right_x   = left_w + DIVIDER_W;

    ribbon->resize(0, 0, win_w, RibbonBar::TOTAL_H);
    side_panel->resize(0, content_y, left_w, content_h);
    divider->resize(left_w, content_y, DIVIDER_W, content_h);
    file_view->resize(right_x, content_y, win_w - right_x, content_h);
    status_bar->resize(0, win_h - StatusBar::HEIGHT, win_w, StatusBar::HEIGHT);
}

void SmartWindow::resize(int nx, int ny, int nw, int nh) {
    Fl_Window::resize(nx, ny, nw, nh);
    layout(nw, nh);
}
