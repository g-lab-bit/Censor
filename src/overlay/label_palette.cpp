#include "overlay/label_palette.h"
#include "overlay/confidence_overlay.h" /* label_to_rgb() */

#include <algorithm>
#include <cctype>

namespace censor {

namespace {

/* AEC default palette: name, letter shortcut, slot 1-9. */
struct DefaultEntry { const char* name; char key; };
static constexpr DefaultEntry kDefaults[9] = {
    {"Wall",      'W'},
    {"Duct",      'D'},
    {"Pipe",      'P'},
    {"Furniture", 'F'},
    {"Ceiling",   'C'},
    {"Grid",      'G'},
    {"Text",      'T'},
    {"Dimension", 'M'},
    {"Equipment", 'E'},
};

} /* anonymous namespace */

/* --------------------------------------------------------------------------
 * Construction
 * -------------------------------------------------------------------------*/

LabelPalette::LabelPalette()
{
    entries_.reserve(9);
    for (int i = 0; i < 9; ++i) {
        LabelEntry e;
        e.name  = kDefaults[i].name;
        e.slot  = i + 1;
        e.key   = kDefaults[i].key;
        e.count = 0;
        auto rgb = label_to_rgb(e.name);
        e.color[0] = rgb[0];
        e.color[1] = rgb[1];
        e.color[2] = rgb[2];
        entries_.push_back(std::move(e));
    }
}

/* --------------------------------------------------------------------------
 * Visibility
 * -------------------------------------------------------------------------*/

void LabelPalette::toggle_visible()
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = !visible_;
}

void LabelPalette::set_visible(bool v)
{
    std::lock_guard<std::mutex> lk(mu_);
    visible_ = v;
}

bool LabelPalette::is_visible() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return visible_;
}

/* --------------------------------------------------------------------------
 * Slot selection
 * -------------------------------------------------------------------------*/

void LabelPalette::select_slot(int slot)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (entry_for_slot(slot))
        active_slot_ = slot;
}

bool LabelPalette::on_key(char key)
{
    std::lock_guard<std::mutex> lk(mu_);

    /* Digit 1-9 → select slot by index. */
    if (key >= '1' && key <= '9') {
        int slot = key - '0';
        if (entry_for_slot(slot)) {
            active_slot_ = slot;
            return true;
        }
        return false;
    }

    /* Letter shortcut — case-insensitive. */
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
    for (const auto& e : entries_) {
        if (e.key && e.key == upper) {
            active_slot_ = e.slot;
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Active state
 * -------------------------------------------------------------------------*/

int LabelPalette::active_slot() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return active_slot_;
}

std::string LabelPalette::active_label() const
{
    std::lock_guard<std::mutex> lk(mu_);
    const LabelEntry* e = entry_for_slot(active_slot_);
    return e ? e->name : std::string{};
}

/* --------------------------------------------------------------------------
 * Continuous click mode
 * -------------------------------------------------------------------------*/

bool LabelPalette::continuous_mode() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return continuous_mode_;
}

void LabelPalette::set_continuous_mode(bool on)
{
    std::lock_guard<std::mutex> lk(mu_);
    continuous_mode_ = on;
}

ClickMode LabelPalette::click_mode() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return click_mode_;
}

void LabelPalette::set_click_mode(ClickMode m)
{
    std::lock_guard<std::mutex> lk(mu_);
    click_mode_ = m;
}

/* --------------------------------------------------------------------------
 * Label tracking
 * -------------------------------------------------------------------------*/

void LabelPalette::record_label(const std::string& label)
{
    std::lock_guard<std::mutex> lk(mu_);
    LabelEntry* e = entry_for_label(label);
    if (e) ++e->count;
}

int LabelPalette::label_count(const std::string& label) const
{
    std::lock_guard<std::mutex> lk(mu_);
    const LabelEntry* e = entry_for_label(label);
    return e ? e->count : 0;
}

bool LabelPalette::seeding_complete(const std::string& label) const
{
    return label_count(label) >= SEEDING_THRESHOLD_PER_CLASS;
}

/* --------------------------------------------------------------------------
 * Custom labels
 * -------------------------------------------------------------------------*/

int LabelPalette::add_custom(const std::string& name)
{
    std::lock_guard<std::mutex> lk(mu_);

    /* Reject duplicate names. */
    for (const auto& e : entries_)
        if (e.name == name) return -1;

    int next_slot = static_cast<int>(entries_.size()) + 1;

    LabelEntry e;
    e.name  = name;
    e.slot  = next_slot;
    e.key   = 0; /* no letter shortcut for custom entries */
    e.count = 0;
    auto rgb = label_to_rgb(name);
    e.color[0] = rgb[0];
    e.color[1] = rgb[1];
    e.color[2] = rgb[2];
    entries_.push_back(std::move(e));
    return next_slot;
}

/* --------------------------------------------------------------------------
 * Color
 * -------------------------------------------------------------------------*/

void LabelPalette::set_color(int slot, float r, float g, float b)
{
    std::lock_guard<std::mutex> lk(mu_);
    LabelEntry* e = entry_for_slot(slot);
    if (!e) return;
    e->color[0] = r;
    e->color[1] = g;
    e->color[2] = b;
}

/* --------------------------------------------------------------------------
 * Snapshot
 * -------------------------------------------------------------------------*/

std::vector<LabelEntry> LabelPalette::snapshot() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return entries_;
}

/* --------------------------------------------------------------------------
 * Private helpers (caller holds mu_)
 * -------------------------------------------------------------------------*/

LabelEntry* LabelPalette::entry_for_slot(int slot)
{
    for (auto& e : entries_)
        if (e.slot == slot) return &e;
    return nullptr;
}

const LabelEntry* LabelPalette::entry_for_slot(int slot) const
{
    for (const auto& e : entries_)
        if (e.slot == slot) return &e;
    return nullptr;
}

LabelEntry* LabelPalette::entry_for_label(const std::string& label)
{
    for (auto& e : entries_)
        if (e.name == label) return &e;
    return nullptr;
}

const LabelEntry* LabelPalette::entry_for_label(const std::string& label) const
{
    for (const auto& e : entries_)
        if (e.name == label) return &e;
    return nullptr;
}

} /* namespace censor */
