#include <gtest/gtest.h>
<<<<<<< HEAD
#include "clustering/spatial_clustering.h"
#include "clustering/stroke_weight_histogram.h"
#include "test_utils.h"

#include <algorithm>
=======

#include <cmath>
#include <limits>

#include "censor_types.h"
#include "clustering/feature_vector.h"
#include "test_utils.h"
>>>>>>> 99573ee (feat: feature vector extraction — 12 dimensions per cluster (ce-082))

using namespace censor;
using namespace censor::test;

<<<<<<< HEAD
// --- Proximity grouping ---

TEST(ClusterPage, EmptyInputReturnsEmpty) {
    std::vector<ElementData> elems;
    EXPECT_TRUE(cluster_page(elems).empty());
}

TEST(ClusterPage, NearbyElementsGrouped) {
    // elem0 at x=0..10, elem1 at x=12..22: gap=2, within CLUSTER_PROXIMITY_TOLERANCE
    // elem2 at x=200..210: far away, forms its own cluster
    std::vector<ElementData> elems = {
        make_element(0,   0.0f, 0.0f, 10.0f, 2.0f, 0.5f),
        make_element(1,  12.0f, 0.0f, 10.0f, 2.0f, 0.5f),
        make_element(2, 200.0f, 0.0f, 10.0f, 2.0f, 0.5f),
    };
    auto clusters = cluster_page(elems);

    ASSERT_EQ(clusters.size(), 2u);

    bool found_pair = false;
    for (const auto& c : clusters) {
        if (c.element_indices.size() == 2u) {
            found_pair = true;
            auto& ei = c.element_indices;
            EXPECT_NE(std::find(ei.begin(), ei.end(), 0), ei.end());
            EXPECT_NE(std::find(ei.begin(), ei.end(), 1), ei.end());
        }
    }
    EXPECT_TRUE(found_pair);
}

TEST(ClusterPage, WallPairGrouped) {
    // make_wall_cluster: two strokes with bbox gap 7.5 pts — within tolerance 10
    auto wall = make_wall_cluster();
    auto clusters = cluster_page(wall);
    ASSERT_EQ(clusters.size(), 1u);
    EXPECT_EQ(clusters[0].element_indices.size(), 2u);
}

// --- Weight band separation ---

TEST(ClusterPage, DifferentBandsNotGrouped) {
    StrokeWeightBands bands;
    bands.band_count    = 2;
    bands.thresholds[0] = 0.5f;  // band 0: width <= 0.5
    bands.thresholds[1] = 2.0f;  // band 1: 0.5 < width <= 2.0

    std::vector<ElementData> elems = {
        make_element(0,  0.0f, 0.0f, 10.0f, 2.0f, 0.3f),
        make_element(1, 12.0f, 0.0f, 10.0f, 2.0f, 1.5f),
    };
    auto clusters = cluster_page(elems, bands);
    EXPECT_EQ(clusters.size(), 2u);
}

TEST(ClusterPage, SameBandGrouped) {
    StrokeWeightBands bands;
    bands.band_count    = 2;
    bands.thresholds[0] = 0.5f;
    bands.thresholds[1] = 2.0f;

    std::vector<ElementData> elems = {
        make_element(0,  0.0f, 0.0f, 10.0f, 2.0f, 1.0f),
        make_element(1, 12.0f, 0.0f, 10.0f, 2.0f, 1.5f),
    };
    auto clusters = cluster_page(elems, bands);
    EXPECT_EQ(clusters.size(), 1u);
    EXPECT_EQ(clusters[0].element_indices.size(), 2u);
}

// --- Fill containment ---

TEST(ClusterPage, FillAbsorbedIntoOverlappingStrokeCluster) {
    auto wall = make_wall_cluster();  // two strokes at y=0..0.5 and y=8..8.5

    // Fill spanning the wall opening — overlaps the stroke cluster bbox
    ElementData fill{};
    fill.id           = 10;
    fill.bounds[0]    = 0.0f;
    fill.bounds[1]    = 0.0f;
    fill.bounds[2]    = 50.0f;
    fill.bounds[3]    = 10.0f;
    fill.fill_rgba    = 0xFF000000u;
    fill.is_filled    = true;
    fill.is_stroked   = false;
    fill.transform[0] = 1.0f; fill.transform[3] = 1.0f;
    wall.push_back(fill);

    auto clusters = cluster_page(wall);
    ASSERT_EQ(clusters.size(), 1u);
    EXPECT_EQ(clusters[0].element_indices.size(), 3u);
}

