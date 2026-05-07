#include "discovery_queue.h"

#include <algorithm>
#include <cmath>

namespace censor {

/* Cosine similarity in [0, 1] for non-negative feature vectors. */
static float cosine_sim(const FeatureVector& a, const FeatureVector& b)
{
    float dot = 0.0f, na2 = 0.0f, nb2 = 0.0f;
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        dot += a.dims[i] * b.dims[i];
        na2 += a.dims[i] * a.dims[i];
        nb2 += b.dims[i] * b.dims[i];
    }
    if (na2 == 0.0f || nb2 == 0.0f) return 0.0f;
    return dot / (std::sqrt(na2) * std::sqrt(nb2));
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

} /* namespace censor */
