/*
 * test_confusable_popover.cpp — Tests for ConfusablePopover and RejectionToast.
 *
 * SPEC-censor-ui.md §5 (rejection feedback) and §7 (confusable negative surfacing).
 */

#include <gtest/gtest.h>

#include "overlay/confusable_popover.h"
#include "overlay/rejection_toast.h"

using namespace censor;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static PopoverItem make_item(const std::string& label,
                              const std::string& id = "",
                              float confidence = 0.5f,
                              float similarity = 0.8f)
{
    PopoverItem it;
    it.suggestion_id   = id.empty() ? label + "-uuid" : id;
    it.predicted_label = label;
    it.confidence      = confidence;
    it.similarity      = similarity;
    it.bounds[0] = 0; it.bounds[1] = 0; it.bounds[2] = 10; it.bounds[3] = 10;
    return it;
}

/* ========================================================================== */
/* ConfusablePopover                                                           */
/* ========================================================================== */

TEST(ConfusablePopover, InitiallyEmpty)
{
    ConfusablePopover pop;
    EXPECT_FALSE(pop.has_pending());
    EXPECT_FALSE(pop.snapshot().visible);
}

TEST(ConfusablePopover, LoadMakesPending)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall"), make_item("Duct")});
    EXPECT_TRUE(pop.has_pending());
    EXPECT_TRUE(pop.snapshot().visible);
}

TEST(ConfusablePopover, SnapshotShowsFirstItem)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall", "uuid-w"), make_item("Duct", "uuid-d")});
    auto snap = pop.snapshot();
    EXPECT_TRUE(snap.visible);
    EXPECT_EQ(snap.item.predicted_label, "Wall");
    EXPECT_EQ(snap.item.suggestion_id, "uuid-w");
    EXPECT_EQ(snap.remaining, 1);
}

TEST(ConfusablePopover, ConfirmAdvancesQueue)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall", "w"), make_item("Duct", "d")});

    auto r = pop.confirm();
    EXPECT_TRUE(r.acted);
    EXPECT_EQ(r.item.suggestion_id, "w");
    EXPECT_EQ(r.item.predicted_label, "Wall");
    EXPECT_TRUE(r.corrected_label.empty());

    auto snap = pop.snapshot();
    EXPECT_TRUE(snap.visible);
    EXPECT_EQ(snap.item.predicted_label, "Duct");
}

TEST(ConfusablePopover, RejectAdvancesQueue)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall", "w"), make_item("Duct", "d")});

    auto r = pop.reject();
    EXPECT_TRUE(r.acted);
    EXPECT_EQ(r.item.suggestion_id, "w");
    EXPECT_TRUE(r.corrected_label.empty());

    EXPECT_EQ(pop.snapshot().item.predicted_label, "Duct");
}

TEST(ConfusablePopover, ReclassifyCarriesCorrectedLabel)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall", "w")});

    auto r = pop.reclassify("Duct");
    EXPECT_TRUE(r.acted);
    EXPECT_EQ(r.item.predicted_label, "Wall");
    EXPECT_EQ(r.corrected_label, "Duct");

    EXPECT_FALSE(pop.has_pending());
}

TEST(ConfusablePopover, AllActedBecomesInvisible)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall")});
    pop.confirm();
    EXPECT_FALSE(pop.has_pending());
    EXPECT_FALSE(pop.snapshot().visible);
}

TEST(ConfusablePopover, DismissClearsAll)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall"), make_item("Duct"), make_item("Pipe")});
    pop.dismiss();
    EXPECT_FALSE(pop.has_pending());
    EXPECT_FALSE(pop.snapshot().visible);
}

TEST(ConfusablePopover, RemainingDecrementsPerAction)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall"), make_item("Duct"), make_item("Pipe")});

    EXPECT_EQ(pop.snapshot().remaining, 2);
    pop.reject();
    EXPECT_EQ(pop.snapshot().remaining, 1);
    pop.confirm();
    EXPECT_EQ(pop.snapshot().remaining, 0);
}

TEST(ConfusablePopover, LoadReplacesExistingQueue)
{
    ConfusablePopover pop;
    pop.load({make_item("Wall", "old")});
    pop.load({make_item("Duct", "new1"), make_item("Pipe", "new2")});

    auto snap = pop.snapshot();
    EXPECT_EQ(snap.item.suggestion_id, "new1");
    EXPECT_EQ(snap.remaining, 1);
}

TEST(ConfusablePopover, ActOnEmptyReturnsNotActed)
{
    ConfusablePopover pop;
    auto r = pop.confirm();
    EXPECT_FALSE(r.acted);
    r = pop.reject();
    EXPECT_FALSE(r.acted);
    r = pop.reclassify("Duct");
    EXPECT_FALSE(r.acted);
}

TEST(ConfusablePopover, FeaturesCarriedThrough)
{
    PopoverItem it = make_item("Wall", "w");
    it.features.dims[0] = 3.14f;

    ConfusablePopover pop;
    pop.load({it});
    auto r = pop.confirm();
    EXPECT_FLOAT_EQ(r.item.features.dims[0], 3.14f);
}

/* ========================================================================== */
/* RejectionToast                                                              */
/* ========================================================================== */

TEST(RejectionToast, InitiallyNotVisible)
{
    RejectionToast toast;
    auto snap = toast.current();
    EXPECT_FALSE(snap.visible);
    EXPECT_TRUE(snap.message.empty());
}

TEST(RejectionToast, SetMakesVisible)
{
    RejectionToast toast;
    toast.set("Rejected");
    auto snap = toast.current();
    EXPECT_TRUE(snap.visible);
    EXPECT_EQ(snap.message, "Rejected");
}

TEST(RejectionToast, ReclassifyMessage)
{
    RejectionToast toast;
    toast.set("Reclassified \xe2\x86\x92 Duct");
    auto snap = toast.current();
    EXPECT_TRUE(snap.visible);
    EXPECT_NE(snap.message.find("Duct"), std::string::npos);
}

TEST(RejectionToast, DismissMakesInvisible)
{
    RejectionToast toast;
    toast.set("Rejected");
    toast.dismiss();
    EXPECT_FALSE(toast.current().visible);
}

TEST(RejectionToast, SetReplacesPreviousMessage)
{
    RejectionToast toast;
    toast.set("Rejected");
    toast.set("Reclassified \xe2\x86\x92 Wall");
    auto snap = toast.current();
    EXPECT_TRUE(snap.visible);
    EXPECT_EQ(snap.message.find("Rejected"), std::string::npos);
    EXPECT_NE(snap.message.find("Wall"), std::string::npos);
}

TEST(RejectionToast, DismissAfterDismissIsNoop)
{
    RejectionToast toast;
    toast.dismiss();
    toast.dismiss();
    EXPECT_FALSE(toast.current().visible);
}