TEST(ClusterPage, IsolatedFillFormsSingletonCluster) {
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 10.0f, 2.0f, 0.5f),
    };

    ElementData fill{};
    fill.id           = 1;
    fill.bounds[0]    = 500.0f; fill.bounds[1] = 500.0f;
    fill.bounds[2]    = 510.0f; fill.bounds[3] = 510.0f;
    fill.fill_rgba    = 0xFF000000u;
    fill.is_filled    = true;
    fill.is_stroked   = false;
    fill.transform[0] = 1.0f; fill.transform[3] = 1.0f;
    elems.push_back(fill);

    auto clusters = cluster_page(elems);
    EXPECT_EQ(clusters.size(), 2u);
}

// --- Cluster IDs ---

TEST(ClusterPage, ClusterIdsAreStableIndexBased) {
    auto wall = make_wall_cluster();
    auto clusters = cluster_page(wall);
    for (int i = 0; i < static_cast<int>(clusters.size()); ++i) {
        EXPECT_EQ(clusters[i].cluster_id, i);
    }
}

// --- Lazy caching ---

TEST(ClusterCache, SameReferenceOnSecondCall) {
    ClusterCache cache;
    auto wall = make_wall_cluster();

    const auto& first  = cache.get_or_compute(0, wall);
    const auto& second = cache.get_or_compute(0, wall);
    EXPECT_EQ(&first, &second);
}

TEST(ClusterCache, InvalidateTriggersRecompute) {
    ClusterCache cache;
    auto wall = make_wall_cluster();

    size_t first_size = cache.get_or_compute(0, wall).size();
    cache.invalidate(0);
    size_t second_size = cache.get_or_compute(0, wall).size();
    EXPECT_EQ(first_size, second_size);
}

TEST(ClusterCache, InvalidateAllClearsAll) {
    ClusterCache cache;
    auto wall = make_wall_cluster();

    cache.get_or_compute(0, wall);
    cache.get_or_compute(1, wall);
    cache.invalidate_all();

    auto& result = cache.get_or_compute(0, wall);
    EXPECT_FALSE(result.empty());
=======
/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/* Build a Cluster that references all elements in `elems` (indices 0..N-1)
 * with bounds computed from the union of all element bounds. */
static Cluster make_cluster_from(const std::vector<ElementData>& elems)
{
    Cluster c;
    c.cluster_id = 0;

    float x0 =  std::numeric_limits<float>::max();
    float y0 =  std::numeric_limits<float>::max();
    float x1 = -std::numeric_limits<float>::max();
    float y1 = -std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < elems.size(); ++i) {
        c.element_indices.push_back(static_cast<int>(i));
        x0 = std::min(x0, elems[i].bounds[0]);
        y0 = std::min(y0, elems[i].bounds[1]);
        x1 = std::max(x1, elems[i].bounds[2]);
        y1 = std::max(y1, elems[i].bounds[3]);
    }

    c.bounds[0] = x0; c.bounds[1] = y0;
    c.bounds[2] = x1; c.bounds[3] = y1;
    return c;
}

static StrokeWeightBands no_bands() { return {}; }

/* ---------------------------------------------------------------------------
 * Dim 10 — ELEMENT_COUNT
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, ElementCountMatchesClusterSize)
{
    auto elems   = make_wall_cluster();   /* 2 strokes */
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[ELEMENT_COUNT], 2.0f);
}

TEST(FeatureVector, ElementCountFiveElements)
{
    std::vector<ElementData> elems;
    for (int i = 0; i < 5; ++i)
        elems.push_back(make_element(static_cast<uint32_t>(i),
                                     static_cast<float>(i * 10), 0.0f,
                                     8.0f, 1.0f, 0.5f));
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[ELEMENT_COUNT], 5.0f);
}

/* ---------------------------------------------------------------------------
 * Dim 7 — FILL_PRESENCE
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, FillPresenceOneWhenFilledElementPresent)
{
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 50.0f, 50.0f,
                     1.0f, 0xFF000000u, 0x0000FFFFu)  /* filled rect */
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[FILL_PRESENCE], 1.0f);
}

TEST(FeatureVector, FillPresenceZeroWhenNoFill)
{
    auto elems   = make_wall_cluster();  /* stroked only, fill_rgba = 0 */
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[FILL_PRESENCE], 0.0f);
}

/* ---------------------------------------------------------------------------
 * Dim 8 — ASPECT_RATIO
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, AspectRatioWidthOverHeight)
{
    /* Single element 100 wide × 20 tall → ratio = 5.0 */
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 100.0f, 20.0f, 1.0f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[ASPECT_RATIO], 5.0f, 0.001f);
}

