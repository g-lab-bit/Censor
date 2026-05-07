/*
 * censor_types.h — Shared internal data types for Censor modules.
 *
 * Header-only. Include from any Censor source file that needs these types.
 * No .cpp counterpart — all definitions live here.
 *
 * ElementData is a mock stand-in for IVectorEngine element data until
 * Phase 2 (rapida_vector_engine_c.h integration) lands. Its field shape
 * mirrors the C ABI shim types in SPEC-censor-integration.md.
 */

#ifndef CENSOR_TYPES_H
#define CENSOR_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace censor {

/* ---------------------------------------------------------------------------
 * ElementData — mock element from IVectorEngine stream.
 *
 * Mirrors the primitive shape described in SPEC-censor-integration.md
 * §C-ABI-shim. Used internally; never crosses the DLL boundary.
 * -------------------------------------------------------------------------*/
struct ElementData {
    uint32_t id;
    float    bounds[4];        /* x0, y0, x1, y1 */
    float    stroke_width;
    uint32_t stroke_rgba;
    uint32_t fill_rgba;
    bool     is_stroked;
    bool     is_filled;
    float    transform[6];     /* 2×3 affine matrix: [a,b,c,d,tx,ty] */
};

/* ---------------------------------------------------------------------------
 * FeatureVector — 12 per-cluster geometric features (SPEC-censor-core.md §4).
 * -------------------------------------------------------------------------*/
enum FeatureDim : int {
    STROKE_LENGTH         = 0,  /* total stroke length (sum of segment lengths) */
    DOMINANT_ORIENTATION  = 1,  /* angle in radians, histogram peak */
    STROKE_WEIGHT_MEAN    = 2,  /* mean stroke width */
    STROKE_WEIGHT_BAND    = 3,  /* calibrated band index from §2 */
    CLOSED_LOOP_COUNT     = 4,  /* number of closed subpaths */
    CLOSED_LOOP_AREA      = 5,  /* sum of shoelace areas of closed loops */
    ADJACENT_CONNECTIVITY = 6,  /* neighboring cells containing this cluster */
    FILL_PRESENCE         = 7,  /* 1.0 if any fill element, 0.0 otherwise */
    ASPECT_RATIO          = 8,  /* bounds width / height */
    BBOX_AREA             = 9,  /* bounds width × height */
    ELEMENT_COUNT         = 10, /* number of elements in cluster */
    PARALLEL_OFFSET       = 11, /* distance between dominant parallel strokes */

    FEATURE_DIM_COUNT     = 12
};

struct FeatureVector {
    float dims[FEATURE_DIM_COUNT] = {};
};

/* ---------------------------------------------------------------------------
 * Cluster — spatial group of element indices with precomputed geometry.
 * -------------------------------------------------------------------------*/
struct Cluster {
    std::vector<int> element_indices; /* indices into the page element array */
    float            bounds[4] = {};  /* x0, y0, x1, y1 */
    FeatureVector    features;
    int              cluster_id = -1; /* stable per-page per-session index */
};

/* ---------------------------------------------------------------------------
 * StrokeWeightBands — calibrated stroke weight buckets (SPEC-censor-core §2).
 * -------------------------------------------------------------------------*/
struct StrokeWeightBands {
    int   band_count               = 0;
    float thresholds[8]            = {}; /* upper boundary of each band */
};

/* ---------------------------------------------------------------------------
 * CeilingGridResult — output of the ceiling grid pre-pass (§3).
 * -------------------------------------------------------------------------*/
struct CeilingGridResult {
    bool  is_grid    = false;
    float cell_width = 0.0f;
    float cell_height = 0.0f;
};

/* ---------------------------------------------------------------------------
 * ClassifyResult — k-NN prediction output (§6).
 * -------------------------------------------------------------------------*/
struct ClassifyResult {
    std::string label;
    float       confidence       = 0.0f;
    bool        above_threshold  = false;
};

/* ---------------------------------------------------------------------------
 * CensorOverlayEntry — per-cluster render data (SPEC-censor-plugin.md §5).
 * -------------------------------------------------------------------------*/
struct CensorOverlayEntry {
    float       bounds[4]       = {}; /* x0, y0, x1, y1 of cluster */
    float       color[4]        = {}; /* RGBA (class color at confidence opacity) */
    std::string label;                /* predicted or user-applied label */
    float       confidence      = 0.0f;
    int         cluster_id      = -1;
    bool        is_user_labeled = false;
};

} /* namespace censor */

#endif /* CENSOR_TYPES_H */
