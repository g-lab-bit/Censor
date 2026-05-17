#include <gtest/gtest.h>

#include "overlay/completeness.h"
#include "overlay/confidence_overlay.h"
#include "overlay/debug_viz_data.h"
#include "overlay/discovery_queue.h"
#include "classifier/background_worker.h"
#include "classifier/iclassifier.h"
#include "classifier/knn_classifier.h"
#include "censor_types.h"

#include <cfloat>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

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

/* ========================================================================== */
/* ConfidenceOverlay tests                                                     */
/* ========================================================================== */

TEST(ConfidenceOverlay, BelowThresholdNotAdded)
{
    ConfidenceOverlay ov;
    float bounds[4] = {0, 0, 10, 10};
    ClassifyResult r{"wall", 0.9f, /*above_threshold=*/false};
    ov.update(1, bounds, r);
    EXPECT_TRUE(ov.snapshot().empty());
}

TEST(ConfidenceOverlay, BoundsConfidenceLabelCarryThrough)
{
    ConfidenceOverlay ov;
    float bounds[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    ClassifyResult r{"duct", 0.75f, /*above_threshold=*/true};
    ov.update(42, bounds, r);

    auto snap = ov.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].cluster_id, 42);
    EXPECT_EQ(snap[0].label, "duct");
    EXPECT_FLOAT_EQ(snap[0].confidence, 0.75f);
    EXPECT_FLOAT_EQ(snap[0].bounds[0], 1.0f);
    EXPECT_FLOAT_EQ(snap[0].bounds[1], 2.0f);
    EXPECT_FLOAT_EQ(snap[0].bounds[2], 3.0f);
    EXPECT_FLOAT_EQ(snap[0].bounds[3], 4.0f);
    /* alpha channel must equal confidence */
    EXPECT_FLOAT_EQ(snap[0].color[3], 0.75f);
}

TEST(ConfidenceOverlay, UserLabelConfidenceOneIsUserLabeled)
{
    ConfidenceOverlay ov;
    float bounds[4] = {0, 0, 5, 5};
    ov.update_user_label(7, bounds, "pipe");

    auto snap = ov.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].label, "pipe");
    EXPECT_FLOAT_EQ(snap[0].confidence, 1.0f);
    EXPECT_TRUE(snap[0].is_user_labeled);
}

TEST(ConfidenceOverlay, RemoveDeletesEntry)
{
    ConfidenceOverlay ov;
    float bounds[4] = {0, 0, 1, 1};
    ClassifyResult r{"wall", 0.8f, true};
    ov.update(3, bounds, r);
    ASSERT_EQ(ov.snapshot().size(), 1u);

    ov.remove(3);
    EXPECT_TRUE(ov.snapshot().empty());
}

TEST(ConfidenceOverlay, ClearRemovesAllEntries)
{
    ConfidenceOverlay ov;
    float bounds[4] = {0, 0, 1, 1};
    ClassifyResult r{"wall", 0.8f, true};
    ov.update(1, bounds, r);
    ov.update(2, bounds, r);
    ASSERT_EQ(ov.snapshot().size(), 2u);

    ov.clear();
    EXPECT_TRUE(ov.snapshot().empty());
}

/* ========================================================================== */
/* BackgroundWorker tests                                                      */
/* ========================================================================== */

namespace {

/* Minimal IClassifier stub — always returns a fixed result above threshold. */
class StubClassifier : public IClassifier {
public:
    explicit StubClassifier(const std::string& label, float confidence)
        : label_(label), confidence_(confidence) {}

    ClassifyResult predict(const FeatureVector&) const override
    {
        return {label_, confidence_, /*above_threshold=*/true};
    }
    void add_example(const FeatureVector&, const std::string&, bool) override {}
    std::map<std::string, int> label_counts() const override { return {}; }

private:
    std::string label_;
    float       confidence_;
};

} /* anonymous namespace */

