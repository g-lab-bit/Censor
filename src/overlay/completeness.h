/*
 * completeness.h — Completeness metric (SPEC-censor-core.md §10 Phase 6).
 *
 * A cluster is 'understood' if the user has labeled it OR the classifier
 * predicts it above CLASSIFIER_CONFIDENCE_THRESHOLD.
 */

#ifndef CENSOR_COMPLETENESS_H
#define CENSOR_COMPLETENESS_H


#include "censor_visibility.h"
#include "censor_types.h"
#include "classifier/iclassifier.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace censor {

/* Returns fraction of clusters that are understood (0.0–1.0).
 * user_labels[i] is the user-applied label for clusters[i]; empty string = unlabeled.
 * Returns 0.0 when clusters is empty. */
CENSOR_API
float compute_completeness(
    const std::vector<Cluster>&     clusters,
    const std::vector<std::string>& user_labels,
    const IClassifier&              classifier);

/* ---------------------------------------------------------------------------
 * CompletenessHUDData — snapshot for Rapida to render the completeness HUD.
 *
 * est_seconds == -1 means insufficient labeling rate data (fewer than 2
 * label events in the last 60 seconds).
 * -------------------------------------------------------------------------*/
struct CENSOR_API CompletenessHUDData {
    int   total_clusters  = 0;
    int   labeled_count   = 0;    /* user-applied labels */
    int   auto_classified = 0;    /* above threshold, not user-labeled */
    int   remaining       = 0;    /* not understood */
    float pct_understood  = 0.0f; /* (labeled + auto) / total */
    float est_seconds     = -1.0f;/* -1 = unknown */
    bool  visible         = false;
};

/* ---------------------------------------------------------------------------
 * CompletenessHUD — HUD state with live labeling-rate tracking.
 *
 * record_label_event() must be called whenever the user applies a label.
 * snapshot() derives the current HUD data from the page state and rate.
 * Thread-safe: all methods are safe from any thread.
 * -------------------------------------------------------------------------*/
class CENSOR_API CompletenessHUD {
public:
    CompletenessHUD() = default;

    void set_visible(bool v);
    bool is_visible() const;
    void toggle_visible();

    /* Record a user label event for rate estimation. */
    void record_label_event();

    /* Compute HUD snapshot from current page state. */
    CompletenessHUDData snapshot(
        const std::vector<Cluster>&     clusters,
        const std::vector<std::string>& user_labels,
        const IClassifier&              classifier) const;

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    mutable std::mutex      mu_;
    bool                    visible_     = true;
    std::vector<TimePoint>  label_times_;

    /* Labels/second over a 60-second rolling window; 0 if fewer than 2 events. */
    float labeling_rate() const; /* called with mu_ held */
};

} /* namespace censor */

#endif /* CENSOR_COMPLETENESS_H */
