#include "thread_pool.hpp"

ThreadPool::ThreadPool(std::size_t num_threads, std::size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_task_.notify_all();
    cv_space_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();
}

bool ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        cv_space_.wait(lock, [this] {
            return stopping_ || tasks_.size() < max_queue_size_;
        });
        if (stopping_) {
            return false;
        }
        tasks_.push(std::move(task));
    }
    cv_task_.notify_one();
    return true;
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_task_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        cv_space_.notify_one();
        if (task) {
            task();
        }
    }
}
