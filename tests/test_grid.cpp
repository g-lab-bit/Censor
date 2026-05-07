#include <gtest/gtest.h>
#include "grid/grid_system.h"
#include "censor_types.h"

#include <cmath>

using namespace censor;

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

static Cluster make_cluster(int id, float x0, float y0, float x1, float y1)
{
    Cluster c;
    c.cluster_id  = id;
    c.bounds[0]   = x0;
    c.bounds[1]   = y0;
    c.bounds[2]   = x1;
    c.bounds[3]   = y1;
    return c;
}

/* ---------------------------------------------------------------------------
 * Layout
 * -------------------------------------------------------------------------*/

TEST(GridSystem, DefaultDivisionsConstantDefined)
{
    EXPECT_GT(GRID_DEFAULT_DIVISIONS, 0);
}

TEST(GridSystem, ColsEqualDivisions)
{
    GridSystem g(600.0f, 400.0f, 6);
    EXPECT_EQ(g.cols(), 6);
}

TEST(GridSystem, CellCountIsRowsTimesCols)
{
    GridSystem g(600.0f, 600.0f, 4);
    EXPECT_EQ(g.cell_count(), g.rows() * g.cols());
}

TEST(GridSystem, ApproximatelySquareCells)
{
    /* Square page → rows == cols. */
    GridSystem g(600.0f, 600.0f, 6);
    EXPECT_EQ(g.rows(), 6);
    EXPECT_EQ(g.cols(), 6);
    EXPECT_NEAR(g.cell_width(), g.cell_height(), 0.01f);
}

TEST(GridSystem, RectangularPageApproximateSquares)
{
    /* 1200 × 600 page, 6 divisions → cell_w = 200, rows ≈ 3. */
    GridSystem g(1200.0f, 600.0f, 6);
    EXPECT_EQ(g.cols(), 6);
    EXPECT_EQ(g.rows(), 3);
}

TEST(GridSystem, CellBoundsCorrect)
{
    /* 600 × 400 page, 3 cols → cell_w = 200.
     * rows = round(400 / 200) = 2, cell_h = 200. */
    GridSystem g(600.0f, 400.0f, 3);
    const GridCell& c = g.cell_at(0, 1);
    EXPECT_FLOAT_EQ(c.bounds[0], 200.0f);
    EXPECT_FLOAT_EQ(c.bounds[2], 400.0f);
}

/* ---------------------------------------------------------------------------
 * set_divisions
 * -------------------------------------------------------------------------*/

TEST(GridSystem, SetDivisionsUpdatesCols)
{
    GridSystem g(600.0f, 600.0f, 4);
    EXPECT_EQ(g.cols(), 4);
    g.set_divisions(8);
    EXPECT_EQ(g.cols(), 8);
}

TEST(GridSystem, SetDivisionsRemapsClusterAssignments)
{
    /* One cluster covering the whole left half of a 400×400 page. */
    std::vector<Cluster> clusters = { make_cluster(0, 0.0f, 0.0f, 200.0f, 400.0f) };

    GridSystem g(400.0f, 400.0f, 2);
    g.map_clusters(clusters);

    /* With 2 divisions the cluster occupies column 0 only. */
    auto cells2 = g.cells_for_cluster(0);
    for (auto& [r, c] : cells2) {
        EXPECT_EQ(c, 0);
    }

    /* Double the divisions — cluster still occupies only the left columns. */
    g.set_divisions(4);
    g.map_clusters(clusters);
    auto cells4 = g.cells_for_cluster(0);
    for (auto& [r, c] : cells4) {
        EXPECT_LT(c, 2); // left half = cols 0 and 1
    }
}

/* ---------------------------------------------------------------------------
 * Cluster mapping
 * -------------------------------------------------------------------------*/

TEST(GridSystem, SingleClusterFullyCoveredByCell)
{
    /* 600×600 page, 6 divisions → 36 cells of 100×100.
     * Cluster fits entirely inside cell (1,1). */
    GridSystem g(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(0, 110.0f, 110.0f, 190.0f, 190.0f) };
    g.map_clusters(clusters);

    const GridCell& cell = g.cell_at(1, 1);
    ASSERT_EQ(cell.cluster_indices.size(), 1u);
    EXPECT_EQ(cell.cluster_indices[0], 0);

    auto cf = g.cells_for_cluster(0);
    ASSERT_EQ(cf.size(), 1u);
    EXPECT_EQ(cf[0].first,  1);
    EXPECT_EQ(cf[0].second, 1);
}

TEST(GridSystem, CrossBoundaryClustersAssignedToMultipleCells)
{
    /* 600×600 page, 6 divisions → cells of 100×100.
     * Cluster straddles the boundary between cells (0,1) and (0,2). */
    GridSystem g(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(0, 150.0f, 10.0f, 250.0f, 90.0f) };
    g.map_clusters(clusters);

    auto cf = g.cells_for_cluster(0);
    EXPECT_GE(cf.size(), 2u);

    /* Both col 1 and col 2 must appear. */
    bool has_col1 = false, has_col2 = false;
    for (auto& [r, c] : cf) {
        if (c == 1) has_col1 = true;
        if (c == 2) has_col2 = true;
    }
    EXPECT_TRUE(has_col1);
    EXPECT_TRUE(has_col2);
}

TEST(GridSystem, MultipleClustersInSameCell)
{
    GridSystem g(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = {
        make_cluster(0, 10.0f, 10.0f, 40.0f, 40.0f),
        make_cluster(1, 50.0f, 50.0f, 90.0f, 90.0f),
    };
    g.map_clusters(clusters);

    const GridCell& cell = g.cell_at(0, 0);
    EXPECT_EQ(cell.cluster_indices.size(), 2u);
}

TEST(GridSystem, NoClustersLeavesEmptyCells)
{
    GridSystem g(600.0f, 600.0f, 4);
    std::vector<Cluster> empty;
    g.map_clusters(empty);

    /* Querying must not crash; cell indices should be empty. */
    const GridCell& cell = g.cell_at(0, 0);
    EXPECT_TRUE(cell.cluster_indices.empty());
}

/* ---------------------------------------------------------------------------
 * Lazy evaluation
 * -------------------------------------------------------------------------*/

TEST(GridSystem, SecondCellAtCallReturnsSameObject)
{
    /* Verifies that cell_at caches its result (lazy compute once). */
    GridSystem g(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(0, 10.0f, 10.0f, 90.0f, 90.0f) };
    g.map_clusters(clusters);

    const GridCell& first  = g.cell_at(0, 0);
    const GridCell& second = g.cell_at(0, 0);
    EXPECT_EQ(&first, &second);
}

TEST(GridSystem, RemapAfterSetDivisionsYieldsCorrectData)
{
    GridSystem g(600.0f, 600.0f, 3);
    std::vector<Cluster> clusters = { make_cluster(0, 10.0f, 10.0f, 190.0f, 190.0f) };
    g.map_clusters(clusters);

    /* Trigger first compute. */
    g.cell_at(0, 0);

    /* Change divisions and remap — old cached data must be gone. */
    g.set_divisions(6);
    g.map_clusters(clusters);

    /* Cell (0,0) is now 100×100; cluster still overlaps it. */
    const GridCell& cell = g.cell_at(0, 0);
    EXPECT_FALSE(cell.cluster_indices.empty());
}
