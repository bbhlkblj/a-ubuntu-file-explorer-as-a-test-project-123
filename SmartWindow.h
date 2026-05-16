#ifndef SMARTWINDOW_H
#define SMARTWINDOW_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <string>
#include <vector>

#include "RibbonBar.h"
#include "ResizeDivider.h"
#include "SidePanel.h"
#include "FileView.h"
#include "StatusBar.h"

class SmartWindow : public Fl_Window {
    static constexpr int DIVIDER_W   = 5;
    static constexpr int MIN_PANEL_W = 150;

    struct TabState {
        std::string              location    = "/home/martin";
        int                      sidebar_sel = -1;
        FileView::ViewMode       view_mode   = FileView::LARGE_ICONS;
        int                      scroll_y    = 0;
        std::vector<std::string> history     = {"/home/martin"};
        int                      history_idx = 0;
    };

    int left_w = 220;

    RibbonBar*     ribbon;
    SidePanel*     side_panel;
    ResizeDivider* divider;
    FileView*      file_view;
    StatusBar*     status_bar;

    std::vector<TabState> tab_states;

    void layout(int win_w, int win_h);
    void on_divider_drag(int cursor_x);

    void save_current_state();
    void load_tab_state(int idx);

    void handle_tab_click(int idx);
    void handle_tab_close(int idx);
    void handle_plus_click();
    void open_in_new_tab(const std::string& path);

    void navigate_to(const std::string& path);
    void go_back();
    void go_forward();
    void apply_current_location();
    void sync_nav_buttons();
    void on_location_submitted();
    static void location_cb(Fl_Widget* w, void* userdata);

public:
    SmartWindow(int w, int h, const char* title,
                const std::string& initial_path = {});

    void resize(int x, int y, int w, int h) override;

    RibbonBar* get_ribbon()     const { return ribbon; }
    SidePanel* get_side_panel() const { return side_panel; }
    FileView*  get_file_view()  const { return file_view; }
};

#endif
