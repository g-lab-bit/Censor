/*
 * completeness.h — Completeness metric (SPEC-censor-core.md §10 Phase 6).
 *
 * A cluster is 'understood' if the user has labeled it OR the classifier
 * predicts it above CLASSIFIER_CONFIDENCE_THRESHOLD.
 */

#ifndef CENSOR_COMPLETENESS_H
#define CENSOR_COMPLETENESS_H

#include "censor_types.h"
#include "classifier/iclassifier.h"

#include <string>
#include <vector>

namespace censor {

/* Returns fraction of clusters that are understood (0.0–1.0).
 * user_labels[i] is the user-applied label for clusters[i]; empty string = unlabeled.
 * Returns 0.0 when clusters is empty. */
__attribute__((visibility("default")))
float compute_completeness(
    const std::vector<Cluster>&     clusters,
    const std::vector<std::string>& user_labels,
    const IClassifier&              classifier);

} /* namespace censor */

#endif /* CENSOR_COMPLETENESS_H */
