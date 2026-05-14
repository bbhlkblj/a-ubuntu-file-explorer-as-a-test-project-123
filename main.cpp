#include <FL/Fl.H>
#include "SmartWindow.h"
#include "X11Clipboard.h"

int main(int argc, char** argv) {
    SmartWindow* window = new SmartWindow(1100, 680, "File Explorer");

    window->end();
    window->show(argc, argv);

    X11Clipboard::init();

    return Fl::run();
}
