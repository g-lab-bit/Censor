#pragma once

#include "clustering/cluster.h"

#include <utility>
#include <vector>

namespace censor {

// Default number of grid divisions along the page width (placeholder).
inline constexpr int GRID_DEFAULT_DIVISIONS = 16;

// One cell in the sampling grid.
struct GridCell {
    int   row    = 0;
    int   col    = 0;
    float bounds[4] = {};             // x0, y0, x1, y1
    std::vector<int> cluster_indices; // indices into the clusters passed to map_clusters()
};

// Spatial sampling mesh that divides a page into approximately square cells.
//
// Lazy: cell ↔ cluster mapping is computed on the first query after
// map_clusters() is called. set_divisions() and map_clusters() both
// invalidate the computed mapping.
//
// Cross-boundary rule: a cluster is assigned to every cell its bounds
// overlap, so the same cluster may appear in several cells.
class GridSystem {
public:
    GridSystem(float page_width, float page_height,
               int divisions = GRID_DEFAULT_DIVISIONS);

    // Change the division count; invalidates the computed cell mapping.
    void set_divisions(int divisions);

    int   divisions() const noexcept { return divisions_; }
    int   rows()      const noexcept { return rows_; }
    int   cols()      const noexcept { return cols_; }
    float cell_width()  const noexcept { return cell_w_; }
    float cell_height() const noexcept { return cell_h_; }
    int   cell_count()  const noexcept { return rows_ * cols_; }

    // Store clusters for lazy assignment. Does not compute cell assignments yet.
    void map_clusters(const std::vector<Cluster>& clusters);

    // Return the cell at (row, col). Triggers mapping on first call.
    const GridCell& cell_at(int row, int col) const;

    // All (row, col) pairs that cluster at index `cluster_index` overlaps.
    // Triggers mapping on first call.
    std::vector<std::pair<int, int>> cells_for_cluster(int cluster_index) const;

private:
    void recompute_layout();
    void compute_mapping() const;

    float page_w_;
    float page_h_;
    int   divisions_;
    int   rows_ = 0;
    int   cols_ = 0;
    float cell_w_ = 0.0f;
    float cell_h_ = 0.0f;

    const std::vector<Cluster>* clusters_ = nullptr;

    mutable bool                                           mapped_ = false;
    mutable std::vector<GridCell>                          cells_;         // row-major, rows_ × cols_
    mutable std::vector<std::vector<std::pair<int, int>>>  cluster_cells_; // cluster_index → cell coords
};

} // namespace censor
