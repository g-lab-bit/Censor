#include "discovery_queue.h"
#include "math_utils.h"  /* canonical cosine_similarity (ce-d8i) */

#include <algorithm>
#include <cmath>

namespace censor {

/* cosine_sim: alias to the canonical free function in math_utils.h (ce-d8i). */
static inline float cosine_sim(const FeatureVector& a, const FeatureVector& b)
{
    return censor::cosine_similarity(a, b);
}

std::vector<DiscoveryEntry> compute_discovery_queue(
    const std::vector<Cluster>&       clusters,
    const std::vector<std::string>&   user_labels,
    const std::vector<FeatureVector>& labeled_features,
    const IClassifier&                classifier,
    int                               page,
    int                               top_n)
{
    if (labeled_features.empty()) return {};

    std::vector<DiscoveryEntry> candidates;
    candidates.reserve(clusters.size());

    for (size_t i = 0; i < clusters.size(); ++i) {
        bool labeled = (i < user_labels.size() && !user_labels[i].empty());
        if (labeled) continue;
        if (classifier.predict(clusters[i].features).above_threshold) continue;

        /* max_distance = 1 − max(cosine_sim to any labeled example).
         * High value → cluster is distant from every known example → most novel. */
        float max_sim = 0.0f;
        for (const auto& lf : labeled_features) {
            float s = cosine_sim(clusters[i].features, lf);
            if (s > max_sim) max_sim = s;
        }

        DiscoveryEntry e;
        e.cluster_id   = clusters[i].cluster_id;
        e.page         = page;
        e.bounds[0]    = clusters[i].bounds[0];
        e.bounds[1]    = clusters[i].bounds[1];
        e.bounds[2]    = clusters[i].bounds[2];
        e.bounds[3]    = clusters[i].bounds[3];
        e.max_distance = 1.0f - max_sim;
        candidates.push_back(e);
    }

    /* Furthest from known first. */
    std::sort(candidates.begin(), candidates.end(),
              [](const DiscoveryEntry& a, const DiscoveryEntry& b) {
                  return a.max_distance > b.max_distance;
              });

    if (static_cast<int>(candidates.size()) > top_n)
        candidates.resize(static_cast<size_t>(top_n));

    return candidates;
}

/* --------------------------------------------------------------------------
 * DiscoveryQueuePanel
 * -------------------------------------------------------------------------*/

void DiscoveryQueuePanel::set_visible(bool v)
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = v;
}

bool DiscoveryQueuePanel::is_visible() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return visible_;
}

void DiscoveryQueuePanel::toggle_visible()
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = !visible_;
}

void DiscoveryQueuePanel::update(
    const std::vector<Cluster>&       clusters,
    const std::vector<std::string>&   user_labels,
    const std::vector<FeatureVector>& labeled_features,
    const IClassifier&                classifier,
    int                               page,
    int                               top_n)
{
    auto q = compute_discovery_queue(clusters, user_labels, labeled_features,
                                     classifier, page, top_n);
    std::lock_guard<std::mutex> lk(mu_);
    entries_ = std::move(q);
}

std::vector<DiscoveryEntry> DiscoveryQueuePanel::entries() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return entries_;
}

} /* namespace censor */
