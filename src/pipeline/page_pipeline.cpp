/*
 * page_pipeline.cpp — process_page: raw vector paths → clusters → classifications.
 *
 * This translation unit is compiled only when CENSOR_HAVE_RAPIDA_SHIM is set
 * (CMake adds the flag when RAPIDA_PUBLIC_INCLUDE is provided).  When the flag
 * is absent the no-op inline stub in the header is used instead.
 *
 * Spec: SPEC-censor-integration.md §"Cross-binary delivery (Censor-x7d)"
 */

#if defined(CENSOR_HAVE_RAPIDA_SHIM)

#include "pipeline/page_pipeline.h"

#include "clustering/feature_vector.h"
#include "clustering/stroke_weight_histogram.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace censor {
namespace pipeline {

namespace {

/* ---------------------------------------------------------------------------
 * Sanitise a float per ce-zgy: replace non-finite values with 0.0f so a
 * single NaN/Inf element does not poison the clustering or feature math.
 * -------------------------------------------------------------------------*/
static inline float sanitise(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}

/* ---------------------------------------------------------------------------
 * Convert one RapidaPathElement (plus per-element queries from the api) into
 * a Censor ElementData.  All float fields are sanitised.
 *
 * element_key is used as the uint32 id — stable within a stream pass.
 * -------------------------------------------------------------------------*/
static ElementData element_from_rapida(
    RapidaVectorEngineHandle          engine,
    const RapidaVectorEngineApi*      api,
    const RapidaPathElement&          path_elem)
{
    ElementData e{};
    e.id = path_elem.handle.element_key;

    /* Bounds (ce-zgy: sanitise every float component) */
    RapidaElementBounds b = api->bounds(engine, path_elem.handle);
    e.bounds[0] = sanitise(b.min_x);
    e.bounds[1] = sanitise(b.min_y);
    e.bounds[2] = sanitise(b.max_x);
    e.bounds[3] = sanitise(b.max_y);

    /* Stroke */
    RapidaStrokeInfo s = api->stroke(engine, path_elem.handle);
    e.stroke_width = sanitise(s.width);
    e.stroke_rgba  = s.rgba;
    e.is_stroked   = (s.is_stroked != 0);

    /* Fill */
    RapidaFillInfo f = api->fill(engine, path_elem.handle);
    e.fill_rgba = f.rgba;
    e.is_filled = (f.is_filled != 0);

    /* Transform (6-component 2×3 affine: a,b,c,d,e,f → tx=e,ty=f in PDF) */
    RapidaTransform t = api->transform(engine, path_elem.handle);
    e.transform[0] = sanitise(t.a);
    e.transform[1] = sanitise(t.b);
    e.transform[2] = sanitise(t.c);
    e.transform[3] = sanitise(t.d);
    e.transform[4] = sanitise(t.e);
    e.transform[5] = sanitise(t.f);

    return e;
}

} /* anonymous namespace */

/* ---------------------------------------------------------------------------
 * process_page — public implementation (shim build only).
 * -------------------------------------------------------------------------*/
PageResult process_page(
    RapidaVectorEngineHandle          engine,
    const RapidaVectorEngineApi*      api,
    int32_t                           page_index,
    IClassifier&                      classifier,
    ConfidenceOverlay&                overlay,
    LogFn                             log_fn,
    void*                             log_ud)
{
    PageResult result;
    result.page_index = static_cast<int>(page_index);

    if (!api) {
        return result;   /* no-op: no shim table */
    }

    auto log = [&](int level, const char* msg) {
        if (log_fn) log_fn(log_ud, level, "censor.pipeline", msg);
    };

    /* --- 1. Enumerate paths for this page ---------------------------------- */
    RapidaPathStreamHandle stream = api->enumerate_paths(engine, page_index);
    if (!stream) {
        log(3 /* WARN */, "enumerate_paths returned null stream");
        return result;
    }

    /* --- 2. Build ElementData vector (sanitise all floats, ce-zgy) --------- */
    {
        RapidaPathElement path_elem{};
        while (api->path_stream_next(stream, &path_elem)) {
            result.elements.push_back(
                element_from_rapida(engine, api, path_elem));
        }
    }

    if (result.elements.empty()) {
        return result;   /* blank page — nothing to cluster */
    }

    /* --- 3. Stroke weight calibration -------------------------------------- */
    std::vector<float> widths;
    widths.reserve(result.elements.size());
    for (const auto& e : result.elements) {
        if (e.is_stroked) widths.push_back(e.stroke_width);
    }
    StrokeWeightBands bands = widths.empty()
        ? StrokeWeightBands{}
        : analyze_stroke_weights(widths);

    /* --- 4. Spatial clustering --------------------------------------------- */
    result.clusters = cluster_page(result.elements, bands);

    /* --- 5. Feature extraction + classification per cluster ----------------- */
    for (auto& cluster : result.clusters) {
        cluster.features = compute_features(cluster, result.elements, bands);

        ClassifyResult cr = classifier.predict(cluster.features);

        if (cr.above_threshold) {
            overlay.update(cluster.cluster_id, cluster.bounds, cr,
                           /*is_user_labeled=*/false);
        }
    }

    return result;
}

} /* namespace pipeline */
} /* namespace censor */

#endif /* CENSOR_HAVE_RAPIDA_SHIM */
