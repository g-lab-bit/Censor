#include "clustering/feature_vector.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace censor {

static constexpr int   ORI_BINS  = 18;            /* 10° per bin across [0, π/2] */
static constexpr float HALF_PI_F = 1.57079632f;

/* Angle of the major axis of an element's bounding box, in [0, π/2]. */
static float major_axis_angle(const ElementData& e)
{
    float w = e.bounds[2] - e.bounds[0];
    float h = e.bounds[3] - e.bounds[1];
    return std::atan2f(h, w);
}

/* Length along the major axis (longer bounding-box dimension). */
static float major_axis_length(const ElementData& e)
{
    float w = e.bounds[2] - e.bounds[0];
    float h = e.bounds[3] - e.bounds[1];
    return std::max(w, h);
}

/* Return the weight-band index for a stroke width given calibrated bands. */
static int weight_to_band(float stroke_width, const StrokeWeightBands& bands)
{
    for (int i = 0; i < bands.band_count; ++i) {
        if (stroke_width <= bands.thresholds[i])
            return i;
    }
    return std::max(0, bands.band_count - 1);
}

FeatureVector compute_features(
    const Cluster&                  cluster,
    const std::vector<ElementData>& elements,
    const StrokeWeightBands&        weight_bands)
{
    FeatureVector fv{};

    if (cluster.element_indices.empty())
        return fv;

    /* --- dims from cluster bounds (no per-element loop needed) ------------ */

    float bw = cluster.bounds[2] - cluster.bounds[0];
    float bh = cluster.bounds[3] - cluster.bounds[1];

    fv.dims[ELEMENT_COUNT] =
        static_cast<float>(cluster.element_indices.size());       /* dim 10 */

    fv.dims[ASPECT_RATIO]  = (bh > 0.0f) ? (bw / bh) : 1.0f;    /* dim  8 */
    fv.dims[BBOX_AREA]     = bw * bh;                             /* dim  9 */

    /* --- per-element accumulation ---------------------------------------- */

    float total_stroke_len    = 0.0f;
    float total_stroke_weight = 0.0f;
    float band_sum            = 0.0f;
    int   stroked_count       = 0;
    int   fill_count          = 0;
    int   loop_count          = 0;
    float loop_area           = 0.0f;

    std::array<int, ORI_BINS> ori_hist{};
    ori_hist.fill(0);

    struct StrokeProj { float angle, cx, cy, length; };
    std::vector<StrokeProj> stroked;
    stroked.reserve(cluster.element_indices.size());

    for (int idx : cluster.element_indices) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= elements.size())
            continue;

        const ElementData& e = elements[static_cast<std::size_t>(idx)];

        if (e.is_filled) {
            ++fill_count;
            ++loop_count;                                /* closed-loop proxy */
            float ew = e.bounds[2] - e.bounds[0];
            float eh = e.bounds[3] - e.bounds[1];
            loop_area += ew * eh;                        /* shoelace proxy    */
        }

        if (e.is_stroked) {
            float angle  = major_axis_angle(e);
            float length = major_axis_length(e);

            total_stroke_len    += length;
            total_stroke_weight += e.stroke_width;
            band_sum += static_cast<float>(weight_to_band(e.stroke_width, weight_bands));
            ++stroked_count;

            int bin = static_cast<int>((angle / HALF_PI_F) * ORI_BINS);
            bin = std::clamp(bin, 0, ORI_BINS - 1);
            ori_hist[static_cast<std::size_t>(bin)]++;

            float cx = (e.bounds[0] + e.bounds[2]) * 0.5f;
            float cy = (e.bounds[1] + e.bounds[3]) * 0.5f;
            stroked.push_back({angle, cx, cy, length});
        }
    }

    /* --- scalar dims from accumulated values ----------------------------- */

    fv.dims[STROKE_LENGTH] =
        total_stroke_len / FV_NORM_STROKE_LENGTH;                  /* dim  0 */

    /* dominant orientation: centre angle of the peak histogram bin */
    {
        auto peak = std::max_element(ori_hist.begin(), ori_hist.end());
        int  bin  = static_cast<int>(std::distance(ori_hist.begin(), peak));
        fv.dims[DOMINANT_ORIENTATION] =
            (static_cast<float>(bin) + 0.5f) * HALF_PI_F / ORI_BINS; /* dim 1 */
    }

    fv.dims[STROKE_WEIGHT_MEAN] =
        (stroked_count > 0)
        ? total_stroke_weight / static_cast<float>(stroked_count)
        : 0.0f;                                                    /* dim  2 */

    fv.dims[STROKE_WEIGHT_BAND] =
        (stroked_count > 0)
        ? band_sum / static_cast<float>(stroked_count)
        : 0.0f;                                                    /* dim  3 */

    fv.dims[CLOSED_LOOP_COUNT] = static_cast<float>(loop_count);  /* dim  4 */
    fv.dims[CLOSED_LOOP_AREA]  = loop_area / FV_NORM_LOOP_AREA;   /* dim  5 */

    fv.dims[ADJACENT_CONNECTIVITY] = 0.0f; /* dim 6 — requires grid context */

    fv.dims[FILL_PRESENCE] = (fill_count > 0) ? 1.0f : 0.0f;     /* dim  7 */

    /* --- parallel offset: perpendicular separation between dominant strokes */

    if (stroked.size() >= 2) {
        float dom   = fv.dims[DOMINANT_ORIENTATION];
        float pcos  = std::cos(dom + HALF_PI_F);
        float psin  = std::sin(dom + HALF_PI_F);

        std::vector<float> proj;
        proj.reserve(stroked.size());
        for (const auto& s : stroked) {
            if (std::abs(s.angle - dom) < FV_PARALLEL_ANGLE_TOL &&
                s.length >= FV_PARALLEL_MIN_LENGTH) {
                proj.push_back(s.cx * pcos + s.cy * psin);
            }
        }
        if (proj.size() >= 2) {
            auto [lo, hi] = std::minmax_element(proj.begin(), proj.end());
            fv.dims[PARALLEL_OFFSET] =
                (*hi - *lo) / FV_NORM_PARALLEL_OFFSET;            /* dim 11 */
        }
    }

    return fv;
}

} /* namespace censor */
