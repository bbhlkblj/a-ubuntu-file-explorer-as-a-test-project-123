#ifndef RIBBONBAR_H
#define RIBBONBAR_H

#include <FL/Fl_Group.H>
#include <functional>
#include <string>
#include <vector>
#include "ModernInput.h"

class RibbonBar : public Fl_Group {
public:
    static constexpr int TAB_BAR_H  = 36;
    static constexpr int LOCATION_H = 44;
    static constexpr int TOTAL_H    = TAB_BAR_H + LOCATION_H;

private:
    struct Tab { std::string label; };

    static constexpr int TAB_W     = 148;
    static constexpr int TAB_GAP   = 2;
    static constexpr int PLUS_W    = 32;
    static constexpr int SIDE_PAD  = 8;
    static constexpr int CLOSE_W   = 22;
    static constexpr int NAV_BTN_W = 30;
    static constexpr int NAV_GAP   = 6;

    std::vector<Tab> tabs;
    int active_idx = 0;
    bool back_enabled    = false;
    bool fwd_enabled     = false;
    bool back_hovered    = false;
    bool fwd_hovered     = false;
    bool back_pressed    = false;
    bool fwd_pressed     = false;
    ModernInput* location_input;

    std::function<void(int)> on_tab_clicked_cb;
    std::function<void(int)> on_close_clicked_cb;
    std::function<void()>    on_plus_clicked_cb;
    std::function<void()>    on_back_cb;
    std::function<void()>    on_forward_cb;

    int  tab_x(int i) const;
    int  back_x() const;
    int  forward_x() const;
    int  location_x() const;
    int  location_w(int total_w) const;
    bool hit_close(int i, int ex, int ey) const;
    bool hit_tab(int i, int ex, int ey) const;
    bool hit_plus(int ex, int ey) const;
    bool hit_back(int ex, int ey) const;
    bool hit_forward(int ex, int ey) const;
    void draw_nav_button(int bx, int by, bool right_arrow,
                         bool enabled, bool hovered, bool pressed);

public:
    explicit RibbonBar(int x, int y, int w);

    void draw()            override;
    int  handle(int event) override;
    void resize(int x, int y, int w, int h) override;

    void set_callbacks(
        std::function<void(int)> tab_cb,
        std::function<void(int)> close_cb,
        std::function<void()>    plus_cb);

    void set_nav_callbacks(
        std::function<void()> back_cb,
        std::function<void()> forward_cb);

    void set_back_enabled(bool e);
    void set_forward_enabled(bool e);

    // Visual-only mutations — no callbacks fired
    void add_tab(const std::string& label);
    void remove_tab(int idx);
    void set_active(int idx);
    void set_tab_label(int idx, const std::string& label);

    int  tab_count()  const { return (int)tabs.size(); }
    int  active_tab() const { return active_idx; }

    ModernInput* location_bar() const { return location_input; }
};

#endif
