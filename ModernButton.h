#ifndef MODERNBUTTON_H
#define MODERNBUTTON_H

#include <FL/Fl_Button.H>

class ModernButton : public Fl_Button {
private:
    bool is_hovered = false;
    bool is_pressed = false;

public:
    ModernButton(int x, int y, int w, int h, const char* label = nullptr);

    int handle(int event) override;
    void draw() override;
};

#endif
