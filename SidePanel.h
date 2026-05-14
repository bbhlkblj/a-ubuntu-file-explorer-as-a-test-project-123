#ifndef SIDEPANEL_H
#define SIDEPANEL_H

#include <FL/Fl_Widget.H>
#include <FL/Enumerations.H>
#include <functional>
#include <string>
#include <vector>

class SidePanel : public Fl_Widget {
public:
    struct Item {
        enum Type { HEADER, SEPARATOR, ENTRY } type = ENTRY;
        std::string label;
        Fl_Color    icon_color = 0;
        std::string path;
        bool        is_pinned  = false;
        bool        missing    = false;
    };

private:
    static constexpr int ENTRY_H   = 34;
    static constexpr int HEADER_H  = 28;
    static constexpr int SEP_H     = 17;
    static constexpr int ICON_SIZE = 16;
    static constexpr int TOP_PAD   = 8;
    static constexpr int ICON_X    = 14;
    static constexpr int TEXT_X    = 42;

    std::vector<Item>        items;
    std::vector<std::string> pinned_paths;
    int selected = -1;
    int hovered  = -1;
    std::function<void(const std::string&)> on_select_cb;
    std::function<void(const std::string&)> on_unpin_cb;

    void rebuild_items();
    void show_pinned_menu(int idx, int mx, int my);

    int  item_height(int i) const;
    int  item_y(int i) const;
    int  entry_at(int my) const;

public:
    SidePanel(int x, int y, int w, int h);

    void draw()            override;
    int  handle(int event) override;

    int  get_selected() const { return selected; }
    void set_selected(int i);

    void set_on_select(std::function<void(const std::string&)> cb) {
        on_select_cb = std::move(cb);
    }
    void set_on_unpin(std::function<void(const std::string&)> cb) {
        on_unpin_cb = std::move(cb);
    }

    void set_pinned_paths(const std::vector<std::string>& paths);
};

#endif
