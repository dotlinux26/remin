#pragma once

#include "core/id.hpp"

#include <chrono>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace remin::core {

// Why a running command was interrupted. V1 only ever records CtrlC when a
// literal \x03 commit was observed right after the command — every other case
// is ProcessExit (the process exited while we had a command span) or Unknown
// (we cannot tell). Never guess Ctrl+C for e.g. SIGTERM/app crash/closed pane.
struct InterruptedCommand {
    enum class Source { CtrlC, ProcessExit, Unknown };

    std::string command;
    std::int64_t timestamp_us{0};  // when the interruption was observed
    Source source{Source::Unknown};
};

// Terminal state for a pane: what scrollback/command state we can re-create
// on restore. This is a Remin model, NOT a VTE object snapshot.
struct PaneState {
    std::string cwd;
    std::string shell;
    std::uint32_t cols{0};
    std::uint32_t rows{0};
    std::vector<std::string> environment;      // V1: NOT persisted (see design §3.2)
    std::vector<std::string> command_history;  // per-pane history
    std::string scrollback;                    // captured buffer (text)
    std::optional<InterruptedCommand> interrupted_command;
};

// A leaf pane within a tab. Holds the terminal state for that pane.
struct Pane {
    PaneId id;
    PaneState state;
};

using OptionalPane = std::optional<Pane>;

// A node in the pane tree. Either a leaf Pane or a Split with two children.
class PaneTree {
public:
    enum class Kind { Pane, SplitHorizontal, SplitVertical };

    // -- Constructors --
    PaneTree() = default;

    static PaneTree leaf(Pane pane) {
        PaneTree t;
        t.kind_ = Kind::Pane;
        t.pane_ = std::move(pane);
        return t;
    }
    static PaneTree split(Kind kind, PaneTree first, PaneTree second, double ratio = 0.5) {
        PaneTree t;
        t.kind_ = kind;
        t.first_ = std::make_unique<PaneTree>(std::move(first));
        t.second_ = std::make_unique<PaneTree>(std::move(second));
        t.ratio_ = ratio;
        return t;
    }

    // Deep copy (unique_ptr children).
    PaneTree(const PaneTree& other) {
        copy_from(other);
    }
    PaneTree& operator=(const PaneTree& other) {
        if (this != &other) copy_from(other);
        return *this;
    }
    PaneTree(PaneTree&&) = default;
    PaneTree& operator=(PaneTree&&) = default;

    // -- Accessors --
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] OptionalPane& pane() { return pane_; } // only valid if Kind::Pane
    [[nodiscard]] const OptionalPane& pane() const { return pane_; }
    [[nodiscard]] PaneTree* first() { return first_.get(); }
    [[nodiscard]] const PaneTree* first() const { return first_.get(); }
    [[nodiscard]] PaneTree* second() { return second_.get(); }
    [[nodiscard]] const PaneTree* second() const { return second_.get(); }
    [[nodiscard]] double ratio() const noexcept { return ratio_; }
    void set_ratio(double ratio) noexcept { ratio_ = ratio; }

    // Depth-first collect all leaf panes.
    void collect_panes(std::vector<Pane*>& out);
    void collect_panes(std::vector<const Pane*>& out) const;

private:
    void copy_from(const PaneTree& other) {
        kind_ = other.kind_;
        pane_ = other.pane_;
        ratio_ = other.ratio_;
        first_.reset();
        second_.reset();
        if (other.first_) first_ = std::make_unique<PaneTree>(*other.first_);
        if (other.second_) second_ = std::make_unique<PaneTree>(*other.second_);
    }

    Kind kind_{Kind::Pane};
    OptionalPane pane_;
    std::unique_ptr<PaneTree> first_;
    std::unique_ptr<PaneTree> second_;
    double ratio_{0.5};
};

} // namespace remin::core
