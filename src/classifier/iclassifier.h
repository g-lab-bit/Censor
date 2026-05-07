/*
 * iclassifier.h — Swappable classifier interface (SPEC-censor-core.md §6).
 *
 * All classifier implementations (k-NN v1, neural net v2, …) satisfy this
 * interface. Callers hold IClassifier* and never depend on the concrete type.
 */

#ifndef CENSOR_ICLASSIFIER_H
#define CENSOR_ICLASSIFIER_H

#include "censor_types.h"

#include <map>
#include <string>

namespace censor {

class IClassifier {
public:
    virtual ~IClassifier() = default;

    /* Predict label for a feature vector. Returns empty label when no class
     * meets the activation threshold (< SEEDING_THRESHOLD_PER_CLASS examples). */
    virtual ClassifyResult predict(const FeatureVector& features) const = 0;

    /* Store a labeled example. is_negative=true marks a high-value negative
     * (confusable cluster that was confirmed NOT this class). */
    virtual void add_example(const FeatureVector& features,
                             const std::string&   label,
                             bool                 is_negative) = 0;

    /* Count of stored examples per label (includes negatives). */
    virtual std::map<std::string, int> label_counts() const = 0;
};

} /* namespace censor */

#endif /* CENSOR_ICLASSIFIER_H */
