#include "grid/grid_system.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace censor {

static int clamp_divisions(int d) noexcept
{
    return d < 1 ? 1 : d;
}

GridSystem::GridSystem(float page_width, float page_height, int divisions)
    : page_w_(page_width)
    , page_h_(page_height)
    , divisions_(clamp_divisions(divisions))
{
    recompute_layout();
}

void GridSystem::set_divisions(int divisions)
{
    divisions_ = clamp_divisions(divisions);
    recompute_layout();
    mapped_       = false;
    cluster_cells_.clear();
    cells_.clear();
}

void GridSystem::map_clusters(const std::vector<Cluster>& clusters)
{
    clusters_ = &clusters;
    mapped_       = false;
    cluster_cells_.clear();
    cells_.clear();
}

const GridCell& GridSystem::cell_at(int row, int col) const
{
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        throw std::out_of_range("GridSystem::cell_at: out of range");
    }
    compute_mapping();
    return cells_[static_cast<std::size_t>(row * cols_ + col)];
}

std::vector<std::pair<int, int>> GridSystem::cells_for_cluster(int cluster_index) const
{
    compute_mapping();
    if (cluster_index < 0 ||
        static_cast<std::size_t>(cluster_index) >= cluster_cells_.size()) {
        return {};
    }
    return cluster_cells_[static_cast<std::size_t>(cluster_index)];
}

void GridSystem::recompute_layout()
{
    cols_   = divisions_;
    cell_w_ = page_w_ / static_cast<float>(cols_);

    // Choose rows so cells are approximately square.
    if (cell_w_ > 0.0f) {
        int computed = static_cast<int>(std::round(page_h_ / cell_w_));
        rows_ = computed < 1 ? 1 : computed;
    } else {
        rows_ = 1;
    }

    cell_h_ = page_h_ / static_cast<float>(rows_);
}

void GridSystem::compute_mapping() const
{
    if (mapped_) {
        return;
    }

    // Build the cell grid (no cluster data yet).
    cells_.resize(static_cast<std::size_t>(rows_ * cols_));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            GridCell& cell = cells_[static_cast<std::size_t>(r * cols_ + c)];
            cell.row      = r;
            cell.col      = c;
            cell.bounds[0] = static_cast<float>(c) * cell_w_;
            cell.bounds[1] = static_cast<float>(r) * cell_h_;
            cell.bounds[2] = cell.bounds[0] + cell_w_;
            cell.bounds[3] = cell.bounds[1] + cell_h_;
            cell.cluster_indices.clear();
        }
    }

    if (clusters_ == nullptr || clusters_->empty()) {
        mapped_ = true;
        return;
    }

    const std::size_t n = clusters_->size();
    cluster_cells_.assign(n, {});

    for (std::size_t ci = 0; ci < n; ++ci) {
        const Cluster& cl = (*clusters_)[ci];
        const float cx0 = cl.bounds[0], cy0 = cl.bounds[1];
        const float cx1 = cl.bounds[2], cy1 = cl.bounds[3];

        // Find row range that overlaps [cy0, cy1).
        int r0 = static_cast<int>(std::floor(cy0 / cell_h_));
        int r1 = static_cast<int>(std::ceil (cy1 / cell_h_));
        r0 = std::max(r0, 0);
        r1 = std::min(r1, rows_);

        // Find col range that overlaps [cx0, cx1).
        int c0 = static_cast<int>(std::floor(cx0 / cell_w_));
        int c1 = static_cast<int>(std::ceil (cx1 / cell_w_));
        c0 = std::max(c0, 0);
        c1 = std::min(c1, cols_);

        for (int r = r0; r < r1; ++r) {
            for (int c = c0; c < c1; ++c) {
                GridCell& cell = cells_[static_cast<std::size_t>(r * cols_ + c)];
                cell.cluster_indices.push_back(static_cast<int>(ci));
                cluster_cells_[ci].emplace_back(r, c);
            }
        }
    }

    mapped_ = true;
}

} // namespace censor
