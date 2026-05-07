/*
 * active_learning.cpp — ActiveLearner implementation.
 */

#include "classifier/active_learning.h"
#include "classifier/knn_classifier.h" /* for CLASSIFIER_CONFIDENCE_THRESHOLD */

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>

namespace censor {

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

static std::string generate_uuid()
{
    static std::mt19937                      rng{std::random_device{}()};
    static std::uniform_int_distribution<uint32_t> dist;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dist(rng)
       << '-' << std::setw(4) << (dist(rng) & 0xffffu)
       << '-' << std::setw(4) << ((dist(rng) & 0x0fffu) | 0x4000u) /* v4 */
       << '-' << std::setw(4) << ((dist(rng) & 0x3fffu) | 0x8000u) /* variant 1 */
       << '-' << std::setw(8) << dist(rng)
       << std::setw(4)        << (dist(rng) & 0xffffu);
    return ss.str();
}

static float cosine_similarity(const FeatureVector& a, const FeatureVector& b)
{
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        dot += a.dims[i] * b.dims[i];
        na  += a.dims[i] * a.dims[i];
        nb  += b.dims[i] * b.dims[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    if (denom < 1e-9f) return 0.0f;
    float sim = dot / denom;
    return sim < 0.0f ? 0.0f : (sim > 1.0f ? 1.0f : sim);
}

/* ---------------------------------------------------------------------------
 * ActiveLearner
 * -------------------------------------------------------------------------*/

ActiveLearner::ActiveLearner(CensorDb& db, IClassifier& classifier)
    : db_(db), classifier_(classifier) {}

void ActiveLearner::add_labeled_example(const FeatureVector& features,
                                          const std::string&   label,
                                          bool                 is_negative)
{
    classifier_.add_example(features, label, is_negative);
}

std::vector<Suggestion> ActiveLearner::find_confusables(
    const FeatureVector& labeled_fv,
    const std::vector<std::pair<std::string, FeatureVector>>& unlabeled
) const
{
    std::vector<Suggestion> candidates;
    candidates.reserve(unlabeled.size());

    for (const auto& [ref, fv] : unlabeled) {
        ClassifyResult pred = classifier_.predict(fv);

        /* Skip clusters the classifier is already confident about. */
        if (pred.above_threshold && pred.confidence >= CLASSIFIER_CONFIDENCE_THRESHOLD)
            continue;

        Suggestion s;
        s.cluster_ref     = ref;
        s.features        = fv;
        s.predicted_label = pred.label;
        s.similarity      = cosine_similarity(labeled_fv, fv);
        s.confidence      = pred.confidence;
        candidates.push_back(std::move(s));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Suggestion& a, const Suggestion& b) {
                  return a.similarity > b.similarity;
              });

    if (static_cast<int>(candidates.size()) > CONFUSABLE_NEGATIVE_COUNT)
        candidates.resize(CONFUSABLE_NEGATIVE_COUNT);

    return candidates;
}

void ActiveLearner::store_suggestions(std::vector<Suggestion>& suggestions)
{
    for (auto& s : suggestions) {
        PendingSuggestion ps;
        ps.id              = generate_uuid();
        ps.cluster_ref     = s.cluster_ref;
        ps.predicted_label = s.predicted_label;
        ps.confidence      = s.confidence;
        ps.state           = SuggestionState::Pending;

        if (db_.insert_suggestion(ps))
            s.suggestion_id = ps.id;
    }
}

bool ActiveLearner::confirm(const std::string& suggestion_id,
                              const FeatureVector& features,
                              const std::string& predicted_label)
{
    if (!db_.confirm_suggestion(suggestion_id))
        return false;
    classifier_.add_example(features, predicted_label, false);
    return true;
}

bool ActiveLearner::reject(const std::string& suggestion_id,
                             const FeatureVector& features,
                             const std::string& predicted_label,
                             const std::string& corrected_label)
{
    if (!db_.reject_suggestion(suggestion_id, corrected_label))
        return false;
    classifier_.add_example(features, predicted_label, true);
    if (!corrected_label.empty())
        classifier_.add_example(features, corrected_label, false);
    return true;
}

} /* namespace censor */
