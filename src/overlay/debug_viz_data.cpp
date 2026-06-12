#include "debug_viz_data.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace censor {

namespace {

static constexpr const char* kFeatureNames[FEATURE_DIM_COUNT] = {
    "stroke_length",
    "dominant_orientation",
    "stroke_weight_mean",
    "stroke_weight_band",
    "closed_loop_count",
    "closed_loop_area",
    "adjacent_connectivity",
    "fill_presence",
    "aspect_ratio",
    "bbox_area",
    "element_count",
    "parallel_offset",
};

/* Returns indices of local-maxima bins exceeding min_count. */
std::vector<int> find_peaks(const std::vector<int>& counts, int min_count)
{
    std::vector<int> peaks;
    const int n = static_cast<int>(counts.size());
    if (n == 0) return peaks;
    if (n == 1) {
        if (counts[0] >= min_count) peaks.push_back(0);
        return peaks;
    }
    for (int i = 0; i < n; ++i) {
        if (counts[i] < min_count) continue;
        bool left_ok  = (i == 0)     || counts[i] >= counts[i - 1];
        bool right_ok = (i == n - 1) || counts[i] >= counts[i + 1];
        if (left_ok && right_ok)
            peaks.push_back(i);
    }
    /* Merge adjacent plateau peaks: keep the bin with the higher count. */
    std::vector<int> merged;
    for (int p : peaks) {
        if (!merged.empty() && p == merged.back() + 1) {
            if (counts[p] > counts[merged.back()])
                merged.back() = p;
        } else {
            merged.push_back(p);
        }
    }
    return merged;
}

/* Returns the bin index of the minimum between [left_peak+1, right_peak-1]. */
int find_valley(const std::vector<int>& counts, int left_peak, int right_peak)
{
    int min_idx = left_peak + 1;
    for (int i = left_peak + 1; i < right_peak; ++i) {
        if (counts[i] < counts[min_idx])
            min_idx = i;
    }
    return min_idx;
}

} /* anonymous namespace */

/* ---------------------------------------------------------------------------
 * Debug mode flag
 * -------------------------------------------------------------------------*/

void DebugVizData::set_debug_enabled(bool enabled) noexcept
{
    debug_enabled_.store(enabled);
}

bool DebugVizData::is_debug_enabled() const noexcept
{
    return debug_enabled_.load();
}

/* ---------------------------------------------------------------------------
 * Cluster registry
 * -------------------------------------------------------------------------*/

void DebugVizData::register_cluster(const Cluster& cluster)
{
    /* ce-vii: skip storage when debug overlay is disabled to avoid unbounded
     * growth of element_indices vectors across multi-page sessions. */
    if (!debug_enabled_.load()) return;
    std::lock_guard<std::mutex> lock(mu_);
    clusters_[cluster.cluster_id] = cluster;
}

void DebugVizData::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    clusters_.clear();
    timings_.clear();
}

/* ---------------------------------------------------------------------------
 * API 1 — Feature vector overlay
 * -------------------------------------------------------------------------*/

