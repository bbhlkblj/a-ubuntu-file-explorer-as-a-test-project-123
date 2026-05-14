#ifndef RESIZEDIVIDER_H
#define RESIZEDIVIDER_H

#include <FL/Fl_Widget.H>
#include <functional>

class ResizeDivider : public Fl_Widget {
    bool dragging = false;
    std::function<void(int)> drag_cb;

public:
    ResizeDivider(int x, int y, int h, std::function<void(int)> callback);

    void draw()   override;
    int  handle(int event) override;
};

#endif
