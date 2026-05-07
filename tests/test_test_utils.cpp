#include "test_utils.h"
#include <gtest/gtest.h>

using namespace censor;
using namespace censor::test;

/* ---------------------------------------------------------------------------
 * make_element
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeElementBounds)
{
    auto e = make_element(7u, 10.0f, 20.0f, 100.0f, 50.0f, 1.5f, 0xFF0000FFu, 0u);
    EXPECT_EQ(e.id, 7u);
    EXPECT_FLOAT_EQ(e.bounds[0], 10.0f);
    EXPECT_FLOAT_EQ(e.bounds[1], 20.0f);
    EXPECT_FLOAT_EQ(e.bounds[2], 110.0f);
    EXPECT_FLOAT_EQ(e.bounds[3], 70.0f);
    EXPECT_FLOAT_EQ(e.stroke_width, 1.5f);
    EXPECT_TRUE(e.is_stroked);
    EXPECT_FALSE(e.is_filled);
}

TEST(TestUtils, MakeElementIdentityTransform)
{
    auto e = make_element(0u, 0.0f, 0.0f, 10.0f, 10.0f, 0.5f);
    EXPECT_FLOAT_EQ(e.transform[0], 1.0f);
    EXPECT_FLOAT_EQ(e.transform[1], 0.0f);
    EXPECT_FLOAT_EQ(e.transform[2], 0.0f);
    EXPECT_FLOAT_EQ(e.transform[3], 1.0f);
    EXPECT_FLOAT_EQ(e.transform[4], 0.0f);
    EXPECT_FLOAT_EQ(e.transform[5], 0.0f);
}

TEST(TestUtils, MakeElementFilledWhenFillSet)
{
    auto e = make_element(0u, 0.0f, 0.0f, 10.0f, 10.0f, 0.5f, 0u, 0xFFFFFFFFu);
    EXPECT_TRUE(e.is_filled);
    EXPECT_EQ(e.fill_rgba, 0xFFFFFFFFu);
}

/* ---------------------------------------------------------------------------
 * make_element_grid
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeElementGridCount)
{
    auto grid = make_element_grid(3, 4, 60.0f, 0.5f);
    EXPECT_EQ(grid.size(), 12u);
}

TEST(TestUtils, MakeElementGridPositions)
{
    auto grid = make_element_grid(2, 2, 100.0f, 1.0f);
    /* (row=0, col=0) → x=0, y=0 */
    EXPECT_FLOAT_EQ(grid[0].bounds[0], 0.0f);
    EXPECT_FLOAT_EQ(grid[0].bounds[1], 0.0f);
    /* (row=0, col=1) → x=100, y=0 */
    EXPECT_FLOAT_EQ(grid[1].bounds[0], 100.0f);
    EXPECT_FLOAT_EQ(grid[1].bounds[1], 0.0f);
    /* (row=1, col=0) → x=0, y=100 */
    EXPECT_FLOAT_EQ(grid[2].bounds[0], 0.0f);
    EXPECT_FLOAT_EQ(grid[2].bounds[1], 100.0f);
}

TEST(TestUtils, MakeElementGridUniformSpacing)
{
    const float spacing = 60.0f;
    auto grid = make_element_grid(1, 5, spacing, 0.5f);
    for (size_t i = 0; i + 1 < grid.size(); ++i) {
        float dx = grid[i + 1].bounds[0] - grid[i].bounds[0];
        EXPECT_FLOAT_EQ(dx, spacing);
    }
}

/* ---------------------------------------------------------------------------
 * make_wall_cluster
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeWallClusterMinimumStrokes)
{
    auto wall = make_wall_cluster();
    EXPECT_GE(wall.size(), 2u);
}

TEST(TestUtils, MakeWallClusterElongated)
{
    auto wall = make_wall_cluster();
    for (const auto& e : wall) {
        float w = e.bounds[2] - e.bounds[0];
        float h = e.bounds[3] - e.bounds[1];
        /* Wall strokes are much longer than they are wide. */
        EXPECT_GT(w, h);
    }
}

TEST(TestUtils, MakeWallClusterConsistentStrokeWeight)
{
    auto wall = make_wall_cluster();
    ASSERT_GE(wall.size(), 2u);
    float sw0 = wall[0].stroke_width;
    for (const auto& e : wall) {
        EXPECT_FLOAT_EQ(e.stroke_width, sw0);
    }
}