TEST(FeatureVector, AspectRatioSquareIsOne)
{
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 60.0f, 60.0f, 1.0f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[ASPECT_RATIO], 1.0f, 0.001f);
}

/* ---------------------------------------------------------------------------
 * Dim 9 — BBOX_AREA
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, BBoxAreaIsWidthTimesHeight)
{
    /* Element 80 wide × 40 tall → area = 3200 */
    std::vector<ElementData> elems = {
        make_element(0, 10.0f, 10.0f, 80.0f, 40.0f, 1.0f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[BBOX_AREA], 80.0f * 40.0f, 0.01f);
}

TEST(FeatureVector, BBoxAreaSpansAllElements)
{
    /* Two elements forming a 200×20 bounding box */
    std::vector<ElementData> elems = {
        make_element(0,   0.0f, 0.0f, 100.0f, 20.0f, 0.5f),
        make_element(1, 100.0f, 0.0f, 100.0f, 20.0f, 0.5f),
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[BBOX_AREA], 200.0f * 20.0f, 0.01f);
}

/* ---------------------------------------------------------------------------
 * Dim 2 — STROKE_WEIGHT_MEAN
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, StrokeWeightMeanAveragesWidths)
{
    /* Two strokes: widths 0.5 and 1.5 → mean = 1.0 */
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 100.0f, 1.0f, 0.5f),
        make_element(1, 0.0f, 5.0f, 100.0f, 1.0f, 1.5f),
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[STROKE_WEIGHT_MEAN], 1.0f, 0.001f);
}

/* ---------------------------------------------------------------------------
 * Dim 0 — STROKE_LENGTH
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, StrokeLengthNormalized)
{
    /* Single horizontal element 500 pts wide → raw = 500, normalized = 0.5 */
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 500.0f, 1.0f, 0.5f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_NEAR(fv.dims[STROKE_LENGTH], 500.0f / FV_NORM_STROKE_LENGTH, 0.001f);
}

/* ---------------------------------------------------------------------------
 * Dim 4+5 — CLOSED_LOOP_COUNT / CLOSED_LOOP_AREA
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, ClosedLoopCountAndAreaForFilledElement)
{
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 60.0f, 40.0f,
                     1.0f, 0xFF000000u, 0x0000FFFFu)  /* 60×40 filled rect */
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[CLOSED_LOOP_COUNT], 1.0f);
    /* area = 60×40 = 2400, normalized by FV_NORM_LOOP_AREA */
    EXPECT_NEAR(fv.dims[CLOSED_LOOP_AREA], 2400.0f / FV_NORM_LOOP_AREA, 0.0001f);
}

TEST(FeatureVector, ClosedLoopCountZeroForStrokedOnly)
{
    auto elems   = make_wall_cluster();
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[CLOSED_LOOP_COUNT], 0.0f);
    EXPECT_FLOAT_EQ(fv.dims[CLOSED_LOOP_AREA],  0.0f);
}

/* ---------------------------------------------------------------------------
 * Dim 11 — PARALLEL_OFFSET
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, ParallelOffsetNonZeroForWallCluster)
{
    /* make_wall_cluster: two parallel horizontal strokes 8 pts apart */
    auto elems   = make_wall_cluster();
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    /* Normalized offset ≈ 8/100 = 0.08; just verify it's positive */
    EXPECT_GT(fv.dims[PARALLEL_OFFSET], 0.0f);
}

TEST(FeatureVector, ParallelOffsetZeroForSingleStroke)
{
    /* Only one stroke — no pair to form an offset */
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 200.0f, 1.0f, 0.5f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, no_bands());
    EXPECT_FLOAT_EQ(fv.dims[PARALLEL_OFFSET], 0.0f);
}

/* ---------------------------------------------------------------------------
 * Dim 3 — STROKE_WEIGHT_BAND (with calibrated bands)
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, StrokeWeightBandAssignedCorrectly)
{
    /* Two bands: [0..0.8] = band 0, [0.8..2.0] = band 1 */
    StrokeWeightBands bands;
    bands.band_count    = 2;
    bands.thresholds[0] = 0.8f;
    bands.thresholds[1] = 2.0f;

    /* Stroke width 0.5 → band 0 */
    std::vector<ElementData> elems = {
        make_element(0, 0.0f, 0.0f, 100.0f, 1.0f, 0.5f)
    };
    auto cluster = make_cluster_from(elems);
    auto fv      = compute_features(cluster, elems, bands);
    EXPECT_NEAR(fv.dims[STROKE_WEIGHT_BAND], 0.0f, 0.001f);

    /* Stroke width 1.5 → band 1 */
    std::vector<ElementData> elems2 = {
        make_element(0, 0.0f, 0.0f, 100.0f, 1.0f, 1.5f)
    };
    auto c2  = make_cluster_from(elems2);
    auto fv2 = compute_features(c2, elems2, bands);
    EXPECT_NEAR(fv2.dims[STROKE_WEIGHT_BAND], 1.0f, 0.001f);
}

