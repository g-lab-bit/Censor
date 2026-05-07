#ifndef CENSOR_DEBUG_VIZ_DATA_H
#define CENSOR_DEBUG_VIZ_DATA_H

#include "censor_types.h"

#include <map>
#include <mutex>
#include <vector>

namespace censor {

/* ---------------------------------------------------------------------------
 * NamedFeatureValue — one labelled feature dimension for the debug overlay.
 * name points to a static string literal (program lifetime, no ownership).
 * -------------------------------------------------------------------------*/
struct NamedFeatureValue {
    const char* name  = nullptr;
    float       value = 0.0f;
};

/* ---------------------------------------------------------------------------
 * FeatureOverlayData — result for API 1: feature vector overlay.
 * When found==false the features array is uninitialised and must not be read.
 * -------------------------------------------------------------------------*/
struct FeatureOverlayData {
    int               cluster_id                 = -1;
    bool              found                      = false;
    NamedFeatureValue features[FEATURE_DIM_COUNT] = {};
};

/* ---------------------------------------------------------------------------
 * HistogramBin — one bucket from the stroke weight histogram.
 * -------------------------------------------------------------------------*/
struct HistogramBin {
    float low;   /* inclusive lower bound (pts) */
    float high;  /* exclusive upper bound (pts) */
    int   count; /* number of stroke widths in [low, high) */
};

/* ---------------------------------------------------------------------------
 * HistogramData — result for API 2: stroke weight histogram.
 * bins: raw bucket counts. bands: detected weight-band thresholds.
 * -------------------------------------------------------------------------*/
struct HistogramData {
    std::vector<HistogramBin> bins;
    StrokeWeightBands         bands;
};

/* ---------------------------------------------------------------------------
 * ClusterBoundsEntry — one cluster bounding rect for API 3.
 * -------------------------------------------------------------------------*/
struct ClusterBoundsEntry {
    int   cluster_id;
    float bounds[4]; /* x0, y0, x1, y1 */
};

/* ---------------------------------------------------------------------------
 * DebugVizData — cluster registry + three debug visualization data APIs.
 *
 * Rapida renders; Censor provides the data. These are the observation tools
 * for the seeding run (SPEC-censor-core.md §10 Phase 2, SPEC-censor-ui.md §8).
 *
 * Thread-safe: all methods are safe from any thread.
 * -------------------------------------------------------------------------*/
class DebugVizData {
public:
    /* Register or update a cluster in the internal registry. */
    void register_cluster(const Cluster& cluster);

    /* Remove all registered clusters. */
    void clear();

    /* API 1 — Feature vector overlay.
     * Returns the 12 named feature values for cluster_id.
     * found==false when cluster_id is not registered. */
    FeatureOverlayData get_feature_overlay(int cluster_id) const;

    /* API 2 — Stroke weight histogram.
     * Pure function: no internal state required.
     * Bins stroke_widths into buckets of bin_width pts; detects peak-based bands.
     * Returns empty HistogramData when stroke_widths is empty. */
    static HistogramData compute_histogram(
        const std::vector<float>& stroke_widths,
        float bin_width = 0.1f);

    /* API 3 — Cluster boundary overlay.
     * Returns entries whose bounds overlap the viewport [x0,y0) x [y0,y1).
     * Pass (0,0, FLT_MAX, FLT_MAX) to retrieve all registered clusters. */
    std::vector<ClusterBoundsEntry> get_cluster_bounds(
        float viewport_x0, float viewport_y0,
        float viewport_x1, float viewport_y1) const;

private:
    mutable std::mutex     mu_;
    std::map<int, Cluster> clusters_; /* keyed by cluster_id */
};

} /* namespace censor */

#endif /* CENSOR_DEBUG_VIZ_DATA_H */
