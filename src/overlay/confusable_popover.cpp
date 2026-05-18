/*
 * confusable_popover.cpp — ConfusablePopover implementation.
 */

#include "overlay/confusable_popover.h"

namespace censor {

void ConfusablePopover::load(std::vector<PopoverItem> items)
{
    std::lock_guard<std::mutex> lk(mu_);
    queue_.assign(items.begin(), items.end());
}

bool ConfusablePopover::has_pending() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return !queue_.empty();
}

PopoverResult ConfusablePopover::confirm()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (queue_.empty()) return {};
    PopoverResult r;
    r.acted = true;
    r.item  = queue_.front();
    queue_.pop_front();
    return r;
}

PopoverResult ConfusablePopover::reject()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (queue_.empty()) return {};
    PopoverResult r;
    r.acted = true;
    r.item  = queue_.front();
    queue_.pop_front();
    return r;
}

PopoverResult ConfusablePopover::reclassify(const std::string& label)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (queue_.empty()) return {};
    PopoverResult r;
    r.acted           = true;
    r.item            = queue_.front();
    r.corrected_label = label;
    queue_.pop_front();
    return r;
}

void ConfusablePopover::dismiss()
{
    std::lock_guard<std::mutex> lk(mu_);
    queue_.clear();
}

PopoverSnapshot ConfusablePopover::snapshot() const
{
    std::lock_guard<std::mutex> lk(mu_);
    PopoverSnapshot snap{};
    if (queue_.empty())
        return snap;
    snap.visible   = true;
    snap.item      = queue_.front();
    snap.remaining = static_cast<int>(queue_.size()) - 1;
    return snap;
}

} /* namespace censor */
