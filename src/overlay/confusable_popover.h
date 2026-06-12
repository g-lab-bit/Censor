/*
 * confusable_popover.h — Confusable negative popover state (SPEC-censor-ui.md §7).
 *
 * After labeling a cluster, Censor surfaces 3-5 most similar unlabeled clusters
 * in a "Also a Wall?" popover. The user responds with Y / N / N+label.
 *
 * Censor owns the state; Rapida renders it using snapshot().
 * Thread-safe: all methods are safe from any thread.
 */

#pragma once



#include "censor_visibility.h"
#include "censor_types.h"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace censor {

/* One confusable cluster queued for display in the popover. */
struct PopoverItem {
    std::string   suggestion_id;    /* UUID from ActiveLearner::store_suggestions */
    std::string   predicted_label;  /* e.g. "Wall" → popover asks "Also a Wall?" */
    FeatureVector features;         /* caller passes to ActiveLearner confirm/reject */
    float         bounds[4] = {};   /* x0,y0,x1,y1 — for viewport navigation */
    float         confidence = 0.0f;
    float         similarity = 0.0f;
};

/* Snapshot passed to Rapida for rendering. */
struct PopoverSnapshot {
    bool        visible   = false; /* true when there is an item to show */
    PopoverItem item;              /* valid when visible */
    int         remaining = 0;    /* items left after this one */
};

/* Result returned by Y/N/N+label actions. */
struct PopoverResult {
    PopoverItem item;              /* the item that was acted on */
    std::string corrected_label;   /* non-empty only for reclassify() */
    bool        acted = false;     /* false when popover was empty */
};

/* ---------------------------------------------------------------------------
 * ConfusablePopover — queued "Also a X?" items surfaced after labeling.
 *
 * Workflow:
 *   1. Caller fills the queue via load() after ActiveLearner::store_suggestions.
 *   2. Rapida renders snapshot() — one "Also a X?" prompt at a time.
 *   3. User presses Y → confirm(); N → reject(); N+label → reclassify(label).
 *      Each action pops the front item and returns it for routing to ActiveLearner.
 *   4. dismiss() clears all remaining items without acting on them.
 * -------------------------------------------------------------------------*/
class CENSOR_API ConfusablePopover {
public:
    /* Load a new batch, replacing any existing queue. */
    void load(std::vector<PopoverItem> items);

    /* True when at least one item is pending. */
    bool has_pending() const;

    /* --- User actions (each pops the front item) --- */

    /* Y pressed — confirm: predicted label is correct. */
    PopoverResult confirm();

    /* N pressed — reject: predicted label is wrong (no corrected label). */
    PopoverResult reject();

    /* N+label pressed — reject + reclassify to corrected label. */
    PopoverResult reclassify(const std::string& label);

    /* Dismiss all remaining items without acting on them. */
    void dismiss();

    /* Thread-safe snapshot for Rapida. */
    PopoverSnapshot snapshot() const;

private:
    mutable std::mutex      mu_;
    std::deque<PopoverItem> queue_;
};

} /* namespace censor */

