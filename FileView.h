#ifndef FILEVIEW_H
#define FILEVIEW_H

#include <FL/Fl_Widget.H>
#include <ctime>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

class FileView : public Fl_Widget {
public:
    enum ViewMode {
        DETAILS = 0,
        SMALL_ICONS,
        MEDIUM_ICONS,
        LARGE_ICONS,
        EXTRA_LARGE_ICONS,
        MODE_COUNT
    };

    enum SortColumn { SORT_NAME = 0, SORT_TYPE, SORT_DATE, SORT_SIZE };

    struct Entry {
        enum Kind { FILE_K, FOLDER_K, LINK_K, PARENT_K };
        std::string    name;
        std::string    path;
        Kind           kind  = FILE_K;
        std::uintmax_t size  = 0;
        std::time_t    mtime = 0;
    };

private:
    std::vector<Entry> entries;
    std::string        current_path;
    ViewMode           mode      = LARGE_ICONS;
    int                scroll_y  = 0;
    std::set<int>      selection;
    int                anchor    = -1;
    int                hovered   = -1;

    SortColumn         sort_col  = SORT_NAME;
    bool               sort_desc = false;

    // Column widths for DETAILS view (drag-resizable)
    int                col_w_name = 280;
    int                col_w_type = 100;
    int                col_w_date = 160;
    int                col_w_size = 100;

    int                dragging_boundary = -1;  // 0,1,2 or -1
    int                drag_anchor_x     = 0;
    int                drag_anchor_w     = 0;

    // Outgoing-DND tracking
    int                press_x = 0;
    int                press_y = 0;
    bool               press_on_item    = false;
    bool               dnd_in_progress  = false;

    // Incoming-DND / paste tracking
    enum PendingPaste { PASTE_NONE, PASTE_CLIPBOARD, PASTE_DND };
    PendingPaste       pending_paste = PASTE_NONE;
    std::string        pending_dest;
    int                drop_x = 0;
    int                drop_y = 0;

    std::function<void(const std::string&)> on_navigate_cb;
    std::function<void(int sel, int total)> on_counts_cb;

    int  row_h()    const;
    int  cell_w()   const;
    int  cell_h()   const;
    int  icon_sz()  const;
    int  columns()  const;
    int  content_h() const;
    int  item_index_at(int mx, int my) const;
    void item_rect(int idx, int& ix, int& iy, int& iw, int& ih) const;
    void clamp_scroll();

    void show_entry_menu(int idx, int mx, int my);
    void show_empty_menu(int mx, int my);
    void open_entry(int idx);
    void copy_entry_path(int idx);
    void open_terminal_here();
    void create_new(bool folder);

    void clipboard_copy_selected();
    void clipboard_cut_selected();
    void clipboard_paste_into(const std::string& dest_dir);
    void clipboard_request_paste(const std::string& dest_dir);
    void start_dnd();
    void handle_paste_event();
    bool is_cut(const std::string& path) const;

    std::vector<std::string> selected_paths() const;
    void clear_selection();
    void select_single(int idx);
    void apply_click_selection(int idx, int state);

    int  name_col_x()  const;
    int  boundary_x(int b) const;
    int  header_h()    const { return 32; }
    int  boundary_at(int mx, int my) const;  // returns 0..2, or -1
    int  header_column_at(int mx, int my) const;  // returns SortColumn or -1

    void apply_column_drag(int mx);
    void sort_entries();
    void update_cursor(int mx, int my);

    void draw_details();
    void draw_grid();
    void draw_header_label(const char* text, int hx, int hw,
                           bool is_sort_col, int align);
    void draw_icon(const Entry& e, int ix, int iy, int sz);
    void draw_folder_icon(int ix, int iy, int sz);
    void draw_file_icon  (int ix, int iy, int sz);
    void draw_link_icon  (int ix, int iy, int sz);
    void draw_parent_icon(int ix, int iy, int sz);

    static std::string format_size(std::uintmax_t bytes);
    static std::string format_time(std::time_t t);
    static std::string kind_string(Entry::Kind k);

public:
    FileView(int x, int y, int w, int h);

    void draw()            override;
    int  handle(int event) override;
    void resize(int x, int y, int w, int h) override;

    void set_location(const std::string& path);
    const std::string& location() const { return current_path; }

    void set_view_mode(ViewMode m);
    ViewMode view_mode() const { return mode; }
    void cycle_view_mode(int direction);

    void set_on_navigate(std::function<void(const std::string&)> cb) {
        on_navigate_cb = std::move(cb);
    }
    void set_on_counts_changed(std::function<void(int, int)> cb) {
        on_counts_cb = std::move(cb);
    }

    void notify_counts() const;
};

#endif
