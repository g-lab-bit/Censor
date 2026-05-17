#include <gtest/gtest.h>
#include "grid/grid_system.h"
#include "grid/grid_overlay.h"
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

/* ===========================================================================
 * GridOverlay tests
 * =========================================================================*/

/* ---------------------------------------------------------------------------
 * Grid line generation
 * -------------------------------------------------------------------------*/

TEST(GridOverlay, OverlayDataHasCorrectLineCount)
{
    /* 600×600 page, 3 divisions → 3 cols, 3 rows → 4 v-lines + 4 h-lines. */
    GridOverlay ov(600.0f, 600.0f, 3);
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.lines.v_lines.size(), 4u);
    EXPECT_EQ(data.lines.h_lines.size(), 4u);
}

TEST(GridOverlay, GridLinesSpanFullPage)
{
    GridOverlay ov(600.0f, 400.0f, 3);
    auto data = ov.get_overlay_data();
    EXPECT_FLOAT_EQ(data.lines.v_lines.front(), 0.0f);
    EXPECT_FLOAT_EQ(data.lines.v_lines.back(),  600.0f);
    EXPECT_FLOAT_EQ(data.lines.h_lines.front(), 0.0f);
    EXPECT_FLOAT_EQ(data.lines.h_lines.back(),  data.lines.page_height);
}

TEST(GridOverlay, CellCountMatchesRowsTimesCols)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    auto data = ov.get_overlay_data();
    /* Square page → 4×4 = 16 cells. */
    EXPECT_EQ(data.cells.size(), 16u);
}

/* ---------------------------------------------------------------------------
 * Active / hover cell state
 * -------------------------------------------------------------------------*/

TEST(GridOverlay, NoActiveCellByDefault)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.active_row, -1);
    EXPECT_EQ(data.active_col, -1);
    for (const auto& cell : data.cells)
        EXPECT_FALSE(cell.is_active);
}

TEST(GridOverlay, SetActiveCellMarksCorrectCell)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_active_cell(1, 2);
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.active_row, 1);
    EXPECT_EQ(data.active_col, 2);

    int active_count = 0;
    for (const auto& cell : data.cells) {
        if (cell.is_active) {
            EXPECT_EQ(cell.row, 1);
            EXPECT_EQ(cell.col, 2);
            ++active_count;
        }
    }
    EXPECT_EQ(active_count, 1);
}

TEST(GridOverlay, ClearActiveCellRemovesHighlight)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_active_cell(0, 0);
    ov.clear_active_cell();
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.active_row, -1);
    for (const auto& cell : data.cells)
        EXPECT_FALSE(cell.is_active);
}

TEST(GridOverlay, HoveredCellMarked)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_hovered_cell(0, 3);
    auto data = ov.get_overlay_data();
    int hovered_count = 0;
    for (const auto& cell : data.cells) {
        if (cell.is_hovered) {
            EXPECT_EQ(cell.row, 0);
            EXPECT_EQ(cell.col, 3);
            ++hovered_count;
        }
    }
    EXPECT_EQ(hovered_count, 1);
}

TEST(GridOverlay, ClearHoveredCellWorks)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_hovered_cell(1, 1);
    ov.clear_hovered_cell();
    auto data = ov.get_overlay_data();
    for (const auto& cell : data.cells)
        EXPECT_FALSE(cell.is_hovered);
}

/* ---------------------------------------------------------------------------
 * Cluster mapping and dominant label
 * -------------------------------------------------------------------------*/

TEST(GridOverlay, NoClustersAllCellsHaveNone)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    auto data = ov.get_overlay_data();
    for (const auto& cell : data.cells) {
        EXPECT_FALSE(cell.has_clusters);
        EXPECT_TRUE(cell.dominant_label.empty());
    }
}

TEST(GridOverlay, ClusterInCellSetsHasClusters)
{
    GridOverlay ov(600.0f, 600.0f, 6);
    /* 600×600 / 6 → 100×100 cells; cluster in top-left cell. */
    std::vector<Cluster> clusters = { make_cluster(0, 10.0f, 10.0f, 90.0f, 90.0f) };
    std::vector<std::string> labels = { "" };
    ov.map_clusters(clusters, labels);

    auto data = ov.get_overlay_data();
    EXPECT_TRUE(data.cells[0].has_clusters); /* cell (0,0) */
}

TEST(GridOverlay, LabeledClusterSetsDominantLabel)
{
    GridOverlay ov(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(0, 10.0f, 10.0f, 90.0f, 90.0f) };
    std::vector<std::string> labels = { "wall" };
    ov.map_clusters(clusters, labels);

    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.cells[0].dominant_label, "wall");
}

TEST(GridOverlay, MostFrequentLabelWins)
{
    /* 600×600 / 6 → 100×100 cells; three clusters in cell (0,0).
     * Two labeled "duct", one labeled "wall" → dominant = "duct". */
    GridOverlay ov(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = {
        make_cluster(0, 10.0f, 10.0f, 40.0f, 40.0f),
        make_cluster(1, 50.0f, 10.0f, 80.0f, 40.0f),
        make_cluster(2, 10.0f, 50.0f, 40.0f, 80.0f),
    };
    std::vector<std::string> labels = { "duct", "duct", "wall" };
    ov.map_clusters(clusters, labels);

    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.cells[0].dominant_label, "duct");
}

