#pragma once

#include "core/id.hpp"
#include "core/workspace/workspace.hpp"
#include "core/snapshot/snapshot.hpp"

#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace remin::core {

// A snapshot: a point-in-time capture of a workspace's state.
struct Snapshot {
    SnapshotId id;
    std::chrono::system_clock::time_point timestamp;
    std::uint64_t size_bytes{0};
    int revision{0};
};

} // namespace remin::core
