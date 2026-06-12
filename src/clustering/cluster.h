#pragma once
// Clustering module entry point.
// Cluster struct is defined in censor_types.h; this header adds the
// proximity constant and the internal-API visibility macro.

#include "censor_types.h"

// Placeholder proximity threshold (calibrated during seeding run).
// Two elements with bbox gap <= this value are candidates for grouping.
namespace censor {
inline constexpr float CLUSTER_PROXIMITY_TOLERANCE = 10.0f;
}

// CENSOR_API (internal-symbol visibility) moved to its own header so every
// module can use it without depending on clustering (ce-hcy).
#include "censor_visibility.h"
