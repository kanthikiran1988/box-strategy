/**
 * @file ThreadPool.hpp
 * @brief Thread pool for parallel task execution
 */

#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <type_traits>
#include <algorithm>  // For std::remove_if
#include "../utils/Logger.hpp"

namespace BoxStrategy {

/**
 * @class ThreadPool
 * @brief Thread pool for parallel task execution
 */
class ThreadPool {
public:
    /**
     * @brief Constructor
     * @param numThreads Number of worker threads
     * @param logger Logger instance
     */
    ThreadPool(size_t numThreads, std::shared_ptr<Logger> logger);
    
    /**
     * @brief Destructor
     */
    ~ThreadPool();
    
    /**
     * @brief Enqueue a task
     * @param task Task to execute
     * @return Future result of the task
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    /**
     * @brief Resize the thread pool
     * @param numThreads New number of worker threads
     */
    void resize(size_t numThreads);
    
    /**
     * @brief Get the number of worker threads
     * @return Number of worker threads
     */
    size_t getNumThreads() const;
    
    /**
     * @brief Get the number of queued tasks
     * @return Number of queued tasks
     */
    size_t getQueueSize() const;
    
    /**
     * @brief Get the number of active tasks
     * @return Number of active tasks
     */
    size_t getActiveTaskCount() const;
    
    /**
     * @brief Wait for all tasks to complete
     */
    void waitForCompletion();
    
    /**
     * @brief Get the optimal number of threads for computation tasks
     * @param factor Adjustment factor (0.0-1.0) to control thread usage percentage
     * @return Optimal number of threads
     */
    static size_t getOptimalThreadCount(float factor = 0.75f) {
        // Get the number of hardware threads available
        unsigned int hwThreads = std::thread::hardware_concurrency();
        
        // If we can't determine the hardware threads, use a reasonable default
        if (hwThreads == 0) {
            hwThreads = 4;
        }
        
        // Apply the factor to avoid using 100% of CPU resources
        size_t optimalThreads = static_cast<size_t>(hwThreads * factor);
        
        // Ensure we have at least 1 thread
        return std::max<size_t>(1, optimalThreads);
    }

private:
    /**
     * @brief Worker thread function
     */
    void workerThread();
    
    std::vector<std::thread> m_workers;           ///< Worker threads
    std::queue<std::function<void()>> m_tasks;    ///< Task queue
    
    mutable std::mutex m_queueMutex;              ///< Mutex for task queue
    std::condition_variable m_condition;          ///< Condition variable for task queue
    std::condition_variable m_completionCondition;///< Condition variable for completion
    
    std::atomic<bool> m_stop;                     ///< Whether to stop the thread pool
    std::atomic<size_t> m_activeTaskCount;        ///< Number of active tasks
    std::atomic<size_t> m_threadsToStop{0};       ///< Counter for threads that should exit during resizing
    
    std::shared_ptr<Logger> m_logger;             ///< Logger instance
};

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // Don't allow enqueueing after stopping the pool
        if (m_stop) {
            throw std::runtime_error("Cannot enqueue task on stopped ThreadPool");
        }
        
        m_tasks.emplace([task, this]() {
            m_activeTaskCount++;
            (*task)();
            m_activeTaskCount--;
            if (m_activeTaskCount == 0 && m_tasks.empty()) {
                m_completionCondition.notify_all();
            }
        });
    }
    m_condition.notify_one();
    return res;
}

}  // namespace BoxStrategy