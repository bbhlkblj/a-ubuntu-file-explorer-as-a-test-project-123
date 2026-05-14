#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <FL/Fl_Widget.H>

class StatusBar : public Fl_Widget {
public:
    static constexpr int HEIGHT = 26;

    StatusBar(int x, int y, int w);

    void draw() override;

    void set_counts(int selected, int total);

private:
    int sel_count   = 0;
    int total_count = 0;
};

#endif
