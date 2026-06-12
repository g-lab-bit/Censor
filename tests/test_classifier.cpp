#include <gtest/gtest.h>
#include "classifier/knn_classifier.h"
#include "test_utils.h"

using namespace censor;
using namespace censor::test;

/* ---------------------------------------------------------------------------
 * Helper: add N copies of a labelled example to the classifier.
 * -------------------------------------------------------------------------*/
static void seed_class(KNNClassifier& clf,
                       const std::string& label,
                       int count,
                       const FeatureVector& fv)
{
    for (int i = 0; i < count; ++i) {
        clf.add_example(fv, label, /*is_negative=*/false);
    }
}

/* ---------------------------------------------------------------------------
 * Test 1: cosine_similarity — identical vectors score 1.0 (via predict path).
 *
 * Seed one class with enough examples and query the exact same vector.
 * Confidence must be 1.0 (all k neighbors are the same class with sim=1).
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, CosineSimilarityIdenticalVectorsMaxConfidence)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    seed_class(clf, "wall", SEEDING_THRESHOLD_PER_CLASS, wall);

    ClassifyResult r = clf.predict(wall);
    EXPECT_EQ(r.label, "wall");
    EXPECT_FLOAT_EQ(r.confidence, 1.0f);
    EXPECT_TRUE(r.above_threshold);
}

/* ---------------------------------------------------------------------------
 * Test 2: weighted voting — majority class wins.
 *
 * Seed "wall" (10) and "duct" (10), then query a wall-like vector and a
 * duct-like vector. Each should predict the matching class.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, WeightedVotingMajorityClassWins)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    FeatureVector duct = make_feature_vector("duct");
    seed_class(clf, "wall", SEEDING_THRESHOLD_PER_CLASS, wall);
    seed_class(clf, "duct", SEEDING_THRESHOLD_PER_CLASS, duct);

    ClassifyResult rw = clf.predict(wall);
    EXPECT_EQ(rw.label, "wall");

    ClassifyResult rd = clf.predict(duct);
    EXPECT_EQ(rd.label, "duct");
}

/* ---------------------------------------------------------------------------
 * Test 3: per-class activation threshold — no prediction below threshold.
 *
 * Add fewer than SEEDING_THRESHOLD_PER_CLASS examples; predict must return
 * an empty label even though examples exist.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, ActivationThresholdNotMetReturnsEmpty)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    /* One fewer example than required. */
    seed_class(clf, "wall", SEEDING_THRESHOLD_PER_CLASS - 1, wall);

    ClassifyResult r = clf.predict(wall);
    EXPECT_EQ(r.label, "");
    EXPECT_FLOAT_EQ(r.confidence, 0.0f);
    EXPECT_FALSE(r.above_threshold);
}

/* ---------------------------------------------------------------------------
 * Test 4: confidence calculation — fractional confidence when top-k is split.
 *
 * Class alpha: 4 examples at sim=1.0 + 6 examples at sim=0.0 (10 total).
 * Class beta:  10 examples at sim=0.8.
 * Query: [1, 0, ...].
 *
 * Top-5 by similarity: 4 alpha (1.0) + 1 beta (0.8).
 * Weight alpha = 4*1.0 = 4.0, weight beta = 1*0.8 = 0.8, total = 4.8.
 * Confidence alpha = 4.0/4.8 = 5/6 ≈ 0.833 — strictly between 0 and 1.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, ConfidenceBelowOneForMixedNeighborhood)
{
    KNNClassifier clf;

    /* Query direction. */
    FeatureVector query{};
    query.dims[0] = 1.0f;

    /* Alpha high: same direction as query → sim=1.0. */
    FeatureVector fv_alpha_hi{};
    fv_alpha_hi.dims[0] = 1.0f;

    /* Alpha low: orthogonal to query → sim=0.0. */
    FeatureVector fv_alpha_lo{};
    fv_alpha_lo.dims[1] = 1.0f;

    /* Beta: 80/60 components → sim = 0.8 to query. */
    FeatureVector fv_beta{};
    fv_beta.dims[0] = 0.8f;
    fv_beta.dims[1] = 0.6f;

    /* 4 high-sim + 6 low-sim alpha examples (total 10, meets threshold). */
    for (int i = 0; i < 4; ++i) clf.add_example(fv_alpha_hi, "alpha", false);
    for (int i = 0; i < 6; ++i) clf.add_example(fv_alpha_lo, "alpha", false);

    /* 10 medium-sim beta examples. */
    seed_class(clf, "beta", SEEDING_THRESHOLD_PER_CLASS, fv_beta);

    ClassifyResult r = clf.predict(query);
    ASSERT_EQ(r.label, "alpha");
    /* 4 alpha (1.0) + 1 beta (0.8) in top-5 → alpha confidence = 4/4.8 */
    EXPECT_FLOAT_EQ(r.confidence, 4.0f / 4.8f);
    EXPECT_LT(r.confidence, 1.0f);
    EXPECT_GT(r.confidence, 0.0f);
}

