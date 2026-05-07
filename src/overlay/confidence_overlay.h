#ifndef CENSOR_CONFIDENCE_OVERLAY_H
#define CENSOR_CONFIDENCE_OVERLAY_H

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
std::array<float, 3> label_to_rgb(const std::string& label);

/* ---------------------------------------------------------------------------
 * ConfidenceOverlay — manages per-cluster overlay render data.
 *
 * Only stores entries for clusters whose predicted class is past the seeding
 * activation threshold (ClassifyResult::above_threshold == true).
 * Thread-safe: update/remove/clear are safe from any thread.
 * -------------------------------------------------------------------------*/
class ConfidenceOverlay {
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

private:
    mutable std::mutex              mu_;
    std::map<int, CensorOverlayEntry> entries_;
};

} /* namespace censor */

#endif /* CENSOR_CONFIDENCE_OVERLAY_H */