/* ---------------------------------------------------------------------------
 * make_duct_cluster
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeDuctClusterNonEmpty)
{
    auto duct = make_duct_cluster();
    EXPECT_GE(duct.size(), 1u);
}

TEST(TestUtils, MakeDuctClusterMediumStroke)
{
    auto duct = make_duct_cluster();
    for (const auto& e : duct) {
        /* Ducts use heavier strokes than walls (> 0.5). */
        EXPECT_GT(e.stroke_width, 0.5f);
    }
}

TEST(TestUtils, MakeDuctClusterNonDegenerateArea)
{
    auto duct = make_duct_cluster();
    for (const auto& e : duct) {
        float w = e.bounds[2] - e.bounds[0];
        float h = e.bounds[3] - e.bounds[1];
        EXPECT_GT(w * h, 0.0f);
    }
}

/* ---------------------------------------------------------------------------
 * make_random_elements
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeRandomElementsCount)
{
    auto elems = make_random_elements(50, 42u);
    EXPECT_EQ(elems.size(), 50u);
}

TEST(TestUtils, MakeRandomElementsDeterministic)
{
    auto a = make_random_elements(10, 99u);
    auto b = make_random_elements(10, 99u);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i].bounds[0], b[i].bounds[0]);
        EXPECT_FLOAT_EQ(a[i].bounds[1], b[i].bounds[1]);
        EXPECT_FLOAT_EQ(a[i].bounds[2], b[i].bounds[2]);
        EXPECT_FLOAT_EQ(a[i].bounds[3], b[i].bounds[3]);
        EXPECT_FLOAT_EQ(a[i].stroke_width, b[i].stroke_width);
    }
}

TEST(TestUtils, MakeRandomElementsPositiveDimensions)
{
    auto elems = make_random_elements(100, 7u);
    for (const auto& e : elems) {
        EXPECT_GT(e.bounds[2] - e.bounds[0], 0.0f);
        EXPECT_GT(e.bounds[3] - e.bounds[1], 0.0f);
        EXPECT_GT(e.stroke_width, 0.0f);
    }
}

TEST(TestUtils, MakeRandomElementsDifferentSeeds)
{
    auto a = make_random_elements(5, 1u);
    auto b = make_random_elements(5, 2u);
    /* Different seeds should produce different first elements. */
    EXPECT_NE(a[0].bounds[0], b[0].bounds[0]);
}

/* ---------------------------------------------------------------------------
 * make_feature_vector
 * -------------------------------------------------------------------------*/

TEST(TestUtils, MakeFeatureVectorWallElongated)
{
    auto fv = make_feature_vector("wall");
    EXPECT_GT(fv.dims[ASPECT_RATIO], 1.0f);  /* elongated */
    EXPECT_EQ(fv.dims[FILL_PRESENCE], 0.0f); /* not filled */
    EXPECT_GT(fv.dims[STROKE_LENGTH], 0.0f);
}

TEST(TestUtils, MakeFeatureVectorDuctHasClosedLoops)
{
    auto fv = make_feature_vector("duct");
    EXPECT_GT(fv.dims[CLOSED_LOOP_COUNT], 0.0f);
    EXPECT_GT(fv.dims[STROKE_WEIGHT_MEAN], 0.5f); /* medium weight */
}

TEST(TestUtils, MakeFeatureVectorPipeVertical)
{
    auto fv = make_feature_vector("pipe");
    /* Vertical orientation ≈ π/2 ≈ 1.5708 */
    EXPECT_GT(fv.dims[DOMINANT_ORIENTATION], 1.0f);
    EXPECT_LT(fv.dims[ASPECT_RATIO], 1.0f); /* tall, narrow */
}

TEST(TestUtils, MakeFeatureVectorFurnitureFilled)
{
    auto fv = make_feature_vector("furniture");
    EXPECT_EQ(fv.dims[FILL_PRESENCE], 1.0f);
}

TEST(TestUtils, MakeFeatureVectorCeilingGridDense)
{
    auto fv = make_feature_vector("ceiling_grid");
    EXPECT_GT(fv.dims[ADJACENT_CONNECTIVITY], 10.0f);
    EXPECT_GT(fv.dims[ELEMENT_COUNT], 10.0f);
}

TEST(TestUtils, MakeFeatureVectorUnknownLabelZero)
{
    auto fv = make_feature_vector("unknown_class_xyz");
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        EXPECT_FLOAT_EQ(fv.dims[i], 0.0f) << "dim " << i << " should be 0";
    }
}
