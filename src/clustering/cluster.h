#pragma once
// Clustering module entry point.
// Cluster struct is defined in censor_types.h; this header adds the
// proximity constant and the internal-API visibility macro.

#include "censor_types.h"

// Placeholder proximity threshold (calibrated during seeding run).
// Two elements with bbox gap <= this value are candidates for grouping.
namespace censor {
inline constexpr float CLUSTER_PROXIMITY_TOLERANCE = 10.0f;

// DoS guard, see ce-590: a page with more stroked elements than this cap
// would trigger O(n^2) work in cluster_page's union-find loop. Pathological
// PDFs (200k degenerate paths) would stall the BackgroundWorker for minutes.
inline constexpr int MAX_CLUSTER_ELEMENTS = 20000;

// DoS guard, see ce-dfq: n_bins = ceil((max_w - min_w) / 0.1) + 1 grows
// without bound for adversarial stroke widths (e.g. {0.001, 1e9}), causing
// a ~1e10-element allocation and an uncaught std::bad_alloc that kills the
// worker thread. Cap at 1000 bins; fall back to single-band result above cap.
inline constexpr int MAX_HISTOGRAM_BINS = 1000;
}

// CENSOR_API (internal-symbol visibility) moved to its own header so every
// module can use it without depending on clustering (ce-hcy).
#include "censor_visibility.h"