TEST(BackgroundWorker, ClassifiesAndUpdatesOverlay)
{
    ConfidenceOverlay ov;
    auto clf = std::make_shared<StubClassifier>("wall", 0.9f);
    BackgroundWorker worker(clf, ov);

    Cluster c;
    c.cluster_id = 1;
    c.bounds[0] = 0; c.bounds[1] = 0; c.bounds[2] = 10; c.bounds[3] = 10;

    worker.start();
    worker.submit(c);
    worker.stop();

    auto snap = ov.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].cluster_id, 1);
    EXPECT_EQ(snap[0].label, "wall");
    EXPECT_FLOAT_EQ(snap[0].confidence, 0.9f);
}

TEST(BackgroundWorker, MultipleJobsAllProcessed)
{
    ConfidenceOverlay ov;
    auto clf = std::make_shared<StubClassifier>("duct", 0.8f);
    BackgroundWorker worker(clf, ov);

    worker.start();
    for (int i = 0; i < 5; ++i) {
        Cluster c;
        c.cluster_id = i;
        c.bounds[0] = static_cast<float>(i); c.bounds[1] = 0;
        c.bounds[2] = static_cast<float>(i) + 1; c.bounds[3] = 1;
        worker.submit(c);
    }
    worker.stop();

    EXPECT_EQ(ov.snapshot().size(), 5u);
}

TEST(BackgroundWorker, StopDrainsQueueBeforeExit)
{
    ConfidenceOverlay ov;
    auto clf = std::make_shared<StubClassifier>("pipe", 0.7f);
    BackgroundWorker worker(clf, ov);

    worker.start();
    /* Submit a burst then stop — stop() must drain before returning. */
    for (int i = 0; i < 10; ++i) {
        Cluster c;
        c.cluster_id = i;
        c.bounds[0] = 0; c.bounds[1] = 0; c.bounds[2] = 1; c.bounds[3] = 1;
        worker.submit(c);
    }
    worker.stop();

    /* All 10 clusters should have been processed (last write wins per id,
     * but all 10 unique ids → 10 entries since each id is unique). */
    EXPECT_EQ(ov.snapshot().size(), 10u);
    EXPECT_FALSE(worker.is_running());
}

/* ========================================================================== */
/* DebugVizData tests                                                          */
/* ========================================================================== */

TEST(DebugVizData, FeatureOverlay_NotFound)
{
    DebugVizData dvd;
    FeatureOverlayData result = dvd.get_feature_overlay(99);
    EXPECT_EQ(result.cluster_id, 99);
    EXPECT_FALSE(result.found);
}

TEST(DebugVizData, FeatureOverlay_ReturnsAllTwelveDims)
{
    DebugVizData dvd;
    FeatureVector fv{};
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i)
        fv.dims[i] = static_cast<float>(i + 1);

    Cluster c = make_cluster(5, fv);
    dvd.register_cluster(c);

    FeatureOverlayData result = dvd.get_feature_overlay(5);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.cluster_id, 5);
    for (int i = 0; i < FEATURE_DIM_COUNT; ++i) {
        EXPECT_FLOAT_EQ(result.features[i].value, static_cast<float>(i + 1));
        EXPECT_NE(result.features[i].name, nullptr);
        EXPECT_GT(std::strlen(result.features[i].name), 0u);
    }
}

TEST(DebugVizData, FeatureOverlay_ClearRemovesClusters)
{
    DebugVizData dvd;
    dvd.register_cluster(make_cluster(1, fv_unit(0)));
    dvd.clear();
    EXPECT_FALSE(dvd.get_feature_overlay(1).found);
}

TEST(DebugVizData, Histogram_EmptyInput)
{
    std::vector<float> empty;
    HistogramData result = DebugVizData::compute_histogram(empty);
    EXPECT_TRUE(result.bins.empty());
}

TEST(DebugVizData, Histogram_SingleWidth_OneBin)
{
    std::vector<float> widths(10, 0.5f);
    HistogramData result = DebugVizData::compute_histogram(widths, 0.1f);
    ASSERT_FALSE(result.bins.empty());
    int total = 0;
    for (const auto& b : result.bins) total += b.count;
    EXPECT_EQ(total, 10);
    EXPECT_EQ(result.bands.band_count, 1);
}

