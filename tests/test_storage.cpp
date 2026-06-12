#include <gtest/gtest.h>
#include "storage/censor_db.h"
#include "classifier/knn_classifier.h"
#include "test_utils.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace censor;
using namespace censor::test;

/* ---------------------------------------------------------------------------
 * Helper: unique throwaway DB path in the system temp directory.
 * -------------------------------------------------------------------------*/
static std::string tmp_db_path(const std::string& name)
{
    auto tmp = std::filesystem::temp_directory_path();
    tmp /= name;
    return tmp.string();
}

/* ---------------------------------------------------------------------------
 * RAII helper: deletes the DB file on destruction.
 * -------------------------------------------------------------------------*/
struct TmpDb {
    std::string path;
    CensorDb    db;

    explicit TmpDb(const std::string& name)
        : path(tmp_db_path(name))
    {
        /* Remove any leftover from a previous run. */
        std::filesystem::remove(path);
    }

    ~TmpDb()
    {
        db.close();
        std::filesystem::remove(path);
    }

    bool open() { return db.open(path); }
};

/* ---------------------------------------------------------------------------
 * Test: open() rejects empty path (ce-h34d).
 * -------------------------------------------------------------------------*/
TEST(CensorDb, OpenRejectsEmptyPath)
{
    CensorDb db;
    EXPECT_FALSE(db.open(""));
    EXPECT_FALSE(db.is_open());
}

/* ---------------------------------------------------------------------------
 * Test: basic open + close cycle.
 * -------------------------------------------------------------------------*/
TEST(CensorDb, OpenAndClose)
{
    TmpDb t("test_open_close.db");
    ASSERT_TRUE(t.open());
    EXPECT_TRUE(t.db.is_open());
    t.db.close();
    EXPECT_FALSE(t.db.is_open());
}

/* ---------------------------------------------------------------------------
 * Test: null-db guard — public methods on an un-opened CensorDb return
 * safe defaults rather than crashing or invoking SQLite on a null handle (ce-h34c).
 * -------------------------------------------------------------------------*/
TEST(CensorDb, NullDbGuardSafeMethods)
{
    CensorDb db;  /* never opened */

    EXPECT_FALSE(db.insert_label(LabeledCluster{}));
    EXPECT_TRUE(db.query_examples_by_label("wall").empty());
    EXPECT_TRUE(db.count_per_class().empty());
    EXPECT_FALSE(db.insert_suggestion(PendingSuggestion{}));
    EXPECT_FALSE(db.confirm_suggestion("x"));
    EXPECT_FALSE(db.reject_suggestion("x", ""));
    EXPECT_FALSE(db.upsert_pattern(PatternEntry{}));
    EXPECT_TRUE(db.all_patterns().empty());
    EXPECT_FALSE(db.for_each_labeled_cluster([](const FeatureVector&, const std::string&, bool) {}));
}

/* ---------------------------------------------------------------------------
 * Test: count_per_class() counts positives only (ce-o03).
 *
 * Insert 3 positive + 2 negative "wall" examples; count must be 3, not 5.
 * -------------------------------------------------------------------------*/
TEST(CensorDb, CountPerClassPositivesOnly)
{
    TmpDb t("test_count_per_class.db");
    ASSERT_TRUE(t.open());

    FeatureVector wall = make_feature_vector("wall");

    for (int i = 0; i < 3; ++i) {
        LabeledCluster lc;
        lc.id           = "pos" + std::to_string(i);
        lc.feature_vector = wall;
        lc.label        = "wall";
        lc.source_file  = "test.pdf";
        lc.is_negative  = false;
        ASSERT_TRUE(t.db.insert_label(lc));
    }
    for (int i = 0; i < 2; ++i) {
        LabeledCluster lc;
        lc.id           = "neg" + std::to_string(i);
        lc.feature_vector = wall;
        lc.label        = "wall";
        lc.source_file  = "test.pdf";
        lc.is_negative  = true;
        ASSERT_TRUE(t.db.insert_label(lc));
    }

    auto counts = t.db.count_per_class();
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts[0].first, "wall");
    EXPECT_EQ(counts[0].second, 3);  /* positives only */
}

/* ---------------------------------------------------------------------------
 * Test: round-trip — insert N rows, hydrate into a fresh KNNClassifier,
 * label_counts() in the classifier must match the positive inserts (ce-f4i).
 * -------------------------------------------------------------------------*/
TEST(CensorDb, HydrateClassifierRoundTrip)
{
    TmpDb t("test_hydrate_roundtrip.db");
    ASSERT_TRUE(t.open());

    FeatureVector wall = make_feature_vector("wall");
    FeatureVector duct = make_feature_vector("duct");

    /* Insert 5 positive wall, 3 positive duct, 2 negative wall. */
    for (int i = 0; i < 5; ++i) {
        LabeledCluster lc;
        lc.id           = "w" + std::to_string(i);
        lc.feature_vector = wall;
        lc.label        = "wall";
        lc.source_file  = "test.pdf";
        lc.is_negative  = false;
        ASSERT_TRUE(t.db.insert_label(lc));
    }
    for (int i = 0; i < 3; ++i) {
        LabeledCluster lc;
        lc.id           = "d" + std::to_string(i);
        lc.feature_vector = duct;
        lc.label        = "duct";
        lc.source_file  = "test.pdf";
        lc.is_negative  = false;
        ASSERT_TRUE(t.db.insert_label(lc));
    }
    for (int i = 0; i < 2; ++i) {
        LabeledCluster lc;
        lc.id           = "wn" + std::to_string(i);
        lc.feature_vector = wall;
        lc.label        = "wall";
        lc.source_file  = "test.pdf";
        lc.is_negative  = true;
        ASSERT_TRUE(t.db.insert_label(lc));
    }

    /* Hydrate a fresh classifier. */
    KNNClassifier clf;
    int loaded = hydrate_classifier(t.db, clf);

    /* Total rows loaded = 5 + 3 + 2 = 10 (positives + negatives). */
    EXPECT_EQ(loaded, 10);

    /* label_counts() counts positives only. */
    auto counts = clf.label_counts();
    EXPECT_EQ(counts["wall"], 5);
    EXPECT_EQ(counts["duct"], 3);
}

/* ---------------------------------------------------------------------------
 * Test: KNNClassifier per-label cap — add 503 examples for one label,
 * hydrated count must be <= MAX_EXAMPLES_PER_LABEL (ce-f4i).
 *
 * This test exercises the cap inside add_example (no DB needed for the cap
 * itself; stored count in label_counts must equal MAX).
 * -------------------------------------------------------------------------*/
TEST(CensorDb, KNNCapAt503Examples)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");

    for (int i = 0; i < 503; ++i)
        clf.add_example(wall, "wall", /*is_negative=*/false);

    auto counts = clf.label_counts();
    EXPECT_EQ(counts["wall"], MAX_EXAMPLES_PER_LABEL);  /* capped at 500 */
}