TEST(GridOverlay, UnlabeledClustersNoDominantLabel)
{
    GridOverlay ov(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(0, 10.0f, 10.0f, 90.0f, 90.0f) };
    std::vector<std::string> labels = { "" };
    ov.map_clusters(clusters, labels);

    auto data = ov.get_overlay_data();
    EXPECT_TRUE(data.cells[0].has_clusters);
    EXPECT_TRUE(data.cells[0].dominant_label.empty());
}

/* ---------------------------------------------------------------------------
 * set_divisions resets state
 * -------------------------------------------------------------------------*/

TEST(GridOverlay, SetDivisionsReturnsNewDivisionCount)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    EXPECT_EQ(ov.divisions(), 4);
    ov.set_divisions(8);
    EXPECT_EQ(ov.divisions(), 8);
}

TEST(GridOverlay, SetDivisionsResetsActiveCell)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_active_cell(1, 1);
    ov.set_divisions(8);
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.active_row, -1);
    EXPECT_EQ(data.active_col, -1);
}

TEST(GridOverlay, SetDivisionsUpdatesLineCount)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_divisions(6);
    auto data = ov.get_overlay_data();
    EXPECT_EQ(data.lines.v_lines.size(), 7u); /* 6 cols + 1 */
}

/* ---------------------------------------------------------------------------
 * Cell popout window
 * -------------------------------------------------------------------------*/

TEST(GridOverlay, PopoutInvalidWithNoActiveCell)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    EXPECT_FALSE(ov.get_popout_data().valid);
}

TEST(GridOverlay, PopoutValidAfterSetActiveCell)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_active_cell(0, 0);
    EXPECT_TRUE(ov.get_popout_data().valid);
}

TEST(GridOverlay, PopoutInvalidAfterClear)
{
    GridOverlay ov(600.0f, 600.0f, 4);
    ov.set_active_cell(0, 0);
    ov.clear_active_cell();
    EXPECT_FALSE(ov.get_popout_data().valid);
}

TEST(GridOverlay, PopoutCarriesRowColAndBounds)
{
    /* 600×600 / 6 → 100×100 cells. Cell (1,2) starts at x=200, y=100. */
    GridOverlay ov(600.0f, 600.0f, 6);
    ov.set_active_cell(1, 2);
    auto pd = ov.get_popout_data();
    ASSERT_TRUE(pd.valid);
    EXPECT_EQ(pd.row, 1);
    EXPECT_EQ(pd.col, 2);
    EXPECT_FLOAT_EQ(pd.cell_bounds[0], 200.0f);
    EXPECT_FLOAT_EQ(pd.cell_bounds[1], 100.0f);
    EXPECT_FLOAT_EQ(pd.cell_bounds[2], 300.0f);
    EXPECT_FLOAT_EQ(pd.cell_bounds[3], 200.0f);
}

TEST(GridOverlay, PopoutContainsClustersInActiveCell)
{
    /* Cluster fits entirely inside cell (0,0). */
    GridOverlay ov(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(7, 10.0f, 10.0f, 90.0f, 90.0f) };
    std::vector<std::string> labels = { "pipe" };
    ov.map_clusters(clusters, labels);
    ov.set_active_cell(0, 0);

    auto pd = ov.get_popout_data();
    ASSERT_TRUE(pd.valid);
    ASSERT_EQ(pd.clusters.size(), 1u);
    EXPECT_EQ(pd.clusters[0].cluster_id, 7);
    EXPECT_EQ(pd.clusters[0].label, "pipe");
    EXPECT_TRUE(pd.clusters[0].is_user_labeled);
    EXPECT_FLOAT_EQ(pd.clusters[0].confidence, 1.0f);
}

TEST(GridOverlay, PopoutEmptyCellHasNoClusters)
{
    GridOverlay ov(600.0f, 600.0f, 6);
    /* No clusters mapped — active cell is empty. */
    ov.set_active_cell(0, 0);
    auto pd = ov.get_popout_data();
    ASSERT_TRUE(pd.valid);
    EXPECT_TRUE(pd.clusters.empty());
}

TEST(GridOverlay, UnlabeledClusterInPopoutNotUserLabeled)
{
    GridOverlay ov(600.0f, 600.0f, 6);
    std::vector<Cluster> clusters = { make_cluster(3, 10.0f, 10.0f, 90.0f, 90.0f) };
    std::vector<std::string> labels = { "" };
    ov.map_clusters(clusters, labels);
    ov.set_active_cell(0, 0);

    auto pd = ov.get_popout_data();
    ASSERT_EQ(pd.clusters.size(), 1u);
    EXPECT_TRUE(pd.clusters[0].label.empty());
    EXPECT_FALSE(pd.clusters[0].is_user_labeled);
    EXPECT_FLOAT_EQ(pd.clusters[0].confidence, 0.0f);
}
