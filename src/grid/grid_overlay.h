#pragma once

#include "grid/grid_system.h"
#include "censor_types.h"

#include <string>
#include <vector>

namespace censor {

/* ---------------------------------------------------------------------------
 * GridLineSet — page-space coordinates for the grid lines Rapida draws.
 * Lines are 1-px screen-space (Rapida keeps them zoom-independent).
 * -------------------------------------------------------------------------*/
struct GridLineSet {
    std::vector<float> h_lines;   /* y-coordinates of horizontal lines */
    std::vector<float> v_lines;   /* x-coordinates of vertical lines   */
    float page_width  = 0.0f;
    float page_height = 0.0f;
};

/* ---------------------------------------------------------------------------
 * GridCellState — per-cell render state returned in GridOverlayData.
 *
 * dominant_label: most frequent non-empty label among clusters in this cell,
 * "" when the cell has no labeled clusters.  Rapida uses it for tint color.
 * -------------------------------------------------------------------------*/
struct GridCellState {
    int   row          = 0;
    int   col          = 0;
    float bounds[4]    = {};      /* x0, y0, x1, y1 in page space */
    bool  has_clusters = false;
    bool  is_active    = false;   /* cell was clicked by the user */
    bool  is_hovered   = false;
    std::string dominant_label;   /* "" if unclassified */
};

/* ---------------------------------------------------------------------------
 * GridOverlayData — full snapshot for Rapida to render the grid overlay.
 * -------------------------------------------------------------------------*/
struct GridOverlayData {
    GridLineSet               lines;
    std::vector<GridCellState> cells;
    int                        active_row = -1;
    int                        active_col = -1;
};

/* ---------------------------------------------------------------------------
 * CellPopoutEntry — one cluster inside the popout window.
 * -------------------------------------------------------------------------*/
struct CellPopoutEntry {
    int         cluster_id      = -1;
    float       bounds[4]       = {};  /* x0, y0, x1, y1 in page space */
    std::string label;                 /* "" if unlabeled */
    float       confidence      = 0.0f;
    bool        is_user_labeled = false;
};

/* ---------------------------------------------------------------------------
 * CellPopoutData — content for the popout window that opens on cell click.
 *
 * valid is false when no active cell is set; Rapida must not render the
 * popout in that case.
 * -------------------------------------------------------------------------*/
struct CellPopoutData {
    bool                         valid       = false;
    int                          row         = -1;
    int                          col         = -1;
    float                        cell_bounds[4] = {};
    std::vector<CellPopoutEntry> clusters;
};

/* ---------------------------------------------------------------------------
 * GridOverlay — overlay data provider that Rapida queries.
 *
 * Wraps GridSystem with UI state (active / hovered cell) and per-cluster
 * label data.  Rapida calls get_overlay_data() every frame and
 * get_popout_data() when a cell is active.
 *
 * Thread note: not thread-safe.  All calls must come from the UI thread.
 * -------------------------------------------------------------------------*/
class GridOverlay {
public:
    GridOverlay(float page_width, float page_height,
                int divisions = GRID_DEFAULT_DIVISIONS);

    /* Change grid density; resets active/hover state. */
    void set_divisions(int divisions);
    int  divisions() const noexcept;

    /* Supply clusters and their current labels.
     * labels[i] is the user label for clusters[i]; "" means unlabeled.
     * labels may be shorter than clusters — excess clusters treated as "". */
    void map_clusters(const std::vector<Cluster>&     clusters,
                      const std::vector<std::string>& labels);

    /* Set / clear which cell is active (clicked). (-1,-1) = none. */
    void set_active_cell(int row, int col);
    void clear_active_cell();

    /* Set / clear which cell is hovered. (-1,-1) = none. */
    void set_hovered_cell(int row, int col);
    void clear_hovered_cell();

    /* Snapshot for rendering the full grid overlay. */
    GridOverlayData get_overlay_data() const;

    /* Snapshot for the popout window; valid=false if no active cell. */
    CellPopoutData  get_popout_data()  const;

private:
    GridSystem               grid_;
    std::vector<Cluster>     clusters_;
    std::vector<std::string> labels_;
    int                      active_row_  = -1;
    int                      active_col_  = -1;
    int                      hovered_row_ = -1;
    int                      hovered_col_ = -1;
};

} // namespace censor
