#include "ModernButton.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

ModernButton::ModernButton(int x, int y, int w, int h, const char* label)
    : Fl_Button(x, y, w, h, label) {
    box(FL_FLAT_BOX);
}

int ModernButton::handle(int event) {
    switch (event) {
        case FL_ENTER:
            is_hovered = true;
            redraw();
            return 1;

        case FL_LEAVE:
            is_hovered = false;
            is_pressed = false;
            redraw();
            return 1;

        case FL_PUSH:
            take_focus();
            is_pressed = true;
            redraw();
            return 1;

        case FL_RELEASE:
            if (is_pressed) {
                is_pressed = false;
                redraw();
                do_callback();
            }
            return 1;

        default:
            return Fl_Button::handle(event);
    }
}

void ModernButton::draw() {
    Fl_Color bg_color = fl_rgb_color(79, 70, 229);

    if (is_pressed) {
        bg_color = fl_rgb_color(67, 56, 202);
    } else if (is_hovered) {
        bg_color = fl_rgb_color(99, 102, 241);
    }

    fl_color(bg_color);
    fl_rectf(x(), y(), w(), h());

    fl_color(FL_WHITE);
    fl_font(FL_HELVETICA_BOLD, 16);
    fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_CENTER);
}