TEST(DebugVizData, Histogram_TwoPeaks_TwoBands)
{
    /* Thin strokes ~0.25pt (20 samples) and thick strokes ~2.0pt (20 samples).
     * Should produce 2 detected bands with a threshold between the peaks. */
    std::vector<float> widths;
    for (int i = 0; i < 20; ++i) widths.push_back(0.25f);
    for (int i = 0; i < 20; ++i) widths.push_back(2.0f);

    HistogramData result = DebugVizData::compute_histogram(widths, 0.1f);

    int total = 0;
    for (const auto& b : result.bins) total += b.count;
    EXPECT_EQ(total, 40);
    EXPECT_GE(result.bands.band_count, 2);
    /* Threshold between the two peaks must lie between the cluster centres. */
    EXPECT_GT(result.bands.thresholds[0], 0.25f);
    EXPECT_LT(result.bands.thresholds[0], 2.0f);
}

TEST(DebugVizData, ClusterBounds_EmptyRegistry)
{
    DebugVizData dvd;
    auto result = dvd.get_cluster_bounds(0, 0, FLT_MAX, FLT_MAX);
    EXPECT_TRUE(result.empty());
}

TEST(DebugVizData, ClusterBounds_FullViewport_ReturnsAll)
{
    DebugVizData dvd;
    dvd.register_cluster(make_cluster(1, fv_unit(0),  0,  0, 10, 10));
    dvd.register_cluster(make_cluster(2, fv_unit(0), 20, 20, 30, 30));
    dvd.register_cluster(make_cluster(3, fv_unit(0), 50, 50, 60, 60));

    auto result = dvd.get_cluster_bounds(0, 0, FLT_MAX, FLT_MAX);
    EXPECT_EQ(result.size(), 3u);
}

TEST(DebugVizData, ClusterBounds_ViewportFiltersCorrectly)
{
    DebugVizData dvd;
    /* Cluster 1 fully inside [0,0,15,15]; cluster 2 outside. */
    dvd.register_cluster(make_cluster(1, fv_unit(0),  0,  0, 10, 10));
    dvd.register_cluster(make_cluster(2, fv_unit(0), 20, 20, 30, 30));

    auto result = dvd.get_cluster_bounds(0, 0, 15, 15);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].cluster_id, 1);
    EXPECT_FLOAT_EQ(result[0].bounds[0],  0.0f);
    EXPECT_FLOAT_EQ(result[0].bounds[2], 10.0f);
}

TEST(DebugVizData, ClusterBounds_OverlapEdge_Included)
{
    DebugVizData dvd;
    /* Cluster straddles the right edge of the viewport — should be included. */
    dvd.register_cluster(make_cluster(7, fv_unit(0), 8, 0, 12, 5));
    auto result = dvd.get_cluster_bounds(0, 0, 10, 10);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].cluster_id, 7);
}

/* ========================================================================== */
/* DebugVizData — debug mode flag + inference timing (API 4)                  */
/* ========================================================================== */

TEST(DebugVizData, DebugMode_DefaultOff)
{
    DebugVizData dvd;
    EXPECT_FALSE(dvd.is_debug_enabled());
}

TEST(DebugVizData, DebugMode_Toggle)
{
    DebugVizData dvd;
    dvd.set_debug_enabled(true);
    EXPECT_TRUE(dvd.is_debug_enabled());
    dvd.set_debug_enabled(false);
    EXPECT_FALSE(dvd.is_debug_enabled());
}

TEST(DebugVizData, InferenceTiming_NotFoundWhenDebugOff)
{
    DebugVizData dvd;
    /* record_inference is a no-op when debug is disabled */
    dvd.record_inference(1, 42.0f);
    EXPECT_FALSE(dvd.get_inference_timing(1).found);
}

TEST(DebugVizData, InferenceTiming_NotFoundForUnknownCluster)
{
    DebugVizData dvd;
    dvd.set_debug_enabled(true);
    auto result = dvd.get_inference_timing(99);
    EXPECT_EQ(result.cluster_id, 99);
    EXPECT_FALSE(result.found);
}

