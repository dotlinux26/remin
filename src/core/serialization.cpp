#include "core/serialization.hpp"

#include <iomanip>
#include <sstream>
#include <cstdio>

namespace remin::core {

std::string to_iso8601(std::chrono::system_clock::time_point tp) {
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() % 1000;
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return os.str();
}

std::chrono::system_clock::time_point from_iso8601(const std::string& s) {
    std::tm tm{};
    int ms = 0;
    std::istringstream is(s);
    is >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (is.fail()) return {};
    // try to parse .mmmZ
    const auto dot = s.find('.');
    if (dot != std::string::npos) {
        const auto rest = s.substr(dot + 1);
        std::size_t consumed = 0;
        try {
            ms = std::stoi(rest, &consumed);
        } catch (...) {
            ms = 0;
        }
    }
    auto time_t_val = timegm(&tm);
    return std::chrono::system_clock::from_time_t(time_t_val) + std::chrono::milliseconds(ms);
}

json pane_tree_to_json(const PaneTree& tree) {
    switch (tree.kind()) {
        case PaneTree::Kind::Pane:
            if (tree.pane()) {
                json j;
                to_json(j, *tree.pane());
                return {{"kind", "pane"}, {"pane", j}};
            }
            return {{"kind", "empty"}};
        case PaneTree::Kind::SplitHorizontal:
            return {
                {"kind", "split"},
                {"orientation", "horizontal"},
                {"ratio", tree.ratio()},
                {"first", pane_tree_to_json(*tree.first())},
                {"second", pane_tree_to_json(*tree.second())},
            };
        case PaneTree::Kind::SplitVertical:
            return {
                {"kind", "split"},
                {"orientation", "vertical"},
                {"ratio", tree.ratio()},
                {"first", pane_tree_to_json(*tree.first())},
                {"second", pane_tree_to_json(*tree.second())},
            };
    }
    return {};
}

PaneTree pane_tree_from_json(const json& j) {
    const auto kind = j.value("kind", std::string{"pane"});
    if (kind == "pane" && j.contains("pane")) {
        Pane p;
        from_json(j.at("pane"), p);
        return PaneTree::leaf(std::move(p));
    }
    if (kind == "empty") {
        return PaneTree::leaf(Pane{});
    }
    // split
    const auto orient = j.value("orientation", std::string{"horizontal"});
    const auto ratio = j.value("ratio", 0.5);
    auto first = j.contains("first") ? pane_tree_from_json(j.at("first")) : PaneTree::leaf(Pane{});
    auto second = j.contains("second") ? pane_tree_from_json(j.at("second")) : PaneTree::leaf(Pane{});
    const auto split_kind = (orient == "vertical") ? PaneTree::Kind::SplitVertical : PaneTree::Kind::SplitHorizontal;
    return PaneTree::split(split_kind, std::move(first), std::move(second), ratio);
}

} // namespace remin::core
