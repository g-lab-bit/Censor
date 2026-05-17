/*
 * discovery_queue.h — Discovery queue (SPEC-censor-core.md §10 Phase 6 / SPEC-censor-ui.md §10).
 *
 * Returns the top-N unknown clusters sorted by maximum distance from all labeled
 * examples in feature space. "Furthest from known" clusters are the most novel
 * and therefore the most interesting to label next.
 */

#ifndef CENSOR_DISCOVERY_QUEUE_H
#define CENSOR_DISCOVERY_QUEUE_H

#include "censor_types.h"
#include "classifier/iclassifier.h"

#include <mutex>
#include <string>
#include <vector>

namespace censor {

/* Click-to-navigate entry returned by compute_discovery_queue(). */
struct DiscoveryEntry {
    int   cluster_id;
    int   page;
    float bounds[4];     /* x0, y0, x1, y1 — for viewport navigation */
    float max_distance;  /* 1 − max_cosine_similarity_to_any_labeled_example */
};

/* Returns top_n unknown clusters sorted by max_distance descending.
 *
 * A cluster is unknown when user_labels[i] is empty AND the classifier
 * prediction is not above_threshold.
 *
 * labeled_features: feature vectors of all user-labeled clusters (positive
 * examples). The caller constructs this from clusters where user_labels[i]
 * is non-empty. Returns empty when labeled_features is empty. */
__attribute__((visibility("default")))
std::vector<DiscoveryEntry> compute_discovery_queue(
    const std::vector<Cluster>&       clusters,
    const std::vector<std::string>&   user_labels,
    const std::vector<FeatureVector>& labeled_features,
    const IClassifier&                classifier,
    int                               page,
    int                               top_n = 10);

/* ---------------------------------------------------------------------------
 * DiscoveryQueuePanel — side-panel state for the discovery queue UI.
 *
 * Censor owns the data; Rapida renders from entries().
 * Call update() whenever labels change to refresh the queue.
 * Thread-safe: all methods are safe from any thread.
 * -------------------------------------------------------------------------*/
class __attribute__((visibility("default"))) DiscoveryQueuePanel {
public:
    DiscoveryQueuePanel() = default;

    void set_visible(bool v);
    bool is_visible() const;
    void toggle_visible();

    /* Recompute the queue from the current page state.
     * labeled_features must contain the feature vectors of all user-labeled
     * clusters (caller builds this from clusters where user_labels[i] != ""). */
    void update(
        const std::vector<Cluster>&       clusters,
        const std::vector<std::string>&   user_labels,
        const std::vector<FeatureVector>& labeled_features,
        const IClassifier&                classifier,
        int                               page,
        int                               top_n = 10);

    /* Thread-safe copy of current queue entries. */
    std::vector<DiscoveryEntry> entries() const;

private:
    mutable std::mutex          mu_;
    bool                        visible_ = false;
    std::vector<DiscoveryEntry> entries_;
};

} /* namespace censor */

#endif /* CENSOR_DISCOVERY_QUEUE_H */