/* ---------------------------------------------------------------------------
 * Test 5: empty classifier returns no prediction.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, EmptyClassifierReturnsEmpty)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    ClassifyResult r = clf.predict(wall);
    EXPECT_EQ(r.label, "");
    EXPECT_FLOAT_EQ(r.confidence, 0.0f);
    EXPECT_FALSE(r.above_threshold);
}

/* ---------------------------------------------------------------------------
 * Test 6: label_counts — counts POSITIVE examples only (ce-o03).
 *
 * Negative examples must not be counted so the seeding-progress bar agrees
 * with predict()'s activation guard (SEEDING_THRESHOLD_PER_CLASS counts
 * positives only).  A negative "wall" example must not bump the wall count.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, LabelCountsPositivesOnly)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    FeatureVector duct = make_feature_vector("duct");

    clf.add_example(wall, "wall", false);
    clf.add_example(wall, "wall", false);
    clf.add_example(duct, "duct", false);
    clf.add_example(wall, "wall", true);  /* negative — must NOT be counted */

    auto counts = clf.label_counts();
    EXPECT_EQ(counts["wall"], 2);  /* 2 positives, not 3 */
    EXPECT_EQ(counts["duct"], 1);
}

/* ---------------------------------------------------------------------------
 * Test 8: label_counts — label with only negatives is absent from the map.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, LabelCountsOmitsNegativeOnlyLabel)
{
    KNNClassifier clf;
    FeatureVector duct = make_feature_vector("duct");

    /* Add only negatives for "duct". */
    for (int i = 0; i < 5; ++i)
        clf.add_example(duct, "duct", /*is_negative=*/true);

    auto counts = clf.label_counts();
    /* "duct" must not appear because it has zero positives. */
    EXPECT_EQ(counts.count("duct"), 0u);
}

/* ---------------------------------------------------------------------------
 * Test 9: per-label FIFO cap (ce-f4i).
 *
 * Add MAX_EXAMPLES_PER_LABEL + 3 examples for one label.
 * The total positive-example count in label_counts must stay == MAX.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, PerLabelCapEvictsOldestExample)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");

    const int over = MAX_EXAMPLES_PER_LABEL + 3;
    for (int i = 0; i < over; ++i)
        clf.add_example(wall, "wall", /*is_negative=*/false);

    auto counts = clf.label_counts();
    EXPECT_EQ(counts["wall"], MAX_EXAMPLES_PER_LABEL);
}

/* ---------------------------------------------------------------------------
 * Test 10: cap is per-label; a second label is not affected (ce-f4i).
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, PerLabelCapDoesNotAffectOtherLabels)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    FeatureVector duct = make_feature_vector("duct");

    /* Overflow "wall" by 3. */
    for (int i = 0; i < MAX_EXAMPLES_PER_LABEL + 3; ++i)
        clf.add_example(wall, "wall", false);

    /* Add only 7 "duct" examples — below the cap. */
    for (int i = 0; i < 7; ++i)
        clf.add_example(duct, "duct", false);

    auto counts = clf.label_counts();
    EXPECT_EQ(counts["wall"], MAX_EXAMPLES_PER_LABEL);
    EXPECT_EQ(counts["duct"], 7);
}

/* ---------------------------------------------------------------------------
 * Test 7: negative examples are excluded from predictions.
 *
 * Seed "wall" with exactly SEEDING_THRESHOLD_PER_CLASS positives plus several
 * negatives.  Predict should still work (negatives don't dilute the vote).
 * Then, seed a second class ("duct") with only negatives — it must never win.
 * -------------------------------------------------------------------------*/
TEST(KNNClassifier, NegativeExamplesExcludedFromVoting)
{
    KNNClassifier clf;
    FeatureVector wall = make_feature_vector("wall");
    FeatureVector duct = make_feature_vector("duct");

    seed_class(clf, "wall", SEEDING_THRESHOLD_PER_CLASS, wall);

    /* Add duct vectors as negatives only — should never surface as a winner. */
    for (int i = 0; i < 20; ++i) {
        clf.add_example(duct, "duct", /*is_negative=*/true);
    }

    ClassifyResult r = clf.predict(wall);
    EXPECT_EQ(r.label, "wall");
    /* "duct" must not win because all its examples are negative. */
    EXPECT_NE(r.label, "duct");
}
