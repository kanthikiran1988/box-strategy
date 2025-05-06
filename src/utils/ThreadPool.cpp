/**
 * @file ThreadPool.cpp
 * @brief Implementation of the ThreadPool class
 */

#include "../utils/ThreadPool.hpp"

namespace BoxStrategy {

ThreadPool::ThreadPool(size_t numThreads, std::shared_ptr<Logger> logger)
    : m_stop(false), m_activeTaskCount(0), m_logger(logger) {
    m_logger->info("Initializing thread pool with {} threads", numThreads);
    
    // Create worker threads
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this, i] {
            m_logger->debug("Worker thread {} started", i);
            this->workerThread();
            m_logger->debug("Worker thread {} stopped", i);
        });
    }
}

ThreadPool::~ThreadPool() {
    m_logger->info("Shutting down thread pool");
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    
    // Wake up all worker threads
    m_condition.notify_all();
    
    // Join all worker threads
    for (std::thread& worker : m_workers) {
        worker.join();
    }
    
    m_logger->info("Thread pool shutdown complete");
}

void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            // Wait for a task or stop signal
            m_condition.wait(lock, [this] {
                return m_stop || !m_tasks.empty();
            });
            
            // Check if we should stop
            if (m_stop && m_tasks.empty()) {
                return;
            }
            
            // Get the next task
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        
        // Execute the task
        try {
            task();
        } catch (const std::exception& e) {
            m_logger->error("Exception in worker thread: {}", e.what());
        } catch (...) {
            m_logger->error("Unknown exception in worker thread");
        }
    }
}

size_t ThreadPool::getNumThreads() const {
    return m_workers.size();
}

size_t ThreadPool::getQueueSize() const {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    return m_tasks.size();
}

size_t ThreadPool::getActiveTaskCount() const {
    return m_activeTaskCount;
}

void ThreadPool::waitForCompletion() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completionCondition.wait(lock, [this] {
        return m_activeTaskCount == 0 && m_tasks.empty();
    });
}

}  // namespace BoxStrategy