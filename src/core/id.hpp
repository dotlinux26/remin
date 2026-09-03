#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <random>
#include <sstream>
#include <locale>

namespace remin::core {

// Type-safe-ish identifier wrappers. Each domain gets a distinct id type
// to prevent mixing up workspace/window/tab/pane ids at compile time.
//
// Intentionally simple: string-based UUIDs are stable across processes
// (needed for IPC where a CLI string id must match a GUI object id).

template <typename Tag>
class Id {
public:
    Id() = default;
    explicit Id(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& str() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] bool operator==(const Id& o) const noexcept { return value_ == o.value_; }
    [[nodiscard]] bool operator<(const Id& o) const noexcept { return value_ < o.value_; }

    // Generate a new unique id (v4-ish, hex without dashes for compactness).
    // Explicitly classic-locale so hex output is identical regardless of the
    // process locale (GTK calls setlocale, which can insert thousands
    // separators into numeric output otherwise).
    static Id generate() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::hex << dist(gen) << dist(gen);
        return Id{os.str()};
    }

private:
    std::string value_;
};

struct WorkspaceTag {};
struct WindowTag {};
struct TabTag {};
struct PaneTag {};
struct SnapshotTag {};

using WorkspaceId = Id<WorkspaceTag>;
using WindowId = Id<WindowTag>;
using TabId = Id<TabTag>;
using PaneId = Id<PaneTag>;
using SnapshotId = Id<SnapshotTag>;

} // namespace remin::core
