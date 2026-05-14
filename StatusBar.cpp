#include "StatusBar.h"

#include <FL/fl_draw.H>
#include <cstdio>

StatusBar::StatusBar(int x, int y, int w)
    : Fl_Widget(x, y, w, HEIGHT)
{
    box(FL_FLAT_BOX);
}

void StatusBar::set_counts(int selected, int total) {
    if (selected == sel_count && total == total_count) return;
    sel_count   = selected;
    total_count = total;
    redraw();
}

void StatusBar::draw() {
    fl_color(fl_rgb_color(249, 250, 251));
    fl_rectf(x(), y(), w(), h());

    fl_color(fl_rgb_color(229, 231, 235));
    fl_line(x(), y(), x() + w() - 1, y());

    char buf[64];
    if (total_count == 0) {
        std::snprintf(buf, sizeof(buf), "Empty folder");
    } else if (sel_count == 0) {
        std::snprintf(buf, sizeof(buf), "%d item%s", total_count, total_count == 1 ? "" : "s");
    } else {
        std::snprintf(buf, sizeof(buf), "%d of %d selected", sel_count, total_count);
    }

    fl_font(FL_HELVETICA, 12);
    fl_color(fl_rgb_color(75, 85, 99));
    fl_draw(buf, x() + 12, y(), w() - 24, h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}
