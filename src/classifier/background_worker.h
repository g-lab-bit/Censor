#pragma once



#include "censor_visibility.h"
#include "iclassifier.h"
#include "overlay/confidence_overlay.h"
#include "overlay/debug_viz_data.h"
#include "censor_types.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace censor {

/* ---------------------------------------------------------------------------
 * BackgroundWorker — classifies clusters asynchronously on a worker thread.
 *
 * The caller submits Cluster values; the worker calls IClassifier::predict()
 * (the read path — safe for concurrent access) and writes results to
 * ConfidenceOverlay. The UI thread can call snapshot() on the overlay at any
 * time without blocking classification.
 *
 * Lifecycle: start() → submit(…) × N → stop().
 * stop() drains the pending queue before the thread exits.
 *
 * Ownership:
 *   overlay_  — shared_ptr; worker keeps the overlay alive for its lifetime.
 *   debug_viz — raw pointer; MUST outlive the worker, or be null.
 *               stop() MUST be called (or dtor complete) before debug_viz dies.
 * -------------------------------------------------------------------------*/
class CENSOR_API BackgroundWorker {
public:
    /* debug_viz: optional pointer to DebugVizData for inference timing.
     * Timing is recorded only when debug_viz is non-null and debug mode enabled.
     * IMPORTANT: debug_viz must outlive the worker (or be null); call stop()
     * before destroying debug_viz to guarantee the worker thread has exited. */
    BackgroundWorker(std::shared_ptr<IClassifier>    classifier,
                     std::shared_ptr<ConfidenceOverlay> overlay,
                     DebugVizData* debug_viz = nullptr);
    ~BackgroundWorker();

    /* Start the worker thread (idempotent; no-op if already running). */
    void start();

    /* Signal stop and block until the queue is drained and thread has exited. */
    void stop();

    /* Enqueue a cluster for classification. Safe from any thread. */
    void submit(const Cluster& cluster);

    bool is_running() const noexcept { return running_.load(); }

private:
    struct Job { Cluster cluster; };

    void worker_loop();

    std::shared_ptr<IClassifier>    classifier_;
    std::shared_ptr<ConfidenceOverlay> overlay_;
    DebugVizData*                    debug_viz_;

    std::thread             thread_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       stop_requested_{false};

    std::mutex              queue_mu_;
    std::condition_variable queue_cv_;
    std::queue<Job>         queue_;
};

} /* namespace censor */