TEST(DebugVizData, InferenceTiming_RecordedAndRetrieved)
{
    DebugVizData dvd;
    dvd.set_debug_enabled(true);
    dvd.record_inference(7, 123.5f);

    auto result = dvd.get_inference_timing(7);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.cluster_id, 7);
    EXPECT_FLOAT_EQ(result.elapsed_us, 123.5f);
}

TEST(DebugVizData, InferenceTiming_UpdateOverwritesPrevious)
{
    DebugVizData dvd;
    dvd.set_debug_enabled(true);
    dvd.record_inference(3, 50.0f);
    dvd.record_inference(3, 75.0f);

    EXPECT_FLOAT_EQ(dvd.get_inference_timing(3).elapsed_us, 75.0f);
}

TEST(DebugVizData, InferenceTiming_ClearRemovesAll)
{
    DebugVizData dvd;
    dvd.set_debug_enabled(true);
    dvd.record_inference(1, 10.0f);
    dvd.record_inference(2, 20.0f);
    dvd.clear();

    EXPECT_FALSE(dvd.get_inference_timing(1).found);
    EXPECT_FALSE(dvd.get_inference_timing(2).found);
}

TEST(BackgroundWorker, RecordsTimingWhenDebugEnabled)
{
    ConfidenceOverlay ov;
    DebugVizData dvd;
    dvd.set_debug_enabled(true);

    auto clf = std::make_shared<StubClassifier>("wall", 0.9f);
    BackgroundWorker worker(clf, ov, &dvd);

    Cluster c;
    c.cluster_id = 5;
    c.bounds[0] = 0; c.bounds[1] = 0; c.bounds[2] = 10; c.bounds[3] = 10;

    worker.start();
    worker.submit(c);
    worker.stop();

    auto t = dvd.get_inference_timing(5);
    ASSERT_TRUE(t.found);
    EXPECT_EQ(t.cluster_id, 5);
    EXPECT_GE(t.elapsed_us, 0.0f); /* non-negative elapsed time */
}

TEST(BackgroundWorker, NoTimingWhenDebugDisabled)
{
    ConfidenceOverlay ov;
    DebugVizData dvd;
    /* debug disabled by default */

    auto clf = std::make_shared<StubClassifier>("wall", 0.9f);
    BackgroundWorker worker(clf, ov, &dvd);

    Cluster c;
    c.cluster_id = 6;
    c.bounds[0] = 0; c.bounds[1] = 0; c.bounds[2] = 5; c.bounds[3] = 5;

    worker.start();
    worker.submit(c);
    worker.stop();

    EXPECT_FALSE(dvd.get_inference_timing(6).found);
}

TEST(BackgroundWorker, NoTimingWhenNoDebugViz)
{
    /* Existing usage without DebugVizData should still work correctly. */
    ConfidenceOverlay ov;
    auto clf = std::make_shared<StubClassifier>("duct", 0.8f);
    BackgroundWorker worker(clf, ov); /* no debug_viz */

    Cluster c;
    c.cluster_id = 1;
    c.bounds[0] = 0; c.bounds[1] = 0; c.bounds[2] = 1; c.bounds[3] = 1;

    worker.start();
    worker.submit(c);
    worker.stop();

    EXPECT_EQ(ov.snapshot().size(), 1u);
}

/* ========================================================================== */
/* confidence_to_rgba tests                                                    */
/* ========================================================================== */

TEST(ConfidenceToRgba, HighConfidenceIsGreen)
{
    auto c = confidence_to_rgba(0.90f);
    EXPECT_GT(c[1], c[0]); /* g > r */
    EXPECT_GT(c[1], c[2]); /* g > b */
    EXPECT_FLOAT_EQ(c[3], 0.90f);
}

TEST(ConfidenceToRgba, MediumConfidenceIsYellow)
{
    auto c = confidence_to_rgba(0.60f);
    EXPECT_GT(c[0], 0.5f); /* r high */
    EXPECT_GT(c[1], 0.5f); /* g high */
    EXPECT_LT(c[2], 0.5f); /* b low  */
    EXPECT_FLOAT_EQ(c[3], 0.60f);
}