/* ---------------------------------------------------------------------------
 * Edge case — empty cluster returns zero vector
 * -------------------------------------------------------------------------*/

TEST(FeatureVector, EmptyClusterReturnsZeroVector)
{
    Cluster empty;
    empty.cluster_id = 0;
    empty.bounds[0] = empty.bounds[1] = empty.bounds[2] = empty.bounds[3] = 0.0f;

    std::vector<ElementData> elems;
    auto fv = compute_features(empty, elems, no_bands());

    for (int d = 0; d < FEATURE_DIM_COUNT; ++d)
        EXPECT_FLOAT_EQ(fv.dims[d], 0.0f) << "dim " << d;
>>>>>>> 99573ee (feat: feature vector extraction — 12 dimensions per cluster (ce-082))
}

// ---------------------------------------------------------------------------
// Stroke weight histogram tests (SPEC-censor-core §2)
// ---------------------------------------------------------------------------

using namespace censor;

TEST(StrokeWeightHistogram, EmptyInputReturnsBandCountZero) {
    auto bands = analyze_stroke_weights({});
    EXPECT_EQ(bands.band_count, 0);
}

TEST(StrokeWeightHistogram, UniformWidthsReturnsSingleBand) {
    // All exactly the same — max-min = 0, single band.
    std::vector<float> widths(20, 0.5f);
    auto bands = analyze_stroke_weights(widths);
    EXPECT_EQ(bands.band_count, 1);
    EXPECT_GT(bands.thresholds[0], 0.5f);
}

TEST(StrokeWeightHistogram, SingleBandDrawingNarrowSpread) {
    // Slight variation within one bin width — still one band.
    std::vector<float> widths = {0.48f, 0.50f, 0.50f, 0.50f, 0.52f};
    auto bands = analyze_stroke_weights(widths);
    EXPECT_EQ(bands.band_count, 1);
}

TEST(StrokeWeightHistogram, ThreeBandAECDrawing) {
    // Three clearly-separated clusters: light (~0.3), medium (~1.0), heavy (~2.5).
    std::vector<float> widths;
    for (int i = 0; i < 10; ++i) widths.push_back(0.30f);
    for (int i = 0; i < 10; ++i) widths.push_back(1.00f);
    for (int i = 0; i < 10; ++i) widths.push_back(2.50f);

    auto bands = analyze_stroke_weights(widths);
    ASSERT_EQ(bands.band_count, 3);

    // Light band threshold sits between 0.30 and 1.00.
    EXPECT_GT(bands.thresholds[0], 0.30f);
    EXPECT_LT(bands.thresholds[0], 1.00f);

    // Medium band threshold sits between 1.00 and 2.50.
    EXPECT_GT(bands.thresholds[1], 1.00f);
    EXPECT_LT(bands.thresholds[1], 2.50f);

    // Final threshold is above the heaviest observed width.
    EXPECT_GT(bands.thresholds[2], 2.50f);
}

TEST(StrokeWeightHistogram, TwoBandDrawing) {
    // Two clusters: thin lines (~0.25) and thick walls (~1.5).
    std::vector<float> widths;
    for (int i = 0; i < 15; ++i) widths.push_back(0.25f);
    for (int i = 0; i < 15; ++i) widths.push_back(1.50f);

    auto bands = analyze_stroke_weights(widths);
    ASSERT_EQ(bands.band_count, 2);
    EXPECT_GT(bands.thresholds[0], 0.25f);
    EXPECT_LT(bands.thresholds[0], 1.50f);
    EXPECT_GT(bands.thresholds[1], 1.50f);
}

TEST(StrokeWeightHistogram, BandCountCappedAtFour) {
    // Five clusters — must be merged down to ≤4 bands.
    std::vector<float> widths;
    for (float center : {0.1f, 0.5f, 1.0f, 1.5f, 2.0f})
        for (int i = 0; i < 8; ++i) widths.push_back(center);

    auto bands = analyze_stroke_weights(widths);
    EXPECT_LE(bands.band_count, 4);
    EXPECT_GE(bands.band_count, 1);
}
