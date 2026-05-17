/*
 * rejection_toast.h — Rejection feedback toast (SPEC-censor-ui.md §5).
 *
 * After the user presses N or N+label on a confusable popover, a brief
 * "Rejected" or "Reclassified → Duct" message is shown for 2 seconds.
 *
 * Censor owns the state; Rapida renders it using current().
 * Thread-safe: all methods are safe from any thread.
 */

#ifndef CENSOR_REJECTION_TOAST_H
#define CENSOR_REJECTION_TOAST_H

#include <chrono>
#include <mutex>
#include <string>

namespace censor {

struct ToastSnapshot {
    bool        visible = false;
    std::string message;
};

/* ---------------------------------------------------------------------------
 * RejectionToast — ephemeral feedback message after reject/reclassify.
 *
 * Rapida calls current() each frame; the toast self-expires after
 * DISPLAY_DURATION_MS without Rapida needing to call dismiss().
 * -------------------------------------------------------------------------*/
class __attribute__((visibility("default"))) RejectionToast {
public:
    static constexpr int DISPLAY_DURATION_MS = 2000;

    /* Show a message, replacing any existing one and restarting the timer. */
    void set(const std::string& message);

    /* Dismiss immediately. */
    void dismiss();

    /* Thread-safe snapshot. Returns visible=false when empty or expired. */
    ToastSnapshot current() const;

private:
    mutable std::mutex                        mu_;
    std::string                               message_;
    std::chrono::steady_clock::time_point     shown_at_;
    bool                                      has_message_ = false;
};

} /* namespace censor */

#endif /* CENSOR_REJECTION_TOAST_H */
