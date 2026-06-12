/*
 * math_utils.h — Shared math utilities for Censor modules.
 *
 * Header-only. Include from any Censor source file that needs these helpers.
 * No .cpp counterpart — all definitions are inline.
 */

#ifndef CENSOR_MATH_UTILS_H
#define CENSOR_MATH_UTILS_H

#include "censor_types.h"

#include <cmath>

namespace censor {

/*
 * cosine_similarity — canonical implementation (ce-d8i, ce-zgy).
 *
 * Returns dot(a,b) / (|a| * |b|), clamped to [0, 1].
 *
 * Guards:
 *   - Non-finite (NaN or Inf) accumulator sums are treated as zero vectors
 *     so that a crafted PDF with NaN coordinates cannot produce a NaN result
 *     that violates strict weak ordering in std::partial_sort (ce-zgy UB).
 *   - Denominator < 1e-9f (epsilon guard) -> return 0.0f so that nearly-zero
 *     vectors are handled identically to exact-zero vectors.
 *   - Result is clamped to [0, 1] to absorb any floating-point overshoot.
 *
 * Feature vectors are non-negative so the mathematical result is in [0, 1];
 * the clamp is a safety belt, not a semantic change.
 */
inline float cosine_similarity(const FeatureVector& a, const FeatureVector& b)
{
    float dot = 0.0f;
    float na  = 0.0f;
    float nb  = 0.0f;

    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        dot += a.dims[i] * b.dims[i];
        na  += a.dims[i] * a.dims[i];
        nb  += b.dims[i] * b.dims[i];
    }

    /* ce-zgy: NaN/Inf from a crafted PDF must not reach the comparator. */
    if (!std::isfinite(dot) || !std::isfinite(na) || !std::isfinite(nb)) {
        return 0.0f;
    }

    float denom = std::sqrt(na) * std::sqrt(nb);
    if (denom < 1e-9f) return 0.0f;

    float sim = dot / denom;
    return sim < 0.0f ? 0.0f : (sim > 1.0f ? 1.0f : sim);
}

} /* namespace censor */

#endif /* CENSOR_MATH_UTILS_H */
