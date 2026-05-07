/*
 * test_storage.cpp — Unit tests for the SQLite storage layer (censor_db).
 *
 * Uses an in-memory database (path ":memory:") so tests leave no files behind.
 */

#include <gtest/gtest.h>


#include "storage/censor_db.h"

#include <string>
#include <vector>

using namespace censor;

/* ---------------------------------------------------------------------------
 * Fixture — opens an in-memory DB before each test, closes after.
 * -------------------------------------------------------------------------*/
class StorageTest : public ::testing::Test {
protected:
    CensorDb db;

    void SetUp() override {
        ASSERT_TRUE(db.open(":memory:")) << "Failed to open in-memory database";
    }

    static LabeledCluster make_lc(const std::string& id,
                                   const std::string& label,
                                   float confidence = 0.9f,
                                   bool is_neg = false)
    {
        LabeledCluster lc;
        lc.id          = id;
        lc.label       = label;
        lc.source_file = "abc123";
        lc.page_number = 1;
        lc.timestamp   = "2026-01-01T00:00:00Z";
        lc.confidence  = confidence;
        lc.is_negative = is_neg;
        for (int i = 0; i < FEATURE_DIM_COUNT; ++i)
            lc.feature_vector.dims[i] = static_cast<float>(i) * 0.1f;
        return lc;
    }

    static PendingSuggestion make_suggestion(const std::string& id,
                                              const std::string& label,
                                              float confidence = 0.75f)
    {
        PendingSuggestion ps;
        ps.id              = id;
        ps.cluster_ref     = "abc123:1:42";
        ps.predicted_label = label;
        ps.confidence      = confidence;
        ps.state           = SuggestionState::Pending;
        return ps;
    }

    static PatternEntry make_pattern(const std::string& id,
                                      const std::string& label,
                                      int count = 5)
    {
        PatternEntry pe;
        pe.id            = id;
        pe.label         = label;
        pe.example_count = count;
        pe.source        = "local";
        for (int i = 0; i < FEATURE_DIM_COUNT; ++i)
            pe.feature_vector.dims[i] = static_cast<float>(i + 1) * 0.5f;
        return pe;
    }
};

