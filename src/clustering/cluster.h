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

// Mark internal C++ symbols so they appear in the shared-library dynamic
// symbol table and remain reachable from test executables.
#ifdef _MSC_VER
#  define CENSOR_API __declspec(dllexport)
#else
#  define CENSOR_API __attribute__((visibility("default")))
#endif
