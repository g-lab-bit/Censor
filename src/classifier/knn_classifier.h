/*
 * knn_classifier.h — k-NN classifier with cosine similarity (SPEC-censor-core §6).
 */

#ifndef CENSOR_KNN_CLASSIFIER_H
#define CENSOR_KNN_CLASSIFIER_H


#include "censor_visibility.h"
#include "iclassifier.h"
#include "censor_types.h"

#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

namespace censor {

/* Named constants (SPEC-censor-core §8). Calibrated during seeding run. */
static constexpr int   KNN_K                      = 5;
static constexpr int   SEEDING_THRESHOLD_PER_CLASS = 10;
static constexpr float CLASSIFIER_CONFIDENCE_THRESHOLD = 0.80f;
/* ce-f4i: cap per-label examples to bound predict() O(n) scan over long
 * seeding sessions. Oldest example for the label is evicted when the cap is
 * reached (FIFO per label, insertion order). */
static constexpr int   MAX_EXAMPLES_PER_LABEL     = 500;

class CENSOR_API KNNClassifier final : public IClassifier {
public:
    KNNClassifier() = default;

    ClassifyResult predict(const FeatureVector& features) const override;

    void add_example(const FeatureVector& features,
                     const std::string&   label,
                     bool                 is_negative) override;

    std::map<std::string, int> label_counts() const override;

private:
    struct Example {
        FeatureVector fv;
        std::string   label;
        bool          is_negative;
    };

    std::vector<Example> examples_;

    /* Censor-owf — examples_ is read by BackgroundWorker (predict) while the
     * UI thread writes via ActiveLearner (add_example). Readers take shared,
     * the writer takes unique. mutable: predict()/label_counts() are const. */
    mutable std::shared_mutex mu_;

    /* Returns cosine similarity in [0, 1] (clamped; 0 for zero vectors). */
    static float cosine_similarity(const FeatureVector& a, const FeatureVector& b);
};

} /* namespace censor */

#endif /* CENSOR_KNN_CLASSIFIER_H */
