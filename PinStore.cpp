#include "PinStore.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

PinStore& PinStore::instance() {
    static PinStore p;
    return p;
}

std::string PinStore::config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string dir;
    if (xdg && *xdg) {
        dir = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home) home = "/tmp";
        dir = std::string(home) + "/.config";
    }
    dir += "/fileExplorer";
    return dir + "/pinned";
}

void PinStore::load() {
    paths_.clear();
    std::ifstream f(config_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // Reject duplicates and any line that isn't an absolute path
        if (line[0] != '/') continue;
        if (std::find(paths_.begin(), paths_.end(), line) == paths_.end())
            paths_.push_back(line);
    }
}

void PinStore::save() const {
    std::error_code ec;
    fs::path p = config_path();
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p);
    if (!f) return;
    for (const auto& s : paths_) f << s << '\n';
}

bool PinStore::contains(const std::string& path) const {
    return std::find(paths_.begin(), paths_.end(), path) != paths_.end();
}

void PinStore::add(const std::string& path) {
    if (path.empty() || contains(path)) return;
    paths_.push_back(path);
    save();
    if (on_change_) on_change_();
}

void PinStore::remove(const std::string& path) {
    auto it = std::find(paths_.begin(), paths_.end(), path);
    if (it == paths_.end()) return;
    paths_.erase(it);
    save();
    if (on_change_) on_change_();
}
