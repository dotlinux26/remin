#include "core/pane/pane.hpp"

namespace remin::core {

void PaneTree::collect_panes(std::vector<Pane*>& out) {
    if (kind_ == Kind::Pane) {
        if (pane_) out.push_back(&*pane_);
        return;
    }
    first_->collect_panes(out);
    second_->collect_panes(out);
}

void PaneTree::collect_panes(std::vector<const Pane*>& out) const {
    if (kind_ == Kind::Pane) {
        if (pane_) out.push_back(&*pane_);
        return;
    }
    first_->collect_panes(out);
    second_->collect_panes(out);
}

} // namespace remin::core