/* ---------------------------------------------------------------------------
 * Test 1: Schema created and DB opens successfully.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, OpenAndIsOpen) {
    EXPECT_TRUE(db.is_open());
}

/* ---------------------------------------------------------------------------
 * Test 2: insert_label + query_examples_by_label round-trip.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, InsertAndQueryLabel) {
    auto lc = make_lc("id-001", "wall", 0.95f);
    ASSERT_TRUE(db.insert_label(lc));

    auto results = db.query_examples_by_label("wall");
    ASSERT_EQ(results.size(), 1u);

    const auto& r = results[0];
    EXPECT_EQ(r.id,          "id-001");
    EXPECT_EQ(r.label,       "wall");
    EXPECT_EQ(r.source_file, "abc123");
    EXPECT_EQ(r.page_number, 1);
    EXPECT_NEAR(r.confidence, 0.95f, 1e-5f);
    EXPECT_FALSE(r.is_negative);

    for (int i = 0; i < FEATURE_DIM_COUNT; ++i)
        EXPECT_NEAR(r.feature_vector.dims[i], static_cast<float>(i) * 0.1f, 1e-5f)
            << "feature dim " << i;
}

/* ---------------------------------------------------------------------------
 * Test 3: query_examples_by_label returns only matching label.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, QueryByLabelFilters) {
    ASSERT_TRUE(db.insert_label(make_lc("id-w1", "wall")));
    ASSERT_TRUE(db.insert_label(make_lc("id-w2", "wall")));
    ASSERT_TRUE(db.insert_label(make_lc("id-d1", "door")));

    auto walls = db.query_examples_by_label("wall");
    auto doors = db.query_examples_by_label("door");
    auto ducts = db.query_examples_by_label("duct");

    EXPECT_EQ(walls.size(), 2u);
    EXPECT_EQ(doors.size(), 1u);
    EXPECT_EQ(ducts.size(), 0u);
}

/* ---------------------------------------------------------------------------
 * Test 4: count_per_class aggregates correctly.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, CountPerClass) {
    db.insert_label(make_lc("id-w1", "wall"));
    db.insert_label(make_lc("id-w2", "wall"));
    db.insert_label(make_lc("id-w3", "wall"));
    db.insert_label(make_lc("id-d1", "door"));
    db.insert_label(make_lc("id-d2", "door"));

    auto counts = db.count_per_class();
    ASSERT_EQ(counts.size(), 2u);
    EXPECT_EQ(counts[0].first,  "door");
    EXPECT_EQ(counts[0].second, 2);
    EXPECT_EQ(counts[1].first,  "wall");
    EXPECT_EQ(counts[1].second, 3);
}

/* ---------------------------------------------------------------------------
 * Test 5: insert_suggestion + confirm round-trip.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, InsertAndConfirmSuggestion) {
    auto ps = make_suggestion("sug-001", "pipe", 0.82f);
    ASSERT_TRUE(db.insert_suggestion(ps));
    ASSERT_TRUE(db.confirm_suggestion("sug-001"));
}

/* ---------------------------------------------------------------------------
 * Test 6: reject_suggestion with corrected label.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, RejectSuggestionWithCorrectedLabel) {
    ASSERT_TRUE(db.insert_suggestion(make_suggestion("sug-002", "wall", 0.60f)));
    ASSERT_TRUE(db.reject_suggestion("sug-002", "duct"));
}

/* ---------------------------------------------------------------------------
 * Test 7: upsert_pattern inserts and updates on conflict.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, UpsertPatternInsertsAndUpdates) {
    auto pe = make_pattern("pat-001", "wall", 10);
    ASSERT_TRUE(db.upsert_pattern(pe));

    auto all = db.all_patterns();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].label,         "wall");
    EXPECT_EQ(all[0].example_count, 10);

    pe.example_count = 25;
    ASSERT_TRUE(db.upsert_pattern(pe));

    all = db.all_patterns();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].example_count, 25);
}

/* ---------------------------------------------------------------------------
 * Test 8: all_patterns returns all rows sorted by label.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, AllPatternsSortedByLabel) {
    ASSERT_TRUE(db.upsert_pattern(make_pattern("p3", "wall",      5)));
    ASSERT_TRUE(db.upsert_pattern(make_pattern("p1", "door",      3)));
    ASSERT_TRUE(db.upsert_pattern(make_pattern("p2", "furniture", 8)));

    auto all = db.all_patterns();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].label, "door");
    EXPECT_EQ(all[1].label, "furniture");
    EXPECT_EQ(all[2].label, "wall");
}

/* ---------------------------------------------------------------------------
 * Test 9: is_negative flag round-trips correctly.
 * -------------------------------------------------------------------------*/
TEST_F(StorageTest, IsNegativeFlagRoundTrip) {
    ASSERT_TRUE(db.insert_label(make_lc("id-pos", "wall", 0.9f, false)));
    ASSERT_TRUE(db.insert_label(make_lc("id-neg", "wall", 0.9f, true)));

    auto results = db.query_examples_by_label("wall");
    ASSERT_EQ(results.size(), 2u);

    bool found_pos = false, found_neg = false;
    for (const auto& r : results) {
        if (r.id == "id-pos") { found_pos = true; EXPECT_FALSE(r.is_negative); }
        if (r.id == "id-neg") { found_neg = true; EXPECT_TRUE(r.is_negative);  }
    }
    EXPECT_TRUE(found_pos);
    EXPECT_TRUE(found_neg);
}

/* ---------------------------------------------------------------------------
 * Test 10: close and re-open (on-disk) preserves data.
 * -------------------------------------------------------------------------*/
TEST(StoragePersistenceTest, CloseReopenPreservesData) {
    const std::string path = "/tmp/censor_test_persist.db";
    std::remove(path.c_str());

    {
        CensorDb db;
        ASSERT_TRUE(db.open(path));
        LabeledCluster lc;
        lc.id          = "persist-001";
        lc.label       = "duct";
        lc.source_file = "hash42";
        lc.page_number = 3;
        lc.timestamp   = "2026-05-07T12:00:00Z";
        lc.confidence  = 0.88f;
        ASSERT_TRUE(db.insert_label(lc));
    }

    {
        CensorDb db;
        ASSERT_TRUE(db.open(path));
        auto results = db.query_examples_by_label("duct");
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].id,    "persist-001");
        EXPECT_EQ(results[0].label, "duct");
        EXPECT_NEAR(results[0].confidence, 0.88f, 1e-5f);
    }

    std::remove(path.c_str());
}

