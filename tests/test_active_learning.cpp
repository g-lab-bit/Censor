/*
 * test_active_learning.cpp — Unit tests for ActiveLearner.
 *
 * Uses KNNClassifier (real) and an in-memory CensorDb so tests are
 * self-contained and leave no files behind.
 */

#include <gtest/gtest.h>

#include "classifier/active_learning.h"
#include "classifier/knn_classifier.h"
#include "storage/censor_db.h"

using namespace censor;

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

static FeatureVector make_fv(float base)
{
    FeatureVector fv{};
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i)
        fv.dims[i] = base + static_cast<float>(i) * 0.1f;
    return fv;
}

/* Seed the classifier with enough labeled examples for a class to activate. */
static void seed_class(IClassifier& clf, const std::string& label, float base)
{
    for (int i = 0; i < SEEDING_THRESHOLD_PER_CLASS; ++i) {
        FeatureVector fv = make_fv(base + static_cast<float>(i) * 0.01f);
        clf.add_example(fv, label, false);
    }
}

/* ---------------------------------------------------------------------------
 * Fixture
 * -------------------------------------------------------------------------*/
class ALTest : public ::testing::Test {
protected:
    CensorDb      db;
    KNNClassifier clf;

    void SetUp() override {
        ASSERT_TRUE(db.open(":memory:"));
    }
};

/* ---------------------------------------------------------------------------
 * Test 1: find_confusables returns candidates sorted by similarity descending.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, FindConfusablesRankedBySimilarity) {
    ActiveLearner al{db, clf};

    FeatureVector ref = make_fv(1.0f); /* the cluster just labeled */

    /* Three unlabeled candidates at increasing distance from ref. */
    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {
        {"hash:1:far",    make_fv(5.0f)},  /* least similar */
        {"hash:1:close",  make_fv(1.1f)},  /* most similar */
        {"hash:1:medium", make_fv(2.0f)},  /* middle */
    };

    auto suggestions = al.find_confusables(ref, unlabeled);

    /* Should come back sorted closest-first. */
    ASSERT_GE(suggestions.size(), 2u);
    EXPECT_GT(suggestions[0].similarity, suggestions[1].similarity);
    EXPECT_EQ(suggestions[0].cluster_ref, "hash:1:close");
}

/* ---------------------------------------------------------------------------
 * Test 2: find_confusables respects CONFUSABLE_NEGATIVE_COUNT cap.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, FindConfusablesCappedAtK) {
    ActiveLearner al{db, clf};
    FeatureVector ref = make_fv(0.0f);

    std::vector<std::pair<std::string, FeatureVector>> unlabeled;
    for (int i = 0; i < CONFUSABLE_NEGATIVE_COUNT + 4; ++i)
        unlabeled.emplace_back("ref:" + std::to_string(i), make_fv(static_cast<float>(i)));

    auto suggestions = al.find_confusables(ref, unlabeled);
    EXPECT_LE(static_cast<int>(suggestions.size()), CONFUSABLE_NEGATIVE_COUNT);
}

/* ---------------------------------------------------------------------------
 * Test 3: find_confusables excludes high-confidence predictions.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, FindConfusablesExcludesHighConfidence) {
    /* Seed classifier so it can make high-confidence "wall" predictions. */
    seed_class(clf, "wall", 1.0f);

    ActiveLearner al{db, clf};

    FeatureVector ref = make_fv(0.0f);

    /* One candidate very close to the seeded wall examples → high-confidence
     * "wall" prediction → should be excluded.
     * One zero vector → cosine_similarity=0 to all seeds → predict() returns
     * empty result (above_threshold=false) → should be included. */
    FeatureVector zero_fv{};
    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {
        {"h:1:wall",    make_fv(1.05f)}, /* direction ≈ wall seeds → high conf */
        {"h:1:unknown", zero_fv},        /* zero vector → no cosine relation    */
    };

    auto suggestions = al.find_confusables(ref, unlabeled);

    /* The high-confidence "wall" prediction should be filtered out. */
    for (const auto& s : suggestions)
        EXPECT_NE(s.cluster_ref, "h:1:wall")
            << "high-confidence prediction should be excluded";

    /* The unknown cluster should pass through. */
    bool found_unknown = false;
    for (const auto& s : suggestions)
        if (s.cluster_ref == "h:1:unknown") found_unknown = true;
    EXPECT_TRUE(found_unknown);
}

/* ---------------------------------------------------------------------------
 * Test 4: confirm() updates DB and adds a positive example to classifier.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, ConfirmAddPositiveExample) {
    ActiveLearner al{db, clf};

    FeatureVector fv = make_fv(3.0f);

    /* Build and store a suggestion manually. */
    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {{"h:1:42", fv}};
    auto suggestions = al.find_confusables(make_fv(3.1f), unlabeled);
    ASSERT_EQ(suggestions.size(), 1u);
    al.store_suggestions(suggestions);
    ASSERT_FALSE(suggestions[0].suggestion_id.empty());

    int before = clf.label_counts()["duct"];
    ASSERT_TRUE(al.confirm(suggestions[0].suggestion_id, fv, "duct"));
    EXPECT_EQ(clf.label_counts()["duct"], before + 1);
}

/* ---------------------------------------------------------------------------
 * Test 5: reject() with no corrected label adds a negative example.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, RejectAddNegativeExample) {
    ActiveLearner al{db, clf};
    FeatureVector fv = make_fv(4.0f);

    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {{"h:1:10", fv}};
    auto suggestions = al.find_confusables(make_fv(4.1f), unlabeled);
    ASSERT_EQ(suggestions.size(), 1u);
    al.store_suggestions(suggestions);

    int before = clf.label_counts()["pipe"];
    ASSERT_TRUE(al.reject(suggestions[0].suggestion_id, fv, "pipe"));
    EXPECT_EQ(clf.label_counts()["pipe"], before + 1);
}

/* ---------------------------------------------------------------------------
 * Test 6: reject() with corrected label adds both negative and positive.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, ReclassifyAddsBothNegAndPos) {
    ActiveLearner al{db, clf};
    FeatureVector fv = make_fv(5.0f);

    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {{"h:1:99", fv}};
    auto suggestions = al.find_confusables(make_fv(5.1f), unlabeled);
    ASSERT_EQ(suggestions.size(), 1u);
    al.store_suggestions(suggestions);

    int before_pipe = clf.label_counts()["pipe"];
    int before_duct = clf.label_counts()["duct"];

    ASSERT_TRUE(al.reject(suggestions[0].suggestion_id, fv, "pipe", "duct"));

    EXPECT_EQ(clf.label_counts()["pipe"], before_pipe + 1); /* negative */
    EXPECT_EQ(clf.label_counts()["duct"], before_duct + 1); /* positive */
}

/* ---------------------------------------------------------------------------
 * Test 7: store_suggestions round-trips to pending_suggestions table.
 * -------------------------------------------------------------------------*/
TEST_F(ALTest, StoreSuggestionsPopulatesSuggestionId) {
    ActiveLearner al{db, clf};
    FeatureVector fv = make_fv(2.0f);

    std::vector<std::pair<std::string, FeatureVector>> unlabeled = {
        {"h:1:a", make_fv(2.1f)},
        {"h:1:b", make_fv(2.2f)},
    };
    auto suggestions = al.find_confusables(fv, unlabeled);
    ASSERT_GE(suggestions.size(), 1u);

    al.store_suggestions(suggestions);

    for (const auto& s : suggestions)
        EXPECT_FALSE(s.suggestion_id.empty()) << "suggestion_id should be set after store";
}
