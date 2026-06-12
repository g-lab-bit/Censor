#ifndef CENSOR_CONFIDENCE_OVERLAY_H
#define CENSOR_CONFIDENCE_OVERLAY_H


#include "censor_visibility.h"
#include "censor_types.h"
#include "classifier/iclassifier.h"

#include <array>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace censor {

/* Deterministic RGB colour from a label string (hue derived from hash).
 * Returns {r, g, b} in [0, 1]. Combined with confidence as alpha to
 * produce the "class colour at confidence opacity" specified by the plugin. */
CENSOR_API
std::array<float, 3> label_to_rgb(const std::string& label);

/* Traffic-light RGBA from confidence score.
 *   >= 0.75 → green  (high)
 *   >= 0.40 → yellow (medium)
 *   <  0.40 → red    (low)
 * Alpha is set to confidence so low-confidence regions are more transparent.
 * Returns {r, g, b, a} in [0, 1]. */
CENSOR_API
std::array<float, 4> confidence_to_rgba(float confidence);

/* ---------------------------------------------------------------------------
 * OverlayUIState — rendering control passed to snapshot_filtered().
 *
 * predictions_visible: when false, AI-predicted entries are hidden; only
 *   user-labeled entries are returned.
 * min_confidence: entries with confidence < min_confidence are excluded.
 *   Corresponds to a UI threshold slider in [0, 1].
 * -------------------------------------------------------------------------*/
struct CENSOR_API OverlayUIState {
    bool  predictions_visible = true;
    float min_confidence      = 0.0f;
};

/* ---------------------------------------------------------------------------
 * ConfidenceOverlay — manages per-cluster overlay render data.
 *
 * Only stores entries for clusters whose predicted class is past the seeding
 * activation threshold (ClassifyResult::above_threshold == true).
 * Thread-safe: update/remove/clear are safe from any thread.
 * -------------------------------------------------------------------------*/
class CENSOR_API ConfidenceOverlay {
public:
    /* Update or insert overlay entry for a classified cluster.
     * Ignored when result.above_threshold is false or result.label is empty. */
    void update(int cluster_id, const float bounds[4],
                const ClassifyResult& result,
                bool is_user_labeled = false);

    /* Insert / overwrite with a direct user label (always shown, confidence 1). */
    void update_user_label(int cluster_id, const float bounds[4],
                           const std::string& label);

    /* Remove overlay entry for cluster_id (no-op if absent). */
    void remove(int cluster_id);

    /* Remove all entries. */
    void clear();

    /* Thread-safe snapshot of all current entries for rendering. */
    std::vector<CensorOverlayEntry> snapshot() const;

    /* Filtered snapshot applying OverlayUIState rules:
     * - Excludes AI-predicted entries when predictions_visible is false.
     * - Excludes entries with confidence < min_confidence.
     * - Overrides entry color with traffic-light confidence_to_rgba. */
    std::vector<CensorOverlayEntry> snapshot_filtered(
        const OverlayUIState& state) const;

private:
    mutable std::mutex              mu_;
    std::map<int, CensorOverlayEntry> entries_;
};

} /* namespace censor */

#endif /* CENSOR_CONFIDENCE_OVERLAY_H */
