#include <FL/Fl.H>
#include "SmartWindow.h"
#include "X11Clipboard.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char buf[3] = { s[i+1], s[i+2], 0 };
            out += (char)std::strtol(buf, nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string resolve_initial_path(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.empty() || a[0] == '-') continue;

        if (a.rfind("file://", 0) == 0) {
            a = a.substr(7);
            size_t slash = a.find('/');
            if (slash != std::string::npos) a = a.substr(slash);
            a = url_decode(a);
        }

        std::error_code ec;
        if (!fs::exists(a, ec)) continue;
        if (fs::is_directory(a, ec)) return a;
        return fs::path(a).parent_path().string();
    }

    const char* home = std::getenv("HOME");
    return home && *home ? std::string(home) : "/";
}

int main(int argc, char** argv) {
    std::string initial = resolve_initial_path(argc, argv);

    auto* window = new SmartWindow(1100, 680, "File Explorer", initial);

    window->end();
    window->show(argc, argv);

    X11Clipboard::init();

    return Fl::run();
}
