#include "completeness.h"

#include <algorithm>

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

/* --------------------------------------------------------------------------
 * CompletenessHUD
 * -------------------------------------------------------------------------*/

void CompletenessHUD::set_visible(bool v)
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = v;
}

bool CompletenessHUD::is_visible() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return visible_;
}

void CompletenessHUD::toggle_visible()
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = !visible_;
}

void CompletenessHUD::record_label_event()
{
    std::lock_guard<std::mutex> lk(mu_);
    label_times_.push_back(Clock::now());
    /* ce-ivd: prune entries older than 60 s to bound vector growth.
     * labeling_rate() uses the same 60 s window, so stale entries are dead weight. */
    using namespace std::chrono;
    auto cutoff = Clock::now() - seconds(60);
    auto it = std::lower_bound(label_times_.begin(), label_times_.end(), cutoff);
    label_times_.erase(label_times_.begin(), it);
}

float CompletenessHUD::labeling_rate() const
{
    /* mu_ must be held by caller. */
    using namespace std::chrono;
    auto now    = Clock::now();
    auto cutoff = now - seconds(60);

    auto it = std::lower_bound(label_times_.begin(), label_times_.end(), cutoff);
    int  n  = static_cast<int>(std::distance(it, label_times_.end()));

    if (n < 2) return 0.0f;
    return static_cast<float>(n) / 60.0f;
}

CompletenessHUDData CompletenessHUD::snapshot(
    const std::vector<Cluster>&     clusters,
    const std::vector<std::string>& user_labels,
    const IClassifier&              classifier) const
{
    std::lock_guard<std::mutex> lk(mu_);

    CompletenessHUDData d;
    d.visible        = visible_;
    d.total_clusters = static_cast<int>(clusters.size());

    if (clusters.empty()) return d;

    for (size_t i = 0; i < clusters.size(); ++i) {
        bool labeled = (i < user_labels.size() && !user_labels[i].empty());
        if (labeled) {
            ++d.labeled_count;
            continue;
        }
        if (classifier.predict(clusters[i].features).above_threshold) {
            ++d.auto_classified;
        }
    }

    int understood   = d.labeled_count + d.auto_classified;
    d.remaining      = d.total_clusters - understood;
    d.pct_understood = static_cast<float>(understood) /
                       static_cast<float>(d.total_clusters);

    float rate = labeling_rate();
    if (rate > 0.0f && d.remaining > 0) {
        d.est_seconds = static_cast<float>(d.remaining) / rate;
    }

    return d;
}

} /* namespace censor */
