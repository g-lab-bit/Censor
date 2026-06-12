#include "knn_classifier.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace censor {

/* ---------------------------------------------------------------------------
 * cosine_similarity
 *
 * dot(a, b) / (|a| * |b|).  Returns 0 if either vector is all-zero to avoid
 * division by zero.  Result is in [-1, 1] mathematically; feature vectors are
 * non-negative so the result is effectively in [0, 1].
 * -------------------------------------------------------------------------*/
float KNNClassifier::cosine_similarity(const FeatureVector& a,
                                       const FeatureVector& b)
{
    float dot  = 0.0f;
    float na2  = 0.0f;
    float nb2  = 0.0f;

    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        dot += a.dims[i] * b.dims[i];
        na2 += a.dims[i] * a.dims[i];
        nb2 += b.dims[i] * b.dims[i];
    }

    if (na2 == 0.0f || nb2 == 0.0f) return 0.0f;
    return dot / (std::sqrt(na2) * std::sqrt(nb2));
}

/* ---------------------------------------------------------------------------
 * add_example
 * -------------------------------------------------------------------------*/
void KNNClassifier::add_example(const FeatureVector& features,
                                const std::string&   label,
                                bool                 is_negative)
{
    std::unique_lock<std::shared_mutex> lk(mu_);  /* Censor-owf */
    examples_.push_back({features, label, is_negative});
}

/* ---------------------------------------------------------------------------
 * label_counts
 *
 * Counts ALL stored examples (positive and negative) keyed by label.
 * -------------------------------------------------------------------------*/
std::map<std::string, int> KNNClassifier::label_counts() const
{
    std::shared_lock<std::shared_mutex> lk(mu_);  /* Censor-owf */
    std::map<std::string, int> counts;
    for (const auto& ex : examples_) {
        counts[ex.label]++;
    }
    return counts;
}

/* ---------------------------------------------------------------------------
 * predict
 *
 * 1. Compute cosine similarity to every stored positive example.
 * 2. Pick top-KNN_K by similarity.
 * 3. Weighted vote: weight = similarity (not 1/distance — cosine is already a
 *    similarity score, so higher = closer).
 * 4. Winning label is the class whose weights sum highest.
 * 5. Confidence = sum_of_winner_weights / sum_of_all_weights.
 * 6. Return empty result if winning class has < SEEDING_THRESHOLD_PER_CLASS
 *    total positive examples across the entire store (not just the k neighbors).
 * -------------------------------------------------------------------------*/
ClassifyResult KNNClassifier::predict(const FeatureVector& features) const
{
    std::shared_lock<std::shared_mutex> lk(mu_);  /* Censor-owf */
    ClassifyResult result;

    /* Collect (similarity, label) for positive examples only. */
    struct Candidate {
        float       sim;
        std::string label;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(examples_.size());

    for (const auto& ex : examples_) {
        if (ex.is_negative) continue;
        float sim = cosine_similarity(features, ex.fv);
        candidates.push_back({sim, ex.label});
    }

    if (candidates.empty()) return result;  /* no positive examples at all */

    /* Partial sort: bring the top-k to the front. */
    int k = std::min(KNN_K, static_cast<int>(candidates.size()));
    std::partial_sort(candidates.begin(),
                      candidates.begin() + k,
                      candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                          return a.sim > b.sim;
                      });

    /* Weighted voting over top-k. */
    std::map<std::string, float> weight_sum;
    float total_weight = 0.0f;

    for (int i = 0; i < k; ++i) {
        float w = candidates[static_cast<size_t>(i)].sim;
        weight_sum[candidates[static_cast<size_t>(i)].label] += w;
        total_weight += w;
    }

    if (total_weight == 0.0f) return result;  /* all similarities were zero */

    /* Find winning label. */
    auto winner_it = std::max_element(weight_sum.begin(), weight_sum.end(),
                                      [](const auto& a, const auto& b) {
                                          return a.second < b.second;
                                      });
    const std::string& winner = winner_it->first;

    /* Per-class activation guard: winning class must have enough examples. */
    int positive_count = 0;
    for (const auto& ex : examples_) {
        if (!ex.is_negative && ex.label == winner) ++positive_count;
    }
    if (positive_count < SEEDING_THRESHOLD_PER_CLASS) return result;

    result.label          = winner;
    result.confidence     = winner_it->second / total_weight;
    result.above_threshold = (result.confidence >= CLASSIFIER_CONFIDENCE_THRESHOLD);
    return result;
}

} /* namespace censor */
