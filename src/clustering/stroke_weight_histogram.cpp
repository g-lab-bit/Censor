#include "clustering/stroke_weight_histogram.h"
#include "clustering/cluster.h"  /* MAX_HISTOGRAM_BINS (ce-dfq) */

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace censor {

StrokeWeightBands analyze_stroke_weights(const std::vector<float>& stroke_widths) {
    StrokeWeightBands result;

    // Collect positive widths only.
    std::vector<float> valid;
    valid.reserve(stroke_widths.size());
    for (float w : stroke_widths)
        if (w > 0.0f) valid.push_back(w);

    if (valid.empty()) return result;  // band_count stays 0

    float min_w = *std::min_element(valid.begin(), valid.end());
    float max_w = *std::max_element(valid.begin(), valid.end());

    // All widths within one bin → single band.
    if (max_w - min_w < STROKE_HISTOGRAM_BIN_WIDTH) {
        result.band_count    = 1;
        result.thresholds[0] = max_w + STROKE_HISTOGRAM_BIN_WIDTH;
        return result;
    }

    // Build histogram.
    int n_bins = static_cast<int>(
        std::ceil((max_w - min_w) / STROKE_HISTOGRAM_BIN_WIDTH)) + 1;

    /* DoS guard, see ce-dfq: adversarial stroke widths (e.g. {0.001, 1e9})
     * produce ~1e10 bins, causing std::bad_alloc that kills the worker thread.
     * Fall through to the single-band early-out shape when the cap is exceeded. */
    if (n_bins > MAX_HISTOGRAM_BINS) {
        result.band_count    = 1;
        result.thresholds[0] = max_w + STROKE_HISTOGRAM_BIN_WIDTH;
        return result;
    }

    std::vector<int> hist(static_cast<size_t>(n_bins), 0);
    for (float w : valid) {
        int b = static_cast<int>((w - min_w) / STROKE_HISTOGRAM_BIN_WIDTH);
        b = std::clamp(b, 0, n_bins - 1);
        hist[b]++;
    }

    // Find contiguous runs of non-zero bins.
    struct BinRun { int start, end; };
    std::vector<BinRun> runs;
    for (int i = 0; i < n_bins; ) {
        if (hist[i] == 0) { ++i; continue; }
        int j = i;
        while (j < n_bins && hist[j] > 0) ++j;
        runs.push_back({i, j - 1});
        i = j;
    }

    if (runs.empty() || runs.size() == 1) {
        result.band_count    = 1;
        result.thresholds[0] = max_w + STROKE_HISTOGRAM_BIN_WIDTH;
        return result;
    }

    // Merge runs until ≤4 bands by collapsing the smallest gaps first.
    while (runs.size() > 4) {
        int min_gap = INT_MAX;
        size_t merge_at = 0;
        for (size_t i = 0; i + 1 < runs.size(); ++i) {
            int gap = runs[i + 1].start - runs[i].end;
            if (gap < min_gap) { min_gap = gap; merge_at = i; }
        }
        runs[merge_at].end = runs[merge_at + 1].end;
        runs.erase(runs.begin() + static_cast<int>(merge_at) + 1);
    }

    // Thresholds sit at the midpoint of each inter-run gap.
    std::vector<float> thresholds;
    thresholds.reserve(runs.size());
    for (size_t i = 0; i + 1 < runs.size(); ++i) {
        float end_of_run   = min_w + (runs[i].end + 1) * STROKE_HISTOGRAM_BIN_WIDTH;
        float start_of_next = min_w + runs[i + 1].start * STROKE_HISTOGRAM_BIN_WIDTH;
        thresholds.push_back((end_of_run + start_of_next) * 0.5f);
    }
    thresholds.push_back(max_w + STROKE_HISTOGRAM_BIN_WIDTH);  // upper bound of last band

    result.band_count = static_cast<int>(thresholds.size());
    for (int i = 0; i < result.band_count && i < 8; ++i)
        result.thresholds[i] = thresholds[static_cast<size_t>(i)];

    return result;
}

} // namespace censor
