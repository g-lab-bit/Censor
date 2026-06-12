/*
 * active_learning.h — Confusable negative selection + suggestion storage.
 *
 * SPEC-censor-core.md §7 active learning loop:
 *   1. User labels cluster → add_labeled_example()
 *   2. find_confusables() → top-K similar low-confidence unlabeled clusters
 *   3. store_suggestions() → persisted to pending_suggestions table
 *   4. confirm() / reject() → update DB + feed classifier
 */

#pragma once



#include "censor_visibility.h"
#include "censor_types.h"
#include "classifier/iclassifier.h"
#include "storage/censor_db.h"

#include <string>
#include <utility>
#include <vector>

namespace censor {

/* SPEC-censor-core §8 — also defined in knn_classifier.h; kept local to avoid
 * pulling in the KNNClassifier header from this interface. */
static constexpr int CONFUSABLE_NEGATIVE_COUNT = 5;

/* One confusable candidate, before or after storage. */
struct Suggestion {
    std::string   suggestion_id;   /* UUID — empty until store_suggestions() */
    std::string   cluster_ref;     /* file_hash:page:cluster_id */
    FeatureVector features;
    std::string   predicted_label;
    float         similarity  = 0.0f; /* cosine similarity to labeled cluster */
    float         confidence  = 0.0f; /* classifier confidence at query time */
};

class CENSOR_API ActiveLearner {
public:
    ActiveLearner(CensorDb& db, IClassifier& classifier);

    /* Step 1 — add newly labeled example to classifier. */
    void add_labeled_example(const FeatureVector& features,
                              const std::string&   label,
                              bool                 is_negative = false);

    /* Step 2 — find top-K confusable unlabeled clusters sorted by cosine
     * similarity (descending). High-confidence predictions are excluded. */
    std::vector<Suggestion> find_confusables(
        const FeatureVector& labeled_fv,
        const std::vector<std::pair<std::string, FeatureVector>>& unlabeled
    ) const;

    /* Step 3 — persist suggestions; fills suggestion_id on each entry. */
    void store_suggestions(std::vector<Suggestion>& suggestions);

    /* Step 4a — confirm: predicted label correct. Adds positive example. */
    bool confirm(const std::string& suggestion_id,
                 const FeatureVector& features,
                 const std::string& predicted_label);

    /* Step 4b — reject: predicted label wrong.
     *   corrected_label="" → adds negative for predicted_label.
     *   corrected_label!="" → adds negative for predicted + positive for corrected. */
    bool reject(const std::string& suggestion_id,
                const FeatureVector& features,
                const std::string& predicted_label,
                const std::string& corrected_label = "");

private:
    CensorDb&    db_;
    IClassifier& classifier_;
};

} /* namespace censor */

