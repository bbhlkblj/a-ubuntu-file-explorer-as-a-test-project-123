#include "RibbonBar.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

RibbonBar::RibbonBar(int x, int y, int w)
    : Fl_Group(x, y, w, TOTAL_H)
{
    tabs.push_back({"/home/martin"});

    const int pad = 8;
    int lx = x + pad + 2 * NAV_BTN_W + NAV_GAP * 2;
    location_input = new ModernInput(
        lx, y + TAB_BAR_H + pad,
        x + w - pad - lx, LOCATION_H - 2 * pad);
    location_input->value("/home/martin");

    end();
    box(FL_NO_BOX);
}

void RibbonBar::set_callbacks(
    std::function<void(int)> tab_cb,
    std::function<void(int)> close_cb,
    std::function<void()>    plus_cb)
{
    on_tab_clicked_cb   = std::move(tab_cb);
    on_close_clicked_cb = std::move(close_cb);
    on_plus_clicked_cb  = std::move(plus_cb);
}

void RibbonBar::set_nav_callbacks(
    std::function<void()> back_cb,
    std::function<void()> forward_cb)
{
    on_back_cb    = std::move(back_cb);
    on_forward_cb = std::move(forward_cb);
}

void RibbonBar::set_back_enabled(bool e) {
    if (back_enabled == e) return;
    back_enabled = e;
    if (!e) back_pressed = false;
    redraw();
}

void RibbonBar::set_forward_enabled(bool e) {
    if (fwd_enabled == e) return;
    fwd_enabled = e;
    if (!e) fwd_pressed = false;
    redraw();
}

int RibbonBar::back_x() const {
    return x() + SIDE_PAD;
}

int RibbonBar::forward_x() const {
    return back_x() + NAV_BTN_W + NAV_GAP;
}

int RibbonBar::location_x() const {
    return forward_x() + NAV_BTN_W + NAV_GAP;
}

int RibbonBar::location_w(int total_w) const {
    return x() + total_w - SIDE_PAD - location_x();
}

bool RibbonBar::hit_back(int ex, int ey) const {
    int by = y() + TAB_BAR_H + (LOCATION_H - NAV_BTN_W) / 2;
    return ex >= back_x() && ex < back_x() + NAV_BTN_W
        && ey >= by         && ey < by + NAV_BTN_W;
}

bool RibbonBar::hit_forward(int ex, int ey) const {
    int by = y() + TAB_BAR_H + (LOCATION_H - NAV_BTN_W) / 2;
    return ex >= forward_x() && ex < forward_x() + NAV_BTN_W
        && ey >= by            && ey < by + NAV_BTN_W;
}

void RibbonBar::draw_nav_button(int bx, int by, bool right_arrow,
                                bool enabled, bool hovered, bool pressed) {
    Fl_Color bg = fl_rgb_color(243, 244, 246);
    if (enabled && pressed)      bg = fl_rgb_color(209, 213, 219);
    else if (enabled && hovered) bg = fl_rgb_color(229, 231, 235);

    fl_color(bg);
    fl_rectf(bx, by, NAV_BTN_W, NAV_BTN_W);

    Fl_Color fg = enabled
        ? fl_rgb_color(55, 65, 81)
        : fl_rgb_color(209, 213, 219);

    // Draw chevron arrow as two line segments
    int cx = bx + NAV_BTN_W / 2;
    int cy = by + NAV_BTN_W / 2;
    int arm = 5;
    fl_color(fg);
    fl_line_style(FL_SOLID, 2, nullptr);
    if (right_arrow) {
        fl_line(cx - arm / 2, cy - arm, cx + arm / 2, cy);
        fl_line(cx + arm / 2, cy,       cx - arm / 2, cy + arm);
    } else {
        fl_line(cx + arm / 2, cy - arm, cx - arm / 2, cy);
        fl_line(cx - arm / 2, cy,       cx + arm / 2, cy + arm);
    }
    fl_line_style(0);
}

void RibbonBar::add_tab(const std::string& label) {
    tabs.push_back({label});
    redraw();
}

void RibbonBar::remove_tab(int idx) {
    if (idx >= 0 && idx < (int)tabs.size())
        tabs.erase(tabs.begin() + idx);
    redraw();
}

void RibbonBar::set_active(int idx) {
    if (idx >= 0 && idx < (int)tabs.size())
        active_idx = idx;
    redraw();
}

void RibbonBar::set_tab_label(int idx, const std::string& label) {
    if (idx < 0 || idx >= (int)tabs.size()) return;
    if (tabs[idx].label == label) return;
    tabs[idx].label = label;
    redraw();
}

int RibbonBar::tab_x(int i) const {
    return x() + SIDE_PAD + i * (TAB_W + TAB_GAP);
}

bool RibbonBar::hit_close(int i, int ex, int ey) const {
    int cx = tab_x(i) + TAB_W - CLOSE_W;
    return ex >= cx && ex < cx + CLOSE_W && ey >= y() && ey < y() + TAB_BAR_H;
}

bool RibbonBar::hit_tab(int i, int ex, int ey) const {
    int tx = tab_x(i);
    return ex >= tx && ex < tx + TAB_W && ey >= y() && ey < y() + TAB_BAR_H;
}

