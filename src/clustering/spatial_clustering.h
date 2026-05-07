#pragma once
// Spatial clustering API — groups page elements into Cluster objects.
// See SPEC-censor-core.md §1 for grouping rules.

#include "clustering/cluster.h"
#include <unordered_map>
#include <vector>

namespace censor {

// Group all elements on a page into spatial clusters.
//
// Grouping rules (SPEC-censor-core §1):
//   1. Proximity  — stroked elements with bbox gap <= CLUSTER_PROXIMITY_TOLERANCE
//   2. Weight band — elements must be in the same calibrated band (or all-same
//      if bands.band_count == 0, i.e. not yet calibrated)
//   3. Fill containment — fill-only elements absorbed into the stroke cluster
//      whose bbox overlaps most; isolated fills form singleton clusters
//
// Runs in O(n²) — acceptable for AEC page element counts (100–10 000).
// cluster_id equals the index of each Cluster in the returned vector.
CENSOR_API std::vector<Cluster> cluster_page(
    const std::vector<ElementData>& elements,
    const StrokeWeightBands& bands = StrokeWeightBands{});

// Lazy per-page cluster cache. Computes on first access; stable per session.
class CENSOR_API ClusterCache {
public:
    // Returns cached clusters for page_id, computing them if not yet cached.
    const std::vector<Cluster>& get_or_compute(
        int page_id,
        const std::vector<ElementData>& elements,
        const StrokeWeightBands& bands = StrokeWeightBands{});

    void invalidate(int page_id);
    void invalidate_all();

private:
    std::unordered_map<int, std::vector<Cluster>> cache_;
};

} // namespace censor
