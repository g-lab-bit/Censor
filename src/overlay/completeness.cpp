#include "completeness.h"

namespace censor {

float compute_completeness(
    const std::vector<Cluster>&     clusters,
    const std::vector<std::string>& user_labels,
    const IClassifier&              classifier)
{
    if (clusters.empty()) return 0.0f;

    int understood = 0;
    for (size_t i = 0; i < clusters.size(); ++i) {
        bool labeled = (i < user_labels.size() && !user_labels[i].empty());
        if (labeled) {
            ++understood;
            continue;
        }
        if (classifier.predict(clusters[i].features).above_threshold) {
            ++understood;
        }
    }
    return static_cast<float>(understood) / static_cast<float>(clusters.size());
}

} /* namespace censor */
