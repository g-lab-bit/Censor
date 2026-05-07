#include "background_worker.h"

namespace censor {

BackgroundWorker::BackgroundWorker(std::shared_ptr<IClassifier> classifier,
                                   ConfidenceOverlay& overlay)
    : classifier_(std::move(classifier)), overlay_(overlay)
{}

BackgroundWorker::~BackgroundWorker()
{
    stop();
}

void BackgroundWorker::start()
{
    if (running_.load()) return;
    stop_requested_.store(false);
    running_.store(true);
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

        const Cluster&  c      = job.cluster;
        ClassifyResult  result = classifier_->predict(c.features);
        overlay_.update(c.cluster_id, c.bounds, result);
    }
    running_.store(false);
}

} /* namespace censor */
