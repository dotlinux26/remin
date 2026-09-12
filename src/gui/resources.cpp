#include "gui/resources.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace remin::gui {

namespace {

std::string executable_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    fs::path p(buf);
    if (!p.has_parent_path()) return {};
    return p.parent_path().string();
}

// <exe_dir>/../share/remin/resources — matches the layout produced by
// `cmake --install` (usr/bin/remin, usr/share/remin/resources), which is also
// how the AppImage stages its payload.
std::string install_relative_dir() {
    const std::string exe = executable_dir();
    if (exe.empty()) return {};
    fs::path candidate = fs::path(exe) / ".." / "share" / "remin" / "resources";
    if (!fs::is_directory(candidate)) return {};
    return candidate.lexically_normal().string();
}

} // namespace

std::string resource_dir() {
    if (const char* env = std::getenv("REMIN_RESOURCE_DIR")) {
        if (*env && fs::is_directory(env)) return env;
    }
    if (const std::string install = install_relative_dir(); !install.empty()) {
        return install;
    }
    return REMIN_RESOURCE_DIR;
}

std::string resource_path(const char* rel) {
    return resource_dir() + "/" + rel;
}

} // namespace remin::gui