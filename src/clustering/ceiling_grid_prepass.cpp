#include "clustering/ceiling_grid_prepass.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace censor {

/* Minimum number of line positions needed to detect a pattern (>=2 spacings). */
static constexpr int   GRID_MIN_LINES       = 3;
/* Maximum fractional deviation from median spacing that counts as an inlier. */
static constexpr float GRID_REGULARITY_TOL  = 0.05f;
/* Minimum fraction of spacings that must be inliers to declare regularity. */
static constexpr float GRID_INLIER_FRACTION = 0.67f;

/* Returns true when sorted positions form a regular sequence.
 * Writes the mean inlier spacing to out_spacing on success. */
static bool is_regular(std::vector<float> positions, float& out_spacing) noexcept
{
    std::sort(positions.begin(), positions.end());

    /* Build consecutive spacings, skipping duplicates. */
    std::vector<float> spacings;
    spacings.reserve(positions.size());
    for (std::size_t i = 1; i < positions.size(); ++i) {
        float s = positions[i] - positions[i - 1];
        if (s > 0.0f) {
            spacings.push_back(s);
        }
    }

    if (static_cast<int>(spacings.size()) < GRID_MIN_LINES - 1) {
        return false;
    }

    /* Median spacing as the reference — robust to a few outliers. */
    std::vector<float> sc = spacings;
    std::size_t mid = sc.size() / 2;
    std::nth_element(sc.begin(), sc.begin() + static_cast<std::ptrdiff_t>(mid), sc.end());
    float median = sc[mid];

    if (median <= 0.0f) {
        return false;
    }

    /* Count inliers: spacings within GRID_REGULARITY_TOL of median. */
    float tol         = GRID_REGULARITY_TOL * median;
    int   inlier_count = 0;
    float inlier_sum  = 0.0f;
    for (float s : spacings) {
        if (std::fabs(s - median) <= tol) {
            ++inlier_count;
            inlier_sum += s;
        }
    }

    float fraction = static_cast<float>(inlier_count)
                   / static_cast<float>(spacings.size());

    if (fraction < GRID_INLIER_FRACTION || inlier_count < 2) {
        return false;
    }

    out_spacing = inlier_sum / static_cast<float>(inlier_count);
    return true;
}

CeilingGridResult detect_ceiling_grid(
    const std::vector<float>& horizontal_positions,
    const std::vector<float>& vertical_positions) noexcept
{
    CeilingGridResult result{};

    float cell_width  = 0.0f;
    float cell_height = 0.0f;

    bool h_ok = is_regular(horizontal_positions, cell_width);
    bool v_ok = is_regular(vertical_positions, cell_height);

    if (h_ok && v_ok) {
        result.is_grid     = true;
        result.cell_width  = cell_width;
        result.cell_height = cell_height;
    }

    return result;
}

} /* namespace censor */