bool RibbonBar::hit_plus(int ex, int ey) const {
    int px = tab_x((int)tabs.size());
    return ex >= px && ex < px + PLUS_W && ey >= y() && ey < y() + TAB_BAR_H;
}

void RibbonBar::draw() {
    // Tab bar background
    fl_color(fl_rgb_color(243, 244, 246));
    fl_rectf(x(), y(), w(), TAB_BAR_H);

    for (int i = 0; i < (int)tabs.size(); i++) {
        int tx  = tab_x(i);
        int ty  = y();
        bool act = (i == active_idx);

        if (act) {
            fl_color(fl_rgb_color(255, 255, 255));
            fl_rectf(tx, ty, TAB_W, TAB_BAR_H);
            fl_color(fl_rgb_color(79, 70, 229));
            fl_rectf(tx, ty + TAB_BAR_H - 2, TAB_W, 2);
        }

        // Label (clipped away from close area)
        fl_push_clip(tx + 8, ty, TAB_W - CLOSE_W - 10, TAB_BAR_H);
        fl_font(FL_HELVETICA, 13);
        fl_color(act ? fl_rgb_color(17, 24, 39) : fl_rgb_color(107, 114, 128));
        fl_draw(tabs[i].label.c_str(), tx + 8, ty, TAB_W - CLOSE_W - 10, TAB_BAR_H,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_pop_clip();

        // Close button ×
        fl_font(FL_HELVETICA, 13);
        fl_color(act ? fl_rgb_color(107, 114, 128) : fl_rgb_color(190, 190, 195));
        fl_draw("\xc3\x97", tx + TAB_W - CLOSE_W, ty, CLOSE_W, TAB_BAR_H,
                FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

        // Separator between non-adjacent inactive tabs
        if (!act && i + 1 < (int)tabs.size() && (i + 1) != active_idx) {
            fl_color(fl_rgb_color(209, 213, 219));
            fl_line(tx + TAB_W, ty + 8, tx + TAB_W, ty + TAB_BAR_H - 8);
        }
    }

    // Plus button
    {
        int px = tab_x((int)tabs.size());
        fl_font(FL_HELVETICA, 18);
        fl_color(fl_rgb_color(107, 114, 128));
        fl_draw("+", px, y(), PLUS_W, TAB_BAR_H, FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    }

    // Location bar background
    fl_color(fl_rgb_color(255, 255, 255));
    fl_rectf(x(), y() + TAB_BAR_H, w(), LOCATION_H);

    // Back / forward buttons
    int nav_y = y() + TAB_BAR_H + (LOCATION_H - NAV_BTN_W) / 2;
    draw_nav_button(back_x(),    nav_y, false, back_enabled, back_hovered, back_pressed);
    draw_nav_button(forward_x(), nav_y, true,  fwd_enabled,  fwd_hovered,  fwd_pressed);

    // Bottom border
    fl_color(fl_rgb_color(229, 231, 235));
    fl_line(x(), y() + TOTAL_H - 1, x() + w() - 1, y() + TOTAL_H - 1);

    draw_children();
}

int RibbonBar::handle(int event) {
    if (Fl_Group::handle(event)) return 1;

    int ex = Fl::event_x(), ey = Fl::event_y();

    switch (event) {
        case FL_MOVE: {
            bool bh = hit_back(ex, ey);
            bool fh = hit_forward(ex, ey);
            if (bh != back_hovered || fh != fwd_hovered) {
                back_hovered = bh;
                fwd_hovered  = fh;
                redraw();
            }
            return 1;
        }
        case FL_LEAVE:
            if (back_hovered || fwd_hovered) {
                back_hovered = fwd_hovered = false;
                redraw();
            }
            return 1;

        case FL_PUSH: {
            if (back_enabled && hit_back(ex, ey)) {
                back_pressed = true;
                redraw();
                return 1;
            }
            if (fwd_enabled && hit_forward(ex, ey)) {
                fwd_pressed = true;
                redraw();
                return 1;
            }
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (hit_close(i, ex, ey)) {
                    if (on_close_clicked_cb) on_close_clicked_cb(i);
                    return 1;
                }
                if (hit_tab(i, ex, ey)) {
                    if (on_tab_clicked_cb) on_tab_clicked_cb(i);
                    return 1;
                }
            }
            if (hit_plus(ex, ey)) {
                if (on_plus_clicked_cb) on_plus_clicked_cb();
                return 1;
            }
            return 0;
        }

        case FL_RELEASE: {
            if (back_pressed) {
                back_pressed = false;
                redraw();
                if (back_enabled && hit_back(ex, ey) && on_back_cb) on_back_cb();
                return 1;
            }
            if (fwd_pressed) {
                fwd_pressed = false;
                redraw();
                if (fwd_enabled && hit_forward(ex, ey) && on_forward_cb) on_forward_cb();
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

void RibbonBar::resize(int nx, int ny, int nw, int nh) {
    Fl_Group::resize(nx, ny, nw, nh);
    const int pad = 8;
    int lx = location_x();
    location_input->resize(lx, ny + TAB_BAR_H + pad,
                           nx + nw - pad - lx, LOCATION_H - 2 * pad);
}
