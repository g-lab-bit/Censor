/*
 * label_palette.h — Label palette state and hotkey routing (SPEC-censor-ui.md §3, §4).
 *
 * Manages active label selection, per-slot label counts, color swatches,
 * hotkey bindings (digit 1-9 and letter shortcuts), and continuous click mode.
 *
 * Censor owns the data; Rapida renders it using snapshot().
 * Thread-safe: all methods are safe from any thread.
 */

#ifndef CENSOR_LABEL_PALETTE_H
#define CENSOR_LABEL_PALETTE_H

#include "classifier/knn_classifier.h"  /* SEEDING_THRESHOLD_PER_CLASS */

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace censor {

/* ---------------------------------------------------------------------------
 * LabelEntry — one row in the palette toolbar.
 * slot is 1-based (1-9 for built-in classes; 10+ for user-defined customs).
 * -------------------------------------------------------------------------*/
struct LabelEntry {
    std::string name;         /* human-readable label, e.g. "Wall"           */
    int         slot      = 0;/* 1-based slot index                           */
    char        key       = 0;/* letter shortcut (e.g. 'W'), 0 = none        */
    float       color[3]  = {};/* RGB in [0,1]                               */
    int         count     = 0;/* labels applied so far (progress bar source) */
};

/* ---------------------------------------------------------------------------
 * ClickMode — continuous click mode state (§4).
 *
 * ADD:    clicking a region assigns the active label (green indicator).
 * REMOVE: clicking a region removes the label (red indicator).
 * -------------------------------------------------------------------------*/
enum class ClickMode { ADD, REMOVE };

/* ---------------------------------------------------------------------------
 * LabelPalette — floating palette state for Rapida to render.
 *
 * Constructed with 9 AEC default classes pre-populated.
 * Custom labels are appended via add_custom().
 *
 * Workflow:
 *   1. Rapida passes key events to on_key() — returns true if consumed.
 *   2. Rapida calls toggle_visible() on the palette shortcut or right-click.
 *   3. User selects a slot → active_label() returns the current string.
 *   4. After calling censor_label_cluster(), Rapida calls record_label() to
 *      increment the slot's progress counter.
 *   5. In continuous click mode (continuous_mode()==true) Rapida calls
 *      active_label() directly instead of re-opening the palette.
 * -------------------------------------------------------------------------*/
class __attribute__((visibility("default"))) LabelPalette {
public:
    /* Constructs palette pre-loaded with 9 AEC default entries. */
    LabelPalette();

    /* --- Visibility -------------------------------------------------------- */

    /* Toggle palette visibility (keyboard shortcut or right-click trigger). */
    void toggle_visible();

    /* Force palette visibility. */
    void set_visible(bool v);

    bool is_visible() const;

    /* --- Slot selection ---------------------------------------------------- */

    /* Select slot by 1-based index.  No-op when slot is out of range. */
    void select_slot(int slot);

    /* Handle a key press.
     * Digit '1'-'9' selects slot 1-9.
     * Letter key selects the matching slot (case-insensitive).
     * Returns true when the key was consumed by the palette. */
    bool on_key(char key);

    /* --- Active state ------------------------------------------------------- */

    /* Currently active slot (1-based).  0 = no slot selected. */
    int active_slot() const;

    /* Label string for the active slot.  Empty when no slot is active. */
    std::string active_label() const;

    /* --- Continuous click mode --------------------------------------------- */

    /* In continuous mode, clicking a region applies active_label() directly
     * without Rapida re-opening the palette. */
    bool      continuous_mode() const;
    void      set_continuous_mode(bool on);

    ClickMode click_mode() const;
    void      set_click_mode(ClickMode m);

    /* --- Label tracking ---------------------------------------------------- */

    /* Increment the count for the entry whose name matches label.
     * No-op when label is not found in the palette. */
    void record_label(const std::string& label);

    /* Label count for a named entry (0 when not found). */
    int label_count(const std::string& label) const;

    /* Returns true when a label's count meets SEEDING_THRESHOLD_PER_CLASS. */
    bool seeding_complete(const std::string& label) const;

    /* Seeding hint for Rapida to display inline in the palette.
     * Returns "Need N more <label> labels before predictions activate."
     * when below SEEDING_THRESHOLD_PER_CLASS, otherwise returns "".
     * Returns "" for Skip and unknown labels. */
    std::string seeding_hint(const std::string& label) const;

    /* --- Custom labels ----------------------------------------------------- */

    /* Append a new custom label.  Assigns the next available slot index.
     * Returns the new slot index, or -1 when name already exists. */
    int add_custom(const std::string& name);

    /* --- Color ------------------------------------------------------------- */

    /* Override the color for a slot (1-based).  No-op on invalid slot. */
    void set_color(int slot, float r, float g, float b);

    /* --- Snapshot ---------------------------------------------------------- */

    /* Thread-safe copy of all current entries, ordered by slot. */
    std::vector<LabelEntry> snapshot() const;

private:
    /* Returns a pointer to the entry for slot (1-based), nullptr if absent. */
    LabelEntry*       entry_for_slot(int slot);
    const LabelEntry* entry_for_slot(int slot) const;

    LabelEntry*       entry_for_label(const std::string& label);
    const LabelEntry* entry_for_label(const std::string& label) const;

    mutable std::mutex      mu_;
    std::vector<LabelEntry> entries_;
    int                     active_slot_      = 0;
    bool                    visible_          = false;
    bool                    continuous_mode_  = false;
    ClickMode               click_mode_       = ClickMode::ADD;
};

} /* namespace censor */

#endif /* CENSOR_LABEL_PALETTE_H */
