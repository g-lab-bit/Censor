#include "grid/grid_overlay.h"

#include <algorithm>
#include <cassert>
#include <map>

namespace censor {

GridOverlay::GridOverlay(float page_width, float page_height, int divisions)
    : grid_(page_width, page_height, divisions)
{}

void GridOverlay::set_divisions(int divisions)
{
    grid_.set_divisions(divisions);
    if (!clusters_.empty())
        grid_.map_clusters(clusters_);
    active_row_  = -1;
    active_col_  = -1;
    hovered_row_ = -1;
    hovered_col_ = -1;
}

int GridOverlay::divisions() const noexcept
{
    return grid_.divisions();
}

void GridOverlay::map_clusters(const std::vector<Cluster>&     clusters,
                               const std::vector<std::string>& labels)
{
    clusters_ = clusters;
    labels_   = labels;
    grid_.map_clusters(clusters_);
}

void GridOverlay::set_active_cell(int row, int col)
{
    active_row_ = row;
    active_col_ = col;
}

void GridOverlay::clear_active_cell()
{
    active_row_ = -1;
    active_col_ = -1;
}

void GridOverlay::set_hovered_cell(int row, int col)
{
    hovered_row_ = row;
    hovered_col_ = col;
}

void GridOverlay::clear_hovered_cell()
{
    hovered_row_ = -1;
    hovered_col_ = -1;
}

GridOverlayData GridOverlay::get_overlay_data() const
{
    GridOverlayData data;

    const int   rows   = grid_.rows();
    const int   cols   = grid_.cols();
    const float cell_w = grid_.cell_width();
    const float cell_h = grid_.cell_height();

    data.lines.page_width  = cell_w * static_cast<float>(cols);
    data.lines.page_height = cell_h * static_cast<float>(rows);

    /* Grid lines: one extra line at each boundary edge. */
    data.lines.v_lines.reserve(static_cast<std::size_t>(cols + 1));
    for (int c = 0; c <= cols; ++c)
        data.lines.v_lines.push_back(static_cast<float>(c) * cell_w);

    data.lines.h_lines.reserve(static_cast<std::size_t>(rows + 1));
    for (int r = 0; r <= rows; ++r)
        data.lines.h_lines.push_back(static_cast<float>(r) * cell_h);

    data.active_row = active_row_;
    data.active_col = active_col_;

    data.cells.reserve(static_cast<std::size_t>(rows * cols));

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            /* ce-m21: cell_at throws std::out_of_range for out-of-bounds indices.
             * This loop iterates r in [0, rows) and c in [0, cols) drawn directly
             * from grid_.rows()/grid_.cols() — the same values cell_at checks
             * against — so the call is provably in-bounds. The assert documents
             * this invariant; a future refactor that widens the range would catch
             * the violation at debug time rather than throwing in production. */
            assert(r >= 0 && r < rows && c >= 0 && c < cols);
            const GridCell& gc = grid_.cell_at(r, c);
            GridCellState   state;
            state.row          = r;
            state.col          = c;
            state.bounds[0]    = gc.bounds[0];
            state.bounds[1]    = gc.bounds[1];
            state.bounds[2]    = gc.bounds[2];
            state.bounds[3]    = gc.bounds[3];
            state.has_clusters = !gc.cluster_indices.empty();
            state.is_active    = (r == active_row_  && c == active_col_);
            state.is_hovered   = (r == hovered_row_ && c == hovered_col_);

            /* Dominant label: most frequent non-empty label in this cell. */
            if (state.has_clusters && !labels_.empty()) {
                std::map<std::string, int> freq;
                for (int idx : gc.cluster_indices) {
                    if (idx >= 0 &&
                        static_cast<std::size_t>(idx) < labels_.size() &&
                        !labels_[static_cast<std::size_t>(idx)].empty())
                    {
                        freq[labels_[static_cast<std::size_t>(idx)]]++;
                    }
                }
                if (!freq.empty()) {
                    state.dominant_label = std::max_element(
                        freq.begin(), freq.end(),
                        [](const auto& a, const auto& b) {
                            return a.second < b.second;
                        })->first;
                }
            }

            data.cells.push_back(std::move(state));
        }
    }

    return data;
}

CellPopoutData GridOverlay::get_popout_data() const
{
    CellPopoutData data;

    if (active_row_ < 0 || active_col_ < 0 ||
        active_row_ >= grid_.rows() || active_col_ >= grid_.cols())
        return data; /* valid = false */

    const GridCell& gc    = grid_.cell_at(active_row_, active_col_);
    data.valid            = true;
    data.row              = active_row_;
    data.col              = active_col_;
    data.cell_bounds[0]   = gc.bounds[0];
    data.cell_bounds[1]   = gc.bounds[1];
    data.cell_bounds[2]   = gc.bounds[2];
    data.cell_bounds[3]   = gc.bounds[3];

    for (int idx : gc.cluster_indices) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= clusters_.size())
            continue;

        const Cluster& cluster = clusters_[static_cast<std::size_t>(idx)];
        CellPopoutEntry entry;
        entry.cluster_id  = cluster.cluster_id;
        entry.bounds[0]   = cluster.bounds[0];
        entry.bounds[1]   = cluster.bounds[1];
        entry.bounds[2]   = cluster.bounds[2];
        entry.bounds[3]   = cluster.bounds[3];

        if (static_cast<std::size_t>(idx) < labels_.size())
            entry.label = labels_[static_cast<std::size_t>(idx)];

        entry.is_user_labeled = !entry.label.empty();
        entry.confidence      = entry.is_user_labeled ? 1.0f : 0.0f;

        data.clusters.push_back(std::move(entry));
    }

    return data;
}

} // namespace censor
