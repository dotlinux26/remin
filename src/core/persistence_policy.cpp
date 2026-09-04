#include "core/persistence_policy.hpp"

#include <nlohmann/json.hpp>

namespace remin::core {

std::string PersistencePolicy::to_string() const {
    nlohmann::json j;
    j["default_recovery"] = default_recovery;
    j["closed_window_history"] = closed_window_history;
    j["input_checkpoint"] = input_checkpoint;
    return j.dump();
}

PersistencePolicy PersistencePolicy::from_string(const std::string& json) {
    PersistencePolicy p;
    if (json.empty()) return p;
    try {
        auto j = nlohmann::json::parse(json);
        if (j.contains("default_recovery")) p.default_recovery = j["default_recovery"].get<bool>();
        if (j.contains("closed_window_history")) p.closed_window_history = j["closed_window_history"].get<bool>();
        if (j.contains("input_checkpoint")) p.input_checkpoint = j["input_checkpoint"].get<bool>();
    } catch (...) {
        // Fall back to defaults on malformed config.
    }
    return p;
}

} // namespace remin::core
