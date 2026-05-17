#include <gtest/gtest.h>

#include "overlay/label_palette.h"
#include "classifier/knn_classifier.h"  /* SEEDING_THRESHOLD_PER_CLASS */

#include <string>
#include <vector>

using namespace censor;

/* --------------------------------------------------------------------------
 * Construction — 9 default AEC slots
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, DefaultsHaveNineSlots)
{
    LabelPalette p;
    auto entries = p.snapshot();
    ASSERT_EQ(entries.size(), 9u);
}

TEST(LabelPalette, DefaultSlotsNumberedOneToNine)
{
    LabelPalette p;
    auto entries = p.snapshot();
    for (int i = 0; i < 9; ++i)
        EXPECT_EQ(entries[i].slot, i + 1);
}

TEST(LabelPalette, DefaultCountsAreZero)
{
    LabelPalette p;
    for (const auto& e : p.snapshot())
        EXPECT_EQ(e.count, 0);
}

TEST(LabelPalette, DefaultColorsAreNonZero)
{
    /* label_to_rgb() assigns distinct hues — at least one channel must be > 0. */
    LabelPalette p;
    for (const auto& e : p.snapshot()) {
        bool any_nonzero = e.color[0] > 0.0f || e.color[1] > 0.0f || e.color[2] > 0.0f;
        EXPECT_TRUE(any_nonzero) << "slot " << e.slot << " has all-zero color";
    }
}

/* --------------------------------------------------------------------------
 * Visibility toggle
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, InitiallyNotVisible)
{
    LabelPalette p;
    EXPECT_FALSE(p.is_visible());
}

TEST(LabelPalette, ToggleVisibleFlips)
{
    LabelPalette p;
    p.toggle_visible();
    EXPECT_TRUE(p.is_visible());
    p.toggle_visible();
    EXPECT_FALSE(p.is_visible());
}

TEST(LabelPalette, SetVisibleOverrides)
{
    LabelPalette p;
    p.set_visible(true);
    EXPECT_TRUE(p.is_visible());
    p.set_visible(false);
    EXPECT_FALSE(p.is_visible());
}

/* --------------------------------------------------------------------------
 * Hotkey routing — digit 1-9
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, DigitKeySelectsSlot)
{
    LabelPalette p;
    bool consumed = p.on_key('3');
    EXPECT_TRUE(consumed);
    EXPECT_EQ(p.active_slot(), 3);
}

TEST(LabelPalette, AllNineDigitKeysConsumed)
{
    LabelPalette p;
    for (char c = '1'; c <= '9'; ++c) {
        EXPECT_TRUE(p.on_key(c)) << "key '" << c << "' not consumed";
        EXPECT_EQ(p.active_slot(), c - '0');
    }
}

TEST(LabelPalette, DigitKeyOutOfRangeNotConsumed)
{
    LabelPalette p;
    /* '0' maps to slot 0 which doesn't exist. */
    bool consumed = p.on_key('0');
    EXPECT_FALSE(consumed);
}

/* --------------------------------------------------------------------------
 * Hotkey routing — letter shortcuts
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, LetterKeySelectsSlot_Uppercase)
{
    LabelPalette p;
    /* 'W' is slot 1 (Wall). */
    bool consumed = p.on_key('W');
    EXPECT_TRUE(consumed);
    EXPECT_EQ(p.active_label(), "Wall");
}

TEST(LabelPalette, LetterKeySelectsSlot_Lowercase)
{
    LabelPalette p;
    /* 'd' == 'D' → slot 2 (Duct). */
    bool consumed = p.on_key('d');
    EXPECT_TRUE(consumed);
    EXPECT_EQ(p.active_label(), "Duct");
}

TEST(LabelPalette, UnknownLetterKeyNotConsumed)
{
    LabelPalette p;
    EXPECT_FALSE(p.on_key('Z'));
}

/* --------------------------------------------------------------------------
 * Active slot / label
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, InitiallyNoActiveSlot)
{
    LabelPalette p;
    EXPECT_EQ(p.active_slot(), 0);
    EXPECT_TRUE(p.active_label().empty());
}

TEST(LabelPalette, SelectSlotUpdatesActiveLabel)
{
    LabelPalette p;
    p.select_slot(2);
    EXPECT_EQ(p.active_slot(), 2);
    EXPECT_EQ(p.active_label(), "Duct");
}

TEST(LabelPalette, SelectOutOfRangeNoOp)
{
    LabelPalette p;
    p.select_slot(100);
    EXPECT_EQ(p.active_slot(), 0);
}

/* --------------------------------------------------------------------------
 * Label tracking / progress
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, RecordLabelIncrementsCount)
{
    LabelPalette p;
    p.record_label("Wall");
    p.record_label("Wall");
    EXPECT_EQ(p.label_count("Wall"), 2);
}

TEST(LabelPalette, RecordUnknownLabelNoOp)
{
    LabelPalette p;
    p.record_label("Unknown");
    EXPECT_EQ(p.label_count("Unknown"), 0);
}

TEST(LabelPalette, SeedingCompleteAfterThreshold)
{
    LabelPalette p;
    EXPECT_FALSE(p.seeding_complete("Wall"));
    for (int i = 0; i < SEEDING_THRESHOLD_PER_CLASS; ++i)
        p.record_label("Wall");
    EXPECT_TRUE(p.seeding_complete("Wall"));
}

TEST(LabelPalette, SeedingNotCompleteBeforeThreshold)
{
    LabelPalette p;
    for (int i = 0; i < SEEDING_THRESHOLD_PER_CLASS - 1; ++i)
        p.record_label("Duct");
    EXPECT_FALSE(p.seeding_complete("Duct"));
}

TEST(LabelPalette, RecordLabelUpdatesSnapshotCount)
{
    LabelPalette p;
    p.record_label("Pipe");
    p.record_label("Pipe");
    p.record_label("Pipe");

    auto snap = p.snapshot();
    /* Pipe is slot 3 (index 2 in default order). */
    auto it = std::find_if(snap.begin(), snap.end(),
                           [](const LabelEntry& e){ return e.name == "Pipe"; });
    ASSERT_NE(it, snap.end());
    EXPECT_EQ(it->count, 3);
}

