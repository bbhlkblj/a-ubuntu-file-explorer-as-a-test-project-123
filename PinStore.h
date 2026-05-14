#ifndef PINSTORE_H
#define PINSTORE_H

#include <functional>
#include <string>
#include <vector>

class PinStore {
public:
    static PinStore& instance();

    void load();
    void save() const;

    bool contains(const std::string& path) const;
    void add(const std::string& path);
    void remove(const std::string& path);

    const std::vector<std::string>& paths() const { return paths_; }

    // Called whenever the pinned list changes (after add/remove).
    void set_on_change(std::function<void()> cb) { on_change_ = std::move(cb); }

private:
    std::vector<std::string>  paths_;
    std::function<void()>     on_change_;

    static std::string config_path();
};

#endif
