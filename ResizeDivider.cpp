#include "ResizeDivider.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

ResizeDivider::ResizeDivider(int x, int y, int h, std::function<void(int)> callback)
    : Fl_Widget(x, y, 5, h), drag_cb(std::move(callback))
{
}

void ResizeDivider::draw() {
    fl_color(fl_rgb_color(229, 231, 235));
    fl_rectf(x(), y(), w(), h());
}

int ResizeDivider::handle(int event) {
    switch (event) {
        case FL_ENTER:
            fl_cursor(FL_CURSOR_WE);
            return 1;

        case FL_LEAVE:
            if (!dragging) fl_cursor(FL_CURSOR_DEFAULT);
            return 1;

        case FL_PUSH:
            dragging = true;
            fl_cursor(FL_CURSOR_WE);
            return 1;

        case FL_DRAG:
            if (dragging && drag_cb)
                drag_cb(Fl::event_x());
            return 1;

        case FL_RELEASE:
            dragging = false;
            fl_cursor(FL_CURSOR_DEFAULT);
            return 1;
    }
    return 0;
}
