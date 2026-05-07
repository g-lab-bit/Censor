#pragma once

#include "clustering/cluster.h"  /* CENSOR_API macro */
#include "censor_types.h"

#include <vector>

namespace censor {

/* ---------------------------------------------------------------------------
 * Normalization scale factors (SPEC-censor-core.md §4).
 *
 * Placeholder values — calibrated against real AEC drawings during the
 * Phase 3 seeding run using the debug feature overlay.
 * -------------------------------------------------------------------------*/
static constexpr float FV_NORM_STROKE_LENGTH   = 1000.0f; /* typical total stroke run  */
static constexpr float FV_NORM_LOOP_AREA        = 50000.0f; /* typical closed-loop area  */
static constexpr float FV_NORM_PARALLEL_OFFSET  = 100.0f;   /* typical wall/duct thickness */

/* ---------------------------------------------------------------------------
 * Parallel-stroke detection parameters.
 * -------------------------------------------------------------------------*/
static constexpr float FV_PARALLEL_ANGLE_TOL   = 0.15f;  /* radians — angle tolerance */
static constexpr float FV_PARALLEL_MIN_LENGTH  = 10.0f;  /* minimum element length    */

/* ---------------------------------------------------------------------------
 * compute_features — extract all 12 dimensions from a Cluster.
 *
 * Parameters
 *   cluster      — pre-computed Cluster (bounds + element_indices)
 *   elements     — full page element array (indices in cluster reference this)
 *   weight_bands — per-drawing stroke weight calibration (§2); pass {} for
 *                  tests that don't exercise band detection
 *
 * Returns a FeatureVector with all dims filled.  Dims 6 (ADJACENT_CONNECTIVITY)
 * is always 0 here — requires grid context from the caller.
 * -------------------------------------------------------------------------*/
CENSOR_API FeatureVector compute_features(
    const Cluster&                  cluster,
    const std::vector<ElementData>& elements,
    const StrokeWeightBands&        weight_bands);

} /* namespace censor */
