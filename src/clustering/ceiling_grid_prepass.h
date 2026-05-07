#pragma once

#include "censor_types.h"

#include <vector>

namespace censor {

/* detect_ceiling_grid — ceiling grid pre-pass (SPEC-censor-core.md §3).
 *
 * Determines whether two sets of line positions form a regular orthogonal
 * lattice. Both directions must be regular for is_grid to be true.
 *
 * positions are line coordinates in one axis — e.g. x-coords of vertical
 * lines for horizontal_positions, y-coords of horizontal lines for
 * vertical_positions. They need not be pre-sorted.
 *
 * Regularity: >= GRID_MIN_LINES positions in each direction, with at least
 * GRID_INLIER_FRACTION of consecutive spacings within GRID_REGULARITY_TOL
 * of the median spacing.
 *
 * On success, cell_width and cell_height are the mean inlier spacings in the
 * horizontal and vertical directions respectively.
 */
CeilingGridResult detect_ceiling_grid(
    const std::vector<float>& horizontal_positions,
    const std::vector<float>& vertical_positions) noexcept;

} /* namespace censor */
