#include "ModernInput.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

ModernInput::ModernInput(int x, int y, int w, int h, const char* label)
    : Fl_Input(x, y, w, h, label) {
    box(FL_FLAT_BOX);

    color(FL_WHITE);
    textcolor(FL_BLACK);
    cursor_color(fl_rgb_color(79, 70, 229));
    selection_color(fl_rgb_color(165, 180, 252));

    textfont(FL_HELVETICA);
    textsize(16);
}

int ModernInput::handle(int event) {
    int result = Fl_Input::handle(event);
    // Force a full redraw on any state-changing event. Partial damage from
    // Fl_Input's internal scroll optimization can leave the area to the
    // left of a selection unpainted, especially when the input has scrolled
    // text and the user clicks/drags to select.
    switch (event) {
        case FL_FOCUS:
        case FL_UNFOCUS:
        case FL_KEYBOARD:
        case FL_PUSH:
        case FL_DRAG:
        case FL_RELEASE:
        case FL_PASTE:
            redraw();
            break;
    }
    return result;
}

void ModernInput::draw() {
    Fl_Input::draw();

    bool focused = (Fl::focus() == this);
    Fl_Color border = focused
        ? fl_rgb_color(79, 70, 229)
        : fl_rgb_color(229, 231, 235);

    fl_color(border);
    fl_rect(x(), y(), w(), h());
    if (focused)
        fl_rect(x() + 1, y() + 1, w() - 2, h() - 2);
}
