#include "clustering/spatial_clustering.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace censor {

namespace {

// Union-Find with path compression and union-by-rank.
struct UnionFind {
    std::vector<int> parent;
    std::vector<int> rank_;

    explicit UnionFind(int n) : parent(n), rank_(n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank_[a] < rank_[b]) std::swap(a, b);
        parent[b] = a;
        if (rank_[a] == rank_[b]) ++rank_[a];
    }
};

// Minimum gap between two axis-aligned bounding boxes.
// Returns 0 when boxes overlap or touch.
float bbox_gap(const float* a, const float* b) {
    float dx = std::max(0.0f, std::max(a[0] - b[2], b[0] - a[2]));
    float dy = std::max(0.0f, std::max(a[1] - b[3], b[1] - a[3]));
    return std::sqrt(dx * dx + dy * dy);
}

// Assign element to a weight band index.
// Returns 0 for all elements when bands are uncalibrated (band_count == 0).
int weight_band(float stroke_width, const StrokeWeightBands& bands) {
    if (bands.band_count <= 0) return 0;
    for (int i = 0; i < bands.band_count; ++i) {
        if (stroke_width <= bands.thresholds[i]) return i;
    }
    return bands.band_count - 1;
}

// Expand bbox a to include bbox b (in-place).
void expand_bbox(float* a, const float* b) {
    a[0] = std::min(a[0], b[0]);
    a[1] = std::min(a[1], b[1]);
    a[2] = std::max(a[2], b[2]);
    a[3] = std::max(a[3], b[3]);
}

// Overlap area of two bounding boxes; 0 when they do not overlap.
float bbox_overlap_area(const float* a, const float* b) {
    float ox = std::min(a[2], b[2]) - std::max(a[0], b[0]);
    float oy = std::min(a[3], b[3]) - std::max(a[1], b[1]);
    return (ox > 0.0f && oy > 0.0f) ? ox * oy : 0.0f;
}

} // anonymous namespace

std::vector<Cluster> cluster_page(const std::vector<ElementData>& elements,
                                  const StrokeWeightBands& bands)
{
    const int n = static_cast<int>(elements.size());
    if (n == 0) return {};

    /* DoS guard, see ce-590: the all-pairs union-find loop below is O(n^2) over
     * stroked elements. A crafted page with more than MAX_CLUSTER_ELEMENTS paths
     * would stall the BackgroundWorker for minutes; return empty so the overlay
     * stays blank rather than hanging. */
    if (n > MAX_CLUSTER_ELEMENTS) return {};

    // --- Phase 1: union-find over stroked elements (proximity + weight band) ---

    UnionFind uf(n);
    std::vector<int> stroke_band(n, -1);

    for (int i = 0; i < n; ++i) {
        if (!elements[i].is_stroked) continue;
        stroke_band[i] = weight_band(elements[i].stroke_width, bands);
    }

    for (int i = 0; i < n; ++i) {
        if (stroke_band[i] < 0) continue;
        for (int j = i + 1; j < n; ++j) {
            if (stroke_band[j] < 0) continue;
            if (stroke_band[i] != stroke_band[j]) continue;
            if (bbox_gap(elements[i].bounds, elements[j].bounds)
                    <= CLUSTER_PROXIMITY_TOLERANCE) {
                uf.unite(i, j);
            }
        }
    }

    // --- Phase 2: materialise stroke clusters ---

    std::unordered_map<int, int> root_to_cid;
    std::vector<Cluster> clusters;

    for (int i = 0; i < n; ++i) {
        if (stroke_band[i] < 0) continue;
        int root = uf.find(i);
        auto it = root_to_cid.find(root);
        if (it == root_to_cid.end()) {
            int cid = static_cast<int>(clusters.size());
            root_to_cid[root] = cid;
            Cluster c;
            c.cluster_id = cid;
            std::copy(elements[i].bounds, elements[i].bounds + 4, c.bounds);
            c.element_indices.push_back(i);
            clusters.push_back(std::move(c));
        } else {
            int cid = it->second;
            clusters[cid].element_indices.push_back(i);
            expand_bbox(clusters[cid].bounds, elements[i].bounds);
        }
    }

    // --- Phase 3: fill containment ---
    // Pure fill elements are absorbed into the stroke cluster with the greatest
    // bbox overlap, or form singleton clusters if no overlap exists.

    for (int i = 0; i < n; ++i) {
        if (!elements[i].is_filled || elements[i].is_stroked) continue;

        float best = 0.0f;
        int   best_cid = -1;
        for (int cid = 0; cid < static_cast<int>(clusters.size()); ++cid) {
            float area = bbox_overlap_area(elements[i].bounds, clusters[cid].bounds);
            if (area > best) { best = area; best_cid = cid; }
        }

        if (best_cid >= 0) {
            clusters[best_cid].element_indices.push_back(i);
            expand_bbox(clusters[best_cid].bounds, elements[i].bounds);
        } else {
            Cluster c;
            c.cluster_id = static_cast<int>(clusters.size());
            std::copy(elements[i].bounds, elements[i].bounds + 4, c.bounds);
            c.element_indices.push_back(i);
            clusters.push_back(std::move(c));
        }
    }

    return clusters;
}

// ---------------------------------------------------------------------------
// ClusterCache
// ---------------------------------------------------------------------------

const std::vector<Cluster>& ClusterCache::get_or_compute(
    int page_id,
    const std::vector<ElementData>& elements,
    const StrokeWeightBands& bands)
{
    auto [it, inserted] = cache_.emplace(page_id, std::vector<Cluster>{});
    if (inserted) {
        it->second = cluster_page(elements, bands);
    }
    return it->second;
}

void ClusterCache::invalidate(int page_id)     { cache_.erase(page_id); }
void ClusterCache::invalidate_all()            { cache_.clear(); }

} // namespace censor
