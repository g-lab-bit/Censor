/*
 * knn_classifier.h — k-NN classifier with cosine similarity (SPEC-censor-core §6).
 */

#ifndef CENSOR_KNN_CLASSIFIER_H
#define CENSOR_KNN_CLASSIFIER_H

#include "iclassifier.h"
#include "censor_types.h"

#include <map>
#include <string>
#include <vector>

namespace censor {

/* Named constants (SPEC-censor-core §8). Calibrated during seeding run. */
static constexpr int   KNN_K                      = 5;
static constexpr int   SEEDING_THRESHOLD_PER_CLASS = 10;
static constexpr float CLASSIFIER_CONFIDENCE_THRESHOLD = 0.80f;

class __attribute__((visibility("default"))) KNNClassifier final : public IClassifier {
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

    /* Returns cosine similarity in [0, 1] (clamped; 0 for zero vectors). */
    static float cosine_similarity(const FeatureVector& a, const FeatureVector& b);
};

} /* namespace censor */

#endif /* CENSOR_KNN_CLASSIFIER_H */
