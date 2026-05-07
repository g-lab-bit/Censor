#include <gtest/gtest.h>

#include "overlay/completeness.h"
#include "overlay/discovery_queue.h"
#include "classifier/knn_classifier.h"
#include "censor_types.h"

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

namespace {

using namespace censor;

/* Build a Cluster with given id and feature vector. */
static Cluster make_cluster(int id, const FeatureVector& fv,
                             float x0 = 0, float y0 = 0,
                             float x1 = 10, float y1 = 10)
{
    Cluster c;
    c.cluster_id = id;
    c.features   = fv;
    c.bounds[0]  = x0; c.bounds[1] = y0;
    c.bounds[2]  = x1; c.bounds[3] = y1;
    return c;
}

/* Feature vector with a single non-zero dimension for easy distance control. */
static FeatureVector fv_unit(int dim)
{
    FeatureVector fv{};
    fv.dims[dim] = 1.0f;
    return fv;
}

/* Seed a KNNClassifier with `count` copies of (fv, label) so it passes the
 * SEEDING_THRESHOLD_PER_CLASS activation guard. */
static void seed_class(KNNClassifier& clf,
                        const FeatureVector& fv,
                        const std::string& label,
                        int count = SEEDING_THRESHOLD_PER_CLASS)
{
    for (int i = 0; i < count; ++i)
        clf.add_example(fv, label, /*is_negative=*/false);
}

} /* anonymous namespace */

/* ========================================================================== */
/* compute_completeness tests                                                   */
/* ========================================================================== */

TEST(Completeness, EmptyClustersReturnsZero)
{
    KNNClassifier clf;
    std::vector<Cluster> clusters;
    std::vector<std::string> labels;
    EXPECT_FLOAT_EQ(compute_completeness(clusters, labels, clf), 0.0f);
}

TEST(Completeness, AllUserLabeledReturnsOne)
{
    KNNClassifier clf;
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {make_cluster(0, fv), make_cluster(1, fv)};
    std::vector<std::string> labels = {"wall", "duct"};
    EXPECT_FLOAT_EQ(compute_completeness(clusters, labels, clf), 1.0f);
}

TEST(Completeness, NoneUnderstoodReturnsZero)
{
    KNNClassifier clf; /* empty — no examples → no above-threshold predictions */
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {make_cluster(0, fv), make_cluster(1, fv)};
    std::vector<std::string> labels = {"", ""};
    EXPECT_FLOAT_EQ(compute_completeness(clusters, labels, clf), 0.0f);
}

TEST(Completeness, HalfUserLabeledHalfUnknown)
{
    KNNClassifier clf; /* no examples — unlabeled clusters not understood */
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {
        make_cluster(0, fv),
        make_cluster(1, fv),
        make_cluster(2, fv),
        make_cluster(3, fv),
    };
    std::vector<std::string> labels = {"wall", "duct", "", ""};
    EXPECT_FLOAT_EQ(compute_completeness(clusters, labels, clf), 0.5f);
}

TEST(Completeness, AboveThresholdCountsAsUnderstood)
{
    KNNClassifier clf;
    FeatureVector wall_fv = fv_unit(0); /* dim 0 = wall */
    seed_class(clf, wall_fv, "wall");

    /* Cluster 0: no user label, but classifier is confident → understood.
     * Cluster 1: completely orthogonal feature → not understood. */
    FeatureVector unknown_fv = fv_unit(1); /* orthogonal → low similarity */
    std::vector<Cluster> clusters = {
        make_cluster(0, wall_fv),
        make_cluster(1, unknown_fv),
    };
    std::vector<std::string> labels = {"", ""};

    float c = compute_completeness(clusters, labels, clf);
    EXPECT_GT(c, 0.0f);   /* at least one understood */
    EXPECT_LT(c, 1.0f);   /* not all */
}

TEST(Completeness, UpdatesAfterNewLabel)
{
    KNNClassifier clf;
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {make_cluster(0, fv), make_cluster(1, fv)};
    std::vector<std::string> labels = {"", ""};

    float before = compute_completeness(clusters, labels, clf);
    EXPECT_FLOAT_EQ(before, 0.0f);

    labels[0] = "wall";
    float after = compute_completeness(clusters, labels, clf);
    EXPECT_GT(after, before);
}

/* ========================================================================== */
/* compute_discovery_queue tests                                                */
/* ========================================================================== */

TEST(DiscoveryQueue, EmptyLabeledFeaturesReturnsEmpty)
{
    KNNClassifier clf;
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {make_cluster(0, fv)};
    std::vector<std::string> labels = {""};
    std::vector<FeatureVector> labeled_fvs; /* empty */

    auto q = compute_discovery_queue(clusters, labels, labeled_fvs, clf, /*page=*/1);
    EXPECT_TRUE(q.empty());
}

