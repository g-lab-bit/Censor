/*
 * page_pipeline.h — Internal pipeline: raw vector paths → clusters → classifications.
 *
 * process_page() is NOT exported from the DLL (internal linkage).  It is
 * exposed as a non-exported symbol for unit tests that compile the sources
 * directly (like test_clustering / test_pipeline pattern).
 *
 * Spec: SPEC-censor-integration.md §"Cross-binary delivery (Censor-x7d)"
 */

#pragma once

#include "censor_abi.h"      /* RapidaVectorEngineHandle */
#include "censor_types.h"

#include "clustering/spatial_clustering.h"
#include "classifier/iclassifier.h"
#include "overlay/confidence_overlay.h"

#include <vector>

/* Guard: the real implementation requires the Rapida shim headers.
 * When CENSOR_HAVE_RAPIDA_SHIM is not defined the pipeline compiles to a
 * no-op stub so the standalone build still links cleanly. */
#if defined(CENSOR_HAVE_RAPIDA_SHIM)
#include <rapida_vector_engine_c.h>
#endif

namespace censor {
namespace pipeline {

/* ---------------------------------------------------------------------------
 * PageResult — output of one process_page() call.
 * -------------------------------------------------------------------------*/
struct PageResult {
    std::vector<ElementData>  elements;   /* sanitised elements read from the engine  */
    std::vector<Cluster>      clusters;   /* spatial clusters of those elements       */
    int                       page_index = -1;
};

/* ---------------------------------------------------------------------------
 * process_page — run the full pipeline on one page.
 *
 * Parameters
 *   engine       — opaque handle handed to censor_attach_engine.
 *   api          — function-pointer table received via RapidaHostCallbacks v2.
 *   page_index   — 0-based page number.
 *   classifier   — in-memory classifier (predict is thread-safe).
 *   overlay      — confidence overlay updated with predictions.
 *   log_fn/log_ud — optional log sink; may be nullptr.
 *
 * Data flow:
 *   api->enumerate_paths(engine, page)
 *     → loop api->path_stream_next → RapidaPathElement
 *     → build ElementData (sanitise NaN/Inf coords to 0.0f per ce-zgy)
 *     → cluster_page(elements)
 *     → compute_features(cluster, elements)
 *     → classifier.predict(features)
 *     → overlay.update(cluster_id, bounds, result)
 *
 * PathStream lifetime: the stream handle is valid only until the next
 * enumerate_paths call on the same engine — finish the page before moving on.
 *
 * Returns the PageResult (elements + clusters).  Returns an empty PageResult
 * when api is null (standalone/no-shim build) or enumerate_paths fails.
 * Never throws — all exceptions are swallowed and logged via log_fn.
 * -------------------------------------------------------------------------*/

using LogFn = void(*)(void* ud, int level, const char* cat, const char* msg);

#if defined(CENSOR_HAVE_RAPIDA_SHIM)

PageResult process_page(
    RapidaVectorEngineHandle          engine,
    const RapidaVectorEngineApi*      api,
    int32_t                           page_index,
    IClassifier&                      classifier,
    ConfidenceOverlay&                overlay,
    LogFn                             log_fn  = nullptr,
    void*                             log_ud  = nullptr);

#else /* !CENSOR_HAVE_RAPIDA_SHIM */

/* Standalone stub — no-op so the build compiles without Rapida headers.
 * The api parameter type degrades to void* to avoid requiring the shim
 * header; callers that pass nullptr get an empty result. */
inline PageResult process_page(
    RapidaVectorEngineHandle  /*engine*/,
    const void*               /*api*/,
    int32_t                   page_index,
    IClassifier&              /*classifier*/,
    ConfidenceOverlay&        /*overlay*/,
    LogFn                     /*log_fn*/ = nullptr,
    void*                     /*log_ud*/ = nullptr)
{
    PageResult r;
    r.page_index = page_index;
    return r;
}

#endif /* CENSOR_HAVE_RAPIDA_SHIM */

} /* namespace pipeline */
} /* namespace censor */