TEST(ConfidenceToRgba, LowConfidenceIsRed)
{
    auto c = confidence_to_rgba(0.20f);
    EXPECT_GT(c[0], c[1]); /* r > g */
    EXPECT_GT(c[0], c[2]); /* r > b */
    EXPECT_FLOAT_EQ(c[3], 0.20f);
}

TEST(ConfidenceToRgba, BoundaryAt075IsGreen)
{
    auto c = confidence_to_rgba(0.75f);
    EXPECT_GT(c[1], c[0]);
}

TEST(ConfidenceToRgba, BoundaryAt040IsYellow)
{
    auto c = confidence_to_rgba(0.40f);
    EXPECT_GT(c[0], 0.5f);
    EXPECT_GT(c[1], 0.5f);
    EXPECT_LT(c[2], 0.5f);
}

/* ========================================================================== */
/* ConfidenceOverlay::snapshot_filtered tests                                 */
/* ========================================================================== */

TEST(SnapshotFiltered, DefaultStateReturnsAll)
{
    ConfidenceOverlay ov;
    float b[4] = {0, 0, 1, 1};
    ov.update(1, b, {"wall", 0.9f, true});
    ov.update(2, b, {"duct", 0.5f, true});
    ov.update_user_label(3, b, "pipe");

    OverlayUIState state; /* predictions_visible=true, min_confidence=0 */
    EXPECT_EQ(ov.snapshot_filtered(state).size(), 3u);
}

TEST(SnapshotFiltered, PredictionsHiddenExcludesAI)
{
    ConfidenceOverlay ov;
    float b[4] = {0, 0, 1, 1};
    ov.update(1, b, {"wall", 0.9f, true});           /* AI prediction */
    ov.update_user_label(2, b, "pipe");              /* user label    */

    OverlayUIState state{/*predictions_visible=*/false, /*min_confidence=*/0.0f};
    auto snap = ov.snapshot_filtered(state);
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].cluster_id, 2);
    EXPECT_TRUE(snap[0].is_user_labeled);
}

TEST(SnapshotFiltered, ThresholdFiltersLowConfidence)
{
    ConfidenceOverlay ov;
    float b[4] = {0, 0, 1, 1};
    ov.update(1, b, {"wall", 0.9f, true});
    ov.update(2, b, {"duct", 0.3f, true});
    ov.update(3, b, {"pipe", 0.6f, true});

    OverlayUIState state{true, /*min_confidence=*/0.5f};
    auto snap = ov.snapshot_filtered(state);
    ASSERT_EQ(snap.size(), 2u);
    for (const auto& e : snap)
        EXPECT_GE(e.confidence, 0.5f);
}

TEST(SnapshotFiltered, UserLabelsNotFilteredByThreshold)
{
    /* User labels always pass even when their confidence (1.0) would
     * normally pass anyway — but ensure the flag is honoured and no
     * threshold is applied to them regardless. */
    ConfidenceOverlay ov;
    float b[4] = {0, 0, 1, 1};
    ov.update_user_label(1, b, "wall");

    OverlayUIState state{true, /*min_confidence=*/0.99f};
    auto snap = ov.snapshot_filtered(state);
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_TRUE(snap[0].is_user_labeled);
}

TEST(SnapshotFiltered, ColorsAreTrafficLight)
{
    ConfidenceOverlay ov;
    float b[4] = {0, 0, 1, 1};
    ov.update(1, b, {"wall", 0.9f,  true}); /* high   → green  */
    ov.update(2, b, {"duct", 0.55f, true}); /* medium → yellow */
    ov.update(3, b, {"pipe", 0.20f, true}); /* low    → red    */

    OverlayUIState state;
    auto snap = ov.snapshot_filtered(state);
    ASSERT_EQ(snap.size(), 3u);

    for (const auto& e : snap) {
        auto expected = confidence_to_rgba(e.confidence);
        EXPECT_FLOAT_EQ(e.color[0], expected[0]);
        EXPECT_FLOAT_EQ(e.color[1], expected[1]);
        EXPECT_FLOAT_EQ(e.color[2], expected[2]);
        EXPECT_FLOAT_EQ(e.color[3], expected[3]);
    }
}
