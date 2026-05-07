/*
 * test_utils.h — Synthetic data generators for Censor unit tests.
 *
 * Header-only. Include from test files that need fake AEC element data.
 * All types come from censor_types.h.
 *
 * Usage: #include "test_utils.h"
 */

#ifndef CENSOR_TEST_UTILS_H
#define CENSOR_TEST_UTILS_H

#include "censor_types.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace censor::test {

/* ---------------------------------------------------------------------------
 * make_element — single ElementData with explicit bounds and stroke/fill props.
 * Sets identity transform; is_stroked when stroke_width > 0, is_filled when fill_rgba != 0.
 * -------------------------------------------------------------------------*/
inline ElementData make_element(uint32_t id,
                                float x, float y,
                                float w, float h,
                                float stroke_width,
                                uint32_t stroke_rgba = 0,
                                uint32_t fill_rgba   = 0)
{
    ElementData e{};
    e.id          = id;
    e.bounds[0]   = x;
    e.bounds[1]   = y;
    e.bounds[2]   = x + w;
    e.bounds[3]   = y + h;
    e.stroke_width = stroke_width;
    e.stroke_rgba  = stroke_rgba;
    e.fill_rgba    = fill_rgba;
    e.is_stroked   = (stroke_width > 0.0f);
    e.is_filled    = (fill_rgba != 0);
    /* Identity 2×3 affine: a=1, b=0, c=0, d=1, tx=0, ty=0 */
    e.transform[0] = 1.0f; e.transform[1] = 0.0f;
    e.transform[2] = 0.0f; e.transform[3] = 1.0f;
    e.transform[4] = 0.0f; e.transform[5] = 0.0f;
    return e;
}

/* ---------------------------------------------------------------------------
 * make_element_grid — rows×cols elements in a regular lattice at `spacing` intervals.
 * Useful for synthesising ceiling grid patterns where periodicity is the key signal.
 * -------------------------------------------------------------------------*/
inline std::vector<ElementData> make_element_grid(int rows, int cols,
                                                   float spacing,
                                                   float stroke_width)
{
    std::vector<ElementData> elems;
    elems.reserve(static_cast<size_t>(rows * cols));
    uint32_t id = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float x = static_cast<float>(c) * spacing;
            float y = static_cast<float>(r) * spacing;
            elems.push_back(make_element(id++, x, y, spacing, spacing, stroke_width));
        }
    }
    return elems;
}

/* ---------------------------------------------------------------------------
 * make_wall_cluster — two parallel horizontal strokes mimicking a wall section.
 * AEC walls appear as pairs of strokes: thin, elongated, consistent weight.
 * -------------------------------------------------------------------------*/
inline std::vector<ElementData> make_wall_cluster()
{
    const float stroke_w = 0.5f;   /* thin strokes typical of wall lines */
    const float length   = 200.0f; /* long axis: wall run length */
    const float gap      = 8.0f;   /* separation between faces: wall thickness */
    return {
        make_element(0, 0.0f, 0.0f, length, stroke_w, stroke_w),
        make_element(1, 0.0f, gap,  length, stroke_w, stroke_w),
    };
}

/* ---------------------------------------------------------------------------
 * make_duct_cluster — two closed-rectangle sections mimicking a duct run.
 * Ducts appear as closed outlines with medium stroke weight and low aspect ratio.
 * -------------------------------------------------------------------------*/
inline std::vector<ElementData> make_duct_cluster()
{
    const float    stroke_w = 1.2f;        /* medium stroke, heavier than walls */
    const uint32_t stroke_c = 0x000000FFu; /* opaque black */
    return {
        make_element(0,  0.0f, 0.0f, 50.0f, 30.0f, stroke_w, stroke_c, 0),
        make_element(1, 55.0f, 0.0f, 50.0f, 30.0f, stroke_w, stroke_c, 0),
    };
}

/* ---------------------------------------------------------------------------
 * make_random_elements — count deterministic pseudo-random elements seeded by `seed`.
 * Uses std::mt19937 so same (count, seed) always produces identical output.
 * -------------------------------------------------------------------------*/
inline std::vector<ElementData> make_random_elements(int count, uint32_t seed)
{
    std::vector<ElementData> elems;
    elems.reserve(static_cast<size_t>(count));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 500.0f);
    std::uniform_real_distribution<float> dim(1.0f, 100.0f);
    std::uniform_real_distribution<float> sw(0.1f, 2.0f);

    for (int i = 0; i < count; ++i) {
        elems.push_back(make_element(
            static_cast<uint32_t>(i),
            pos(rng), pos(rng),
            dim(rng), dim(rng),
            sw(rng)
        ));
    }
    return elems;
}

/* ---------------------------------------------------------------------------
 * make_feature_vector — FeatureVector pre-loaded with class-typical values.
 * label_hint: "wall", "duct", "pipe", "furniture", "ceiling_grid".
 * Unknown labels return a zero vector (safe default for negative test cases).
 * -------------------------------------------------------------------------*/