FeatureOverlayData DebugVizData::get_feature_overlay(int cluster_id) const
{
    std::lock_guard<std::mutex> lock(mu_);

    FeatureOverlayData result;
    result.cluster_id = cluster_id;

    auto it = clusters_.find(cluster_id);
    if (it == clusters_.end()) return result; /* found stays false */

    result.found = true;
    const FeatureVector& fv = it->second.features;
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        result.features[i].name  = kFeatureNames[i];
        result.features[i].value = fv.dims[i];
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * API 2 — Stroke weight histogram
 * -------------------------------------------------------------------------*/

HistogramData DebugVizData::compute_histogram(
    const std::vector<float>& stroke_widths, float bin_width)
{
    HistogramData result;
    if (stroke_widths.empty() || bin_width <= 0.0f) return result;

    float min_w = *std::min_element(stroke_widths.begin(), stroke_widths.end());
    float max_w = *std::max_element(stroke_widths.begin(), stroke_widths.end());

    /* Handle degenerate case where all strokes have the same width. */
    if (max_w <= min_w + 1e-6f) {
        HistogramBin bin{min_w, min_w + bin_width,
                         static_cast<int>(stroke_widths.size())};
        result.bins.push_back(bin);
        result.bands.band_count    = 1;
        result.bands.thresholds[0] = bin.high;
        return result;
    }

    /* Build histogram buckets. */
    const int num_bins =
        static_cast<int>(std::ceil((max_w - min_w) / bin_width)) + 1;
    result.bins.resize(static_cast<size_t>(num_bins));
    for (int i = 0; i < num_bins; ++i) {
        result.bins[i] = {min_w + i * bin_width,
                          min_w + (i + 1) * bin_width,
                          0};
    }
    for (float w : stroke_widths) {
        int idx = static_cast<int>((w - min_w) / bin_width);
        idx = std::max(0, std::min(idx, num_bins - 1));
        result.bins[idx].count++;
    }

    /* Peak detection — require each peak to hold at least 5% of widths. */
    std::vector<int> counts(result.bins.size());
    for (size_t i = 0; i < result.bins.size(); ++i)
        counts[i] = result.bins[i].count;

    const int min_peak = std::max(1, static_cast<int>(stroke_widths.size()) / 20);
    std::vector<int> peaks = find_peaks(counts, min_peak);

    /* Spec says 2-4 bands typical in AEC drawings. */
    if (peaks.size() > 4) peaks.resize(4);

    StrokeWeightBands& bands = result.bands;

    if (peaks.empty()) {
        /* Fallback: single band covering the full range. */
        bands.band_count    = 1;
        bands.thresholds[0] = max_w + bin_width;
        return result;
    }

    bands.band_count = static_cast<int>(peaks.size());

    /* Thresholds sit at the valley between each consecutive pair of peaks.
     * The final threshold is the upper edge of the last peak's bin. */
    for (int i = 0; i + 1 < static_cast<int>(peaks.size()); ++i) {
        int valley          = find_valley(counts, peaks[i], peaks[i + 1]);
        bands.thresholds[i] = result.bins[valley].high;
    }
    bands.thresholds[bands.band_count - 1] = result.bins[peaks.back()].high;

    return result;
}

/* ---------------------------------------------------------------------------
 * API 3 — Cluster boundary overlay
 * -------------------------------------------------------------------------*/

std::vector<ClusterBoundsEntry> DebugVizData::get_cluster_bounds(
    float viewport_x0, float viewport_y0,
    float viewport_x1, float viewport_y1) const
{
    std::lock_guard<std::mutex> lock(mu_);

    std::vector<ClusterBoundsEntry> result;
    for (const auto& kv : clusters_) {
        const Cluster& c = kv.second;
        /* AABB overlap: clusters whose bounds intersect the viewport. */
        if (c.bounds[2] >= viewport_x0 && c.bounds[0] <= viewport_x1 &&
            c.bounds[3] >= viewport_y0 && c.bounds[1] <= viewport_y1) {
            ClusterBoundsEntry entry;
            entry.cluster_id = c.cluster_id;
            entry.bounds[0]  = c.bounds[0];
            entry.bounds[1]  = c.bounds[1];
            entry.bounds[2]  = c.bounds[2];
            entry.bounds[3]  = c.bounds[3];
            result.push_back(entry);
        }
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * API 4 — Inference timing
 * -------------------------------------------------------------------------*/

void DebugVizData::record_inference(int cluster_id, float elapsed_us)
{
    if (!debug_enabled_.load()) return;
    std::lock_guard<std::mutex> lock(mu_);
    timings_[cluster_id] = elapsed_us;
}

InferenceTiming DebugVizData::get_inference_timing(int cluster_id) const
{
    InferenceTiming result;
    result.cluster_id = cluster_id;
    if (!debug_enabled_.load()) return result; /* found stays false */

    std::lock_guard<std::mutex> lock(mu_);
    auto it = timings_.find(cluster_id);
    if (it == timings_.end()) return result;

    result.found      = true;
    result.elapsed_us = it->second;
    return result;
}

} /* namespace censor */
