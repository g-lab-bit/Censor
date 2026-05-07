#include <gtest/gtest.h>
#include "clustering/spatial_clustering.h"
#include "clustering/stroke_weight_histogram.h"
#include "test_utils.h"

#include <algorithm>

using namespace censor;
using namespace censor::test;

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