inline FeatureVector make_feature_vector(const std::string& label_hint)
{
    FeatureVector fv{};

    if (label_hint == "wall") {
        fv.dims[STROKE_LENGTH]         = 400.0f;  /* long continuous run */
        fv.dims[DOMINANT_ORIENTATION]  = 0.0f;    /* horizontal */
        fv.dims[STROKE_WEIGHT_MEAN]    = 0.5f;    /* thin */
        fv.dims[STROKE_WEIGHT_BAND]    = 0.0f;
        fv.dims[CLOSED_LOOP_COUNT]     = 0.0f;    /* open strokes */
        fv.dims[CLOSED_LOOP_AREA]      = 0.0f;
        fv.dims[ADJACENT_CONNECTIVITY] = 4.0f;
        fv.dims[FILL_PRESENCE]         = 0.0f;
        fv.dims[ASPECT_RATIO]          = 25.0f;   /* very elongated */
        fv.dims[BBOX_AREA]             = 1600.0f;
        fv.dims[ELEMENT_COUNT]         = 2.0f;
        fv.dims[PARALLEL_OFFSET]       = 8.0f;    /* wall thickness */
    } else if (label_hint == "duct") {
        fv.dims[STROKE_LENGTH]         = 200.0f;
        fv.dims[DOMINANT_ORIENTATION]  = 0.0f;
        fv.dims[STROKE_WEIGHT_MEAN]    = 1.2f;    /* medium weight */
        fv.dims[STROKE_WEIGHT_BAND]    = 1.0f;
        fv.dims[CLOSED_LOOP_COUNT]     = 2.0f;    /* closed rectangular outline */
        fv.dims[CLOSED_LOOP_AREA]      = 3000.0f;
        fv.dims[ADJACENT_CONNECTIVITY] = 2.0f;
        fv.dims[FILL_PRESENCE]         = 0.0f;
        fv.dims[ASPECT_RATIO]          = 1.67f;   /* wider than tall */
        fv.dims[BBOX_AREA]             = 3000.0f;
        fv.dims[ELEMENT_COUNT]         = 2.0f;
        fv.dims[PARALLEL_OFFSET]       = 30.0f;   /* duct height */
    } else if (label_hint == "pipe") {
        fv.dims[STROKE_LENGTH]         = 300.0f;
        fv.dims[DOMINANT_ORIENTATION]  = 1.5708f; /* vertical (π/2) */
        fv.dims[STROKE_WEIGHT_MEAN]    = 0.8f;
        fv.dims[STROKE_WEIGHT_BAND]    = 0.0f;
        fv.dims[CLOSED_LOOP_COUNT]     = 0.0f;
        fv.dims[CLOSED_LOOP_AREA]      = 0.0f;
        fv.dims[ADJACENT_CONNECTIVITY] = 1.0f;
        fv.dims[FILL_PRESENCE]         = 0.0f;
        fv.dims[ASPECT_RATIO]          = 0.1f;    /* tall and narrow */
        fv.dims[BBOX_AREA]             = 2400.0f;
        fv.dims[ELEMENT_COUNT]         = 1.0f;
        fv.dims[PARALLEL_OFFSET]       = 0.0f;
    } else if (label_hint == "furniture") {
        fv.dims[STROKE_LENGTH]         = 150.0f;
        fv.dims[DOMINANT_ORIENTATION]  = 0.0f;
        fv.dims[STROKE_WEIGHT_MEAN]    = 0.6f;
        fv.dims[STROKE_WEIGHT_BAND]    = 0.0f;
        fv.dims[CLOSED_LOOP_COUNT]     = 1.0f;
        fv.dims[CLOSED_LOOP_AREA]      = 5000.0f;
        fv.dims[ADJACENT_CONNECTIVITY] = 0.0f;
        fv.dims[FILL_PRESENCE]         = 1.0f;    /* furniture often filled */
        fv.dims[ASPECT_RATIO]          = 1.2f;    /* near-square block */
        fv.dims[BBOX_AREA]             = 5000.0f;
        fv.dims[ELEMENT_COUNT]         = 5.0f;
        fv.dims[PARALLEL_OFFSET]       = 0.0f;
    } else if (label_hint == "ceiling_grid") {
        fv.dims[STROKE_LENGTH]         = 800.0f;
        fv.dims[DOMINANT_ORIENTATION]  = 0.0f;
        fv.dims[STROKE_WEIGHT_MEAN]    = 0.25f;   /* very thin, fine lattice */
        fv.dims[STROKE_WEIGHT_BAND]    = 0.0f;
        fv.dims[CLOSED_LOOP_COUNT]     = 0.0f;
        fv.dims[CLOSED_LOOP_AREA]      = 0.0f;
        fv.dims[ADJACENT_CONNECTIVITY] = 12.0f;   /* densely connected cells */
        fv.dims[FILL_PRESENCE]         = 0.0f;
        fv.dims[ASPECT_RATIO]          = 1.0f;    /* square grid cells */
        fv.dims[BBOX_AREA]             = 36000.0f;
        fv.dims[ELEMENT_COUNT]         = 20.0f;
        fv.dims[PARALLEL_OFFSET]       = 60.0f;   /* grid cell size */
    }
    /* Unknown label → all dims remain 0.0f (useful for negative test cases). */

    return fv;
}

} /* namespace censor::test */

#endif /* CENSOR_TEST_UTILS_H */