/* --------------------------------------------------------------------------
 * Continuous click mode
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, ContinuousModeOffByDefault)
{
    LabelPalette p;
    EXPECT_FALSE(p.continuous_mode());
}

TEST(LabelPalette, SetContinuousModeOn)
{
    LabelPalette p;
    p.set_continuous_mode(true);
    EXPECT_TRUE(p.continuous_mode());
}

TEST(LabelPalette, ClickModeDefaultAdd)
{
    LabelPalette p;
    EXPECT_EQ(p.click_mode(), ClickMode::ADD);
}

TEST(LabelPalette, SetClickModeRemove)
{
    LabelPalette p;
    p.set_click_mode(ClickMode::REMOVE);
    EXPECT_EQ(p.click_mode(), ClickMode::REMOVE);
}

TEST(LabelPalette, ClickModeRoundTrip)
{
    LabelPalette p;
    p.set_click_mode(ClickMode::REMOVE);
    EXPECT_EQ(p.click_mode(), ClickMode::REMOVE);
    p.set_click_mode(ClickMode::ADD);
    EXPECT_EQ(p.click_mode(), ClickMode::ADD);
}

/* --------------------------------------------------------------------------
 * Custom labels
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, AddCustomAppendsSlot)
{
    LabelPalette p;
    int slot = p.add_custom("Stair");
    EXPECT_EQ(slot, 10);
    EXPECT_EQ(p.snapshot().size(), 10u);
}

TEST(LabelPalette, AddCustomDuplicateReturnsMinusOne)
{
    LabelPalette p;
    p.add_custom("Stair");
    int slot = p.add_custom("Stair");
    EXPECT_EQ(slot, -1);
    EXPECT_EQ(p.snapshot().size(), 10u); /* no duplicate added */
}

TEST(LabelPalette, AddDuplicateOfDefaultReturnsMinusOne)
{
    LabelPalette p;
    int slot = p.add_custom("Wall");
    EXPECT_EQ(slot, -1);
}

TEST(LabelPalette, CustomSlotSelectableByIndex)
{
    LabelPalette p;
    int slot = p.add_custom("Ramp");
    p.select_slot(slot);
    EXPECT_EQ(p.active_slot(), slot);
    EXPECT_EQ(p.active_label(), "Ramp");
}

TEST(LabelPalette, CustomSlotHasColor)
{
    LabelPalette p;
    p.add_custom("Skylight");
    auto snap = p.snapshot();
    const auto& last = snap.back();
    EXPECT_EQ(last.name, "Skylight");
    bool any_nonzero = last.color[0] > 0.0f || last.color[1] > 0.0f || last.color[2] > 0.0f;
    EXPECT_TRUE(any_nonzero);
}

/* --------------------------------------------------------------------------
 * Color override
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, SetColorOverridesDefaultColor)
{
    LabelPalette p;
    p.set_color(1, 1.0f, 0.0f, 0.0f); /* bright red for Wall */
    auto snap = p.snapshot();
    EXPECT_FLOAT_EQ(snap[0].color[0], 1.0f);
    EXPECT_FLOAT_EQ(snap[0].color[1], 0.0f);
    EXPECT_FLOAT_EQ(snap[0].color[2], 0.0f);
}

TEST(LabelPalette, SetColorInvalidSlotNoOp)
{
    LabelPalette p;
    auto before = p.snapshot();
    p.set_color(99, 1.0f, 0.0f, 0.0f);
    auto after  = p.snapshot();
    /* Nothing should have changed. */
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_FLOAT_EQ(before[i].color[0], after[i].color[0]);
        EXPECT_FLOAT_EQ(before[i].color[1], after[i].color[1]);
        EXPECT_FLOAT_EQ(before[i].color[2], after[i].color[2]);
    }
}

/* --------------------------------------------------------------------------
 * Snapshot ordering
 * -------------------------------------------------------------------------*/

TEST(LabelPalette, SnapshotOrderedBySlot)
{
    LabelPalette p;
    p.add_custom("Extra");
    auto snap = p.snapshot();
    for (size_t i = 1; i < snap.size(); ++i)
        EXPECT_LT(snap[i-1].slot, snap[i].slot);
}
