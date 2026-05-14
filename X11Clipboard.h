#ifndef X11CLIPBOARD_H
#define X11CLIPBOARD_H

#include <string>
#include <vector>

// Native X11 clipboard owner for file operations. FLTK 1.3's Fl::copy only
// advertises text/plain, which file managers ignore. This module owns the
// CLIPBOARD selection ourselves and serves text/uri-list and
// x-special/gnome-copied-files so file managers can paste from us.
//
// It also pre-populates the source window's XdndTypeList so XDND drags
// originating in Fl::dnd() advertise text/uri-list.
namespace X11Clipboard {

enum Op { COPY, CUT };

void init();

// Take ownership of the CLIPBOARD selection with the given paths.
void set(Op op, const std::vector<std::string>& paths);

// Release ownership (after a cut+paste move).
void clear();

bool owns_clipboard();

// Stage a uri-list for the next XDND drag and prime XdndTypeList on the
// FLTK source window. Call right before Fl::dnd(); call done_dnd() after.
void prepare_dnd(const std::vector<std::string>& paths);
void done_dnd();

}  // namespace X11Clipboard

#endif
