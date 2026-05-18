/*
 * rejection_toast.cpp — RejectionToast implementation.
 */

#include "overlay/rejection_toast.h"

namespace censor {

void RejectionToast::set(const std::string& message)
{
    std::lock_guard<std::mutex> lk(mu_);
    message_     = message;
    shown_at_    = std::chrono::steady_clock::now();
    has_message_ = true;
}

void RejectionToast::dismiss()
{
    std::lock_guard<std::mutex> lk(mu_);
    has_message_ = false;
    message_.clear();
}

ToastSnapshot RejectionToast::current() const
{
    std::lock_guard<std::mutex> lk(mu_);
    if (!has_message_)
        return {};
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - shown_at_);
    if (elapsed.count() >= DISPLAY_DURATION_MS)
        return {};
    return {true, message_};
}

} /* namespace censor */
