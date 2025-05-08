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

void ThreadPool::resize(size_t numThreads) {
    // Handle case where size doesn't need to change
    if (numThreads == m_workers.size()) {
        return;
    }
    
    // Log the resize operation
    m_logger->info("Resizing thread pool from {} to {} threads", m_workers.size(), numThreads);
    
    // Handle case where we need to increase the number of threads
    if (numThreads > m_workers.size()) {
        size_t oldSize = m_workers.size();
        for (size_t i = oldSize; i < numThreads; ++i) {
            m_workers.emplace_back([this, i] {
                m_logger->debug("Worker thread {} started (resized pool)", i);
                this->workerThread();
                m_logger->debug("Worker thread {} stopped", i);
            });
        }
        m_logger->info("Added {} new worker threads", numThreads - oldSize);
        return;
    }
    
    // Handle case where we need to decrease the number of threads
    if (numThreads < m_workers.size()) {
        // Create a temporary vector to hold threads to be removed
        size_t numToRemove = m_workers.size() - numThreads;
        std::vector<std::thread> threadsToRemove;
        
        // Move threads that we want to remove to the temporary vector
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            
            // Set a flag to indicate which threads should exit
            m_threadsToStop = numToRemove;
            
            // Wake up all worker threads so they can check if they should exit
            m_condition.notify_all();
        }
        
        // Wait a bit for threads to exit naturally when they check m_threadsToStop
        // This is a non-blocking approach that allows threads to complete their current tasks
        m_logger->info("Scaling down thread pool by {} threads", numToRemove);
        
        // Wait for thread count to decrease
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool timedOut = false;
        
        while (m_threadsToStop > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Check for timeout to avoid hanging indefinitely
            if (std::chrono::steady_clock::now() > timeout) {
                m_logger->warn("Timeout while waiting for threads to stop naturally");
                timedOut = true;
                break;
            }
        }
        
        // If we still have threads to remove, we need to force them to stop
        if (timedOut) {
            m_logger->warn("Some threads didn't stop naturally, cleaning up remaining threads");
        }
        
        // Remove any stopped threads from our worker vector
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            
            // Erase any threads that have completed (detached)
            m_workers.erase(
                std::remove_if(m_workers.begin(), m_workers.end(), 
                               [](const std::thread& t) { return !t.joinable(); }),
                m_workers.end());
            
            // Reset the counter for next resize operation
            m_threadsToStop = 0;
            
            m_logger->info("Thread pool resized to {} threads", m_workers.size());
        }
    }
}

void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        bool shouldExit = false;
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            // Wait for a task, stop signal, or thread reduction signal
            m_condition.wait(lock, [this] {
                return m_stop || !m_tasks.empty() || m_threadsToStop > 0;
            });
            
            // Check if we should stop due to thread pool shutdown
            if (m_stop && m_tasks.empty()) {
                return;
            }
            
            // Check if we should stop due to thread pool resizing
            if (m_threadsToStop > 0) {
                // Decrement the counter before exiting
                m_threadsToStop--;
                shouldExit = true;
            } else if (!m_tasks.empty()) {
                // Get the next task
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
        }
        
        // Check if this thread should exit due to resizing
        if (shouldExit) {
            return;
        }
        
        // Execute the task if we have one
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                m_logger->error("Exception in worker thread: {}", e.what());
            } catch (...) {
                m_logger->error("Unknown exception in worker thread");
            }
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