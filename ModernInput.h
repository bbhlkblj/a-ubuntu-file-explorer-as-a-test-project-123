#ifndef MODERNINPUT_H
#define MODERNINPUT_H

#include <FL/Fl_Input.H>

class ModernInput : public Fl_Input {
public:
    ModernInput(int x, int y, int w, int h, const char* label = nullptr);

    int handle(int event) override;
    void draw() override;
};

#endif
