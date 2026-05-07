#ifndef CENSOR_BACKGROUND_WORKER_H
#define CENSOR_BACKGROUND_WORKER_H

#include "iclassifier.h"
#include "overlay/confidence_overlay.h"
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
 * -------------------------------------------------------------------------*/
class __attribute__((visibility("default"))) BackgroundWorker {
public:
    BackgroundWorker(std::shared_ptr<IClassifier> classifier,
                     ConfidenceOverlay& overlay);
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

    std::shared_ptr<IClassifier> classifier_;
    ConfidenceOverlay&           overlay_;

    std::thread             thread_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       stop_requested_{false};

    std::mutex              queue_mu_;
    std::condition_variable queue_cv_;
    std::queue<Job>         queue_;
};

} /* namespace censor */

#endif /* CENSOR_BACKGROUND_WORKER_H */
