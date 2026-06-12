#include "background_worker.h"

#include <chrono>

namespace censor {

BackgroundWorker::BackgroundWorker(std::shared_ptr<IClassifier> classifier,
                                   ConfidenceOverlay& overlay,
                                   DebugVizData* debug_viz)
    : classifier_(std::move(classifier)), overlay_(overlay), debug_viz_(debug_viz)
{}

BackgroundWorker::~BackgroundWorker()
{
    stop();
}

void BackgroundWorker::start()
{
    /* ce-654 — CAS gate: exactly one concurrent start() wins; the rest
     * return. (Plain load-then-store let two starters both pass the guard
     * and the second overwrite thread_, losing a joinable thread.) */
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    /* Reap a previously self-exited worker (worker_loop clears running_ on
     * exit but the thread object stays joinable until joined). */
    if (thread_.joinable()) thread_.join();
    stop_requested_.store(false);
    thread_ = std::thread(&BackgroundWorker::worker_loop, this);
}

void BackgroundWorker::stop()
{
    if (!running_.load() && !thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(queue_mu_);
        stop_requested_.store(true);
    }
    queue_cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void BackgroundWorker::submit(const Cluster& cluster)
{
    {
        std::lock_guard<std::mutex> lock(queue_mu_);
        queue_.push({cluster});
    }
    queue_cv_.notify_one();
}

void BackgroundWorker::worker_loop()
{
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queue_mu_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || stop_requested_.load();
            });
            if (stop_requested_.load() && queue_.empty()) break;
            job = std::move(queue_.front());
            queue_.pop();
        }

        const Cluster& c = job.cluster;
        ClassifyResult result;

        if (debug_viz_ && debug_viz_->is_debug_enabled()) {
            auto t0 = std::chrono::steady_clock::now();
            result  = classifier_->predict(c.features);
            auto t1 = std::chrono::steady_clock::now();
            float us = std::chrono::duration<float, std::micro>(t1 - t0).count();
            debug_viz_->record_inference(c.cluster_id, us);
        } else {
            result = classifier_->predict(c.features);
        }

        overlay_.update(c.cluster_id, c.bounds, result);
    }
    running_.store(false);
}

} /* namespace censor */