TEST(DiscoveryQueue, KnownClustersExcluded)
{
    KNNClassifier clf;
    FeatureVector fv = fv_unit(0);
    std::vector<Cluster> clusters = {make_cluster(0, fv), make_cluster(1, fv)};
    /* cluster 0 user-labeled, cluster 1 unknown */
    std::vector<std::string> labels = {"wall", ""};
    std::vector<FeatureVector> labeled_fvs = {fv};

    auto q = compute_discovery_queue(clusters, labels, labeled_fvs, clf, /*page=*/0);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].cluster_id, 1);
}

TEST(DiscoveryQueue, PageAndBoundsCarriedThrough)
{
    KNNClassifier clf;
    FeatureVector fv = fv_unit(1); /* unknown — different dim from labeled */
    std::vector<Cluster> clusters = {make_cluster(7, fv, 10, 20, 30, 40)};
    std::vector<std::string> labels = {""};
    std::vector<FeatureVector> labeled_fvs = {fv_unit(0)};

    auto q = compute_discovery_queue(clusters, labels, labeled_fvs, clf, /*page=*/3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].cluster_id, 7);
    EXPECT_EQ(q[0].page, 3);
    EXPECT_FLOAT_EQ(q[0].bounds[0], 10.0f);
    EXPECT_FLOAT_EQ(q[0].bounds[1], 20.0f);
    EXPECT_FLOAT_EQ(q[0].bounds[2], 30.0f);
    EXPECT_FLOAT_EQ(q[0].bounds[3], 40.0f);
}

TEST(DiscoveryQueue, SortedFurthestFirst)
{
    /* Three unlabeled clusters with orthogonal feature vectors.
     * labeled_fv = unit(0).
     * cluster A = unit(0) → sim=1.0, dist=0.0 (closest)
     * cluster B = unit(1) → sim=0.0, dist=1.0 (furthest)
     * cluster C = unit(2) → sim=0.0, dist=1.0 (equally far)
     * Both B and C should appear before A. */
    KNNClassifier clf;
    FeatureVector lf  = fv_unit(0);
    FeatureVector fv0 = fv_unit(0); /* same as labeled */
    FeatureVector fv1 = fv_unit(1); /* orthogonal */
    FeatureVector fv2 = fv_unit(2); /* orthogonal */

    std::vector<Cluster> clusters = {
        make_cluster(0, fv0),
        make_cluster(1, fv1),
        make_cluster(2, fv2),
    };
    std::vector<std::string> labels = {"", "", ""};
    std::vector<FeatureVector> labeled_fvs = {lf};

    auto q = compute_discovery_queue(clusters, labels, labeled_fvs, clf, /*page=*/0);
    ASSERT_EQ(q.size(), 3u);
    /* First two entries must have dist=1.0 (orthogonal to labeled). */
    EXPECT_FLOAT_EQ(q[0].max_distance, 1.0f);
    EXPECT_FLOAT_EQ(q[1].max_distance, 1.0f);
    /* Last entry is the near one. */
    EXPECT_FLOAT_EQ(q[2].cluster_id, 0);
    EXPECT_LT(q[2].max_distance, 1.0f);
}

TEST(DiscoveryQueue, ReturnsAtMostTopN)
{
    KNNClassifier clf;
    FeatureVector labeled_fv = fv_unit(0);
    std::vector<FeatureVector> labeled_fvs = {labeled_fv};

    std::vector<Cluster> clusters;
    std::vector<std::string> labels;
    for (int i = 0; i < 15; ++i) {
        clusters.push_back(make_cluster(i, fv_unit(1)));
        labels.push_back("");
    }

    auto q = compute_discovery_queue(clusters, labels, labeled_fvs, clf, /*page=*/0, /*top_n=*/10);
    EXPECT_EQ(q.size(), 10u);
}

TEST(DiscoveryQueue, UpdatesAfterNewLabel)
{
    /* After labeling a previously unknown cluster it should vanish from the queue. */
    KNNClassifier clf;
    FeatureVector lf  = fv_unit(0);
    FeatureVector fv1 = fv_unit(1);
    FeatureVector fv2 = fv_unit(2);

    std::vector<Cluster> clusters = {make_cluster(0, fv1), make_cluster(1, fv2)};
    std::vector<std::string> labels = {"", ""};
    std::vector<FeatureVector> labeled_fvs = {lf};

    auto q_before = compute_discovery_queue(clusters, labels, labeled_fvs, clf, 0);
    EXPECT_EQ(q_before.size(), 2u);

    labels[0] = "wall";
    labeled_fvs.push_back(fv1);
    auto q_after = compute_discovery_queue(clusters, labels, labeled_fvs, clf, 0);
    EXPECT_EQ(q_after.size(), 1u);
    EXPECT_EQ(q_after[0].cluster_id, 1);
}
