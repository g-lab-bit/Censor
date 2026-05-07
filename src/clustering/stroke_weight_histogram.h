#pragma once
// Stroke weight histogram analysis — per-drawing band calibration.
// See SPEC-censor-core.md §2.

#include "clustering/cluster.h"
#include <vector>

namespace censor {

// Bin width for stroke weight histogram in points.
// Placeholder value — calibrated during seeding run.
inline constexpr float STROKE_HISTOGRAM_BIN_WIDTH = 0.1f;

// Analyse stroke widths and return natural weight bands.
//
// Peak detection via histogram connected-component analysis:
//   1. Build histogram (bin width = STROKE_HISTOGRAM_BIN_WIDTH).
//   2. Find contiguous runs of non-zero bins.
//   3. Each run becomes one band; threshold = midpoint of gap to next run.
//   4. Merges down to ≤4 bands if more are found (smallest gaps first).
//
// Returns band_count=0 for empty/all-zero input.
// Returns band_count=1 if all widths fall within a single bin.
CENSOR_API StrokeWeightBands analyze_stroke_weights(
    const std::vector<float>& stroke_widths);

} // namespace censor
