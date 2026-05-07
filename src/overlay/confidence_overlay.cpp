#include "confidence_overlay.h"

#include <cmath>
#include <functional>

namespace censor {

namespace {

std::array<float, 3> hsv_to_rgb(float h, float s, float v)
{
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (static_cast<int>(h * 6.0f) % 6) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return {r + m, g + m, b + m};
}

} /* anonymous namespace */

std::array<float, 3> label_to_rgb(const std::string& label)
{
    std::size_t h   = std::hash<std::string>{}(label);
    float       hue = static_cast<float>(h % 1000u) / 1000.0f;
    return hsv_to_rgb(hue, 0.70f, 0.90f);
}

void ConfidenceOverlay::update(int cluster_id, const float bounds[4],
                                const ClassifyResult& result,
                                bool is_user_labeled)
{
    if (!result.above_threshold || result.label.empty()) return;

    auto rgb = label_to_rgb(result.label);

    CensorOverlayEntry entry{};
    entry.bounds[0]       = bounds[0];
    entry.bounds[1]       = bounds[1];
    entry.bounds[2]       = bounds[2];
    entry.bounds[3]       = bounds[3];
    entry.color[0]        = rgb[0];
    entry.color[1]        = rgb[1];
    entry.color[2]        = rgb[2];
    entry.color[3]        = result.confidence;   /* alpha = confidence */
    entry.label           = result.label;
    entry.confidence      = result.confidence;
    entry.cluster_id      = cluster_id;
    entry.is_user_labeled = is_user_labeled;

    std::lock_guard<std::mutex> lock(mu_);
    entries_[cluster_id] = std::move(entry);
}

void ConfidenceOverlay::update_user_label(int cluster_id, const float bounds[4],
                                           const std::string& label)
{
    ClassifyResult r{label, 1.0f, true};
    update(cluster_id, bounds, r, /*is_user_labeled=*/true);
}

void ConfidenceOverlay::remove(int cluster_id)
{
    std::lock_guard<std::mutex> lock(mu_);
    entries_.erase(cluster_id);
}

void ConfidenceOverlay::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    entries_.clear();
}

std::vector<CensorOverlayEntry> ConfidenceOverlay::snapshot() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<CensorOverlayEntry> out;
    out.reserve(entries_.size());
    for (const auto& kv : entries_)
        out.push_back(kv.second);
    return out;
}

} /* namespace censor */
