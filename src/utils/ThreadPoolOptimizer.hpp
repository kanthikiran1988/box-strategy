#pragma once

#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include "ThreadPool.hpp"
#include "../utils/Logger.hpp"

namespace BoxStrategy {

/**
 * @class ThreadPoolOptimizer
 * @brief Optimizes thread pool usage for different workloads
 */
class ThreadPoolOptimizer {
public:
    /**
     * @brief Constructor
     * @param threadPool ThreadPool to optimize
     * @param logger Logger instance
     */
    ThreadPoolOptimizer(std::shared_ptr<ThreadPool> threadPool, std::shared_ptr<Logger> logger)
        : m_threadPool(threadPool), m_logger(logger) {}
    
    /**
     * @brief Calculate optimal batch size for a given workload
     * @param totalItems Total number of items to process
     * @param minBatchSize Minimum batch size
     * @param maxBatchSize Maximum batch size
     * @return Optimal batch size
     */
    size_t calculateOptimalBatchSize(size_t totalItems, size_t minBatchSize = 1, size_t maxBatchSize = 100) {
        // Get the number of threads in the thread pool
        size_t numThreads = m_threadPool->getNumThreads();
        
        // Aim for 2-4 batches per thread for balanced workload
        size_t targetBatchCount = numThreads * 3;
        size_t batchSize = totalItems / targetBatchCount;
        
        // Clamp between min and max values
        batchSize = std::max(minBatchSize, std::min(batchSize, maxBatchSize));
        
        // Ensure we don't create batches smaller than the minimum
        if (batchSize < minBatchSize) {
            batchSize = minBatchSize;
        }
        
        // Log the decision
        m_logger->debug("Calculated optimal batch size: {} for {} items across {} threads",
                       batchSize, totalItems, numThreads);
        
        return batchSize;
    }
    
    /**
     * @brief Monitor and report progress on batch processing
     * @param totalItems Total number of items to process
     * @param processedItemsCounter Atomic counter for processed items
     * @param reportIntervalSec Interval in seconds between progress reports
     * @param label Label for the progress report
     * @return Function that can be called to stop monitoring
     */
    std::function<void()> monitorProgress(
        size_t totalItems,
        std::atomic<size_t>& processedItemsCounter,
        double reportIntervalSec = 5.0,
        const std::string& label = "Progress"
    ) {
        // Create a monitoring thread that periodically reports progress
        std::atomic<bool> keepRunning(true);
        
        std::thread monitorThread([this, totalItems, &processedItemsCounter, 
                                 reportIntervalSec, label, &keepRunning]() {
            auto startTime = std::chrono::high_resolution_clock::now();
            auto lastReportTime = startTime;
            
            while (keepRunning.load() && processedItemsCounter.load() < totalItems) {
                // Sleep for a short interval to reduce CPU usage
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                
                auto now = std::chrono::high_resolution_clock::now();
                auto timeSinceLastReport = std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - lastReportTime).count();
                
                // Check if it's time to report progress
                if (timeSinceLastReport >= reportIntervalSec) {
                    auto totalElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                        now - startTime).count();
                    
                    size_t completed = processedItemsCounter.load();
                    
                    if (completed > 0 && totalItems > 0) {
                        double percentComplete = (double)completed / totalItems * 100.0;
                        double itemsPerSecond = (double)completed / std::max(1.0, totalElapsed);
                        double estimatedSecondsRemaining = (totalItems - completed) / 
                                                         std::max(0.1, itemsPerSecond);
                        
                        m_logger->info("{}: {:.1f}% ({}/{}) - {:.1f} items/sec - Est. remaining: {:.0f} sec", 
                                     label, percentComplete, completed, totalItems, 
                                     itemsPerSecond, estimatedSecondsRemaining);
                    }
                    
                    lastReportTime = now;
                }
            }
            
            // Final report
            if (processedItemsCounter.load() >= totalItems) {
                auto endTime = std::chrono::high_resolution_clock::now();
                auto totalElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                    endTime - startTime).count();
                
                double itemsPerSecond = (double)totalItems / std::max(1.0, totalElapsed);
                
                m_logger->info("{} completed: {} items in {:.1f} seconds ({:.1f} items/sec)",
                             label, totalItems, totalElapsed, itemsPerSecond);
            }
        });
        
        // Detach the thread so it runs independently
        monitorThread.detach();
        
        // Return a function to stop the monitoring
        return [&keepRunning]() {
            keepRunning.store(false);
        };
    }
    
    /**
     * @brief Create a batched workload processor
     * 
     * This method takes work items, divides them into optimal batches, and processes them
     * in parallel with progress reporting.
     * 
     * @param workItems Vector of work items to process
     * @param processItemFunc Function to process a single work item
     * @param batchProcessingFunc Optional function to process a batch of items together
     * @param minBatchSize Minimum batch size
     * @param maxBatchSize Maximum batch size
     * @param progressLabel Label for progress reporting
     */
    template<typename T, typename ProcessFunc, typename BatchFunc = std::function<void(const std::vector<T>&)>>
    std::vector<typename std::result_of<ProcessFunc(T)>::type> processBatchedWorkload(
        const std::vector<T>& workItems,
        ProcessFunc processItemFunc,
        BatchFunc batchProcessingFunc = nullptr,
        size_t minBatchSize = 1,
        size_t maxBatchSize = 100,
        const std::string& progressLabel = "Processing"
    ) {
        using result_type = typename std::result_of<ProcessFunc(T)>::type;
        
        if (workItems.empty()) {
            return {};
        }
        
        // Calculate optimal batch size
        size_t batchSize = calculateOptimalBatchSize(workItems.size(), minBatchSize, maxBatchSize);
        m_logger->info("Processing {} items in batches of up to {} items", workItems.size(), batchSize);
        
        // Set up progress tracking
        std::atomic<size_t> processedItems(0);
        auto stopProgress = monitorProgress(workItems.size(), processedItems, 5.0, progressLabel);
        
        // Create a queue of work batches
        std::vector<std::vector<T>> batches;
        for (size_t i = 0; i < workItems.size(); i += batchSize) {
            size_t batchEnd = std::min(i + batchSize, workItems.size());
            batches.emplace_back(workItems.begin() + i, workItems.begin() + batchEnd);
        }
        
        // Process each batch
        std::mutex resultsMutex;
        std::vector<result_type> results;
        results.reserve(workItems.size());
        
        std::vector<std::future<void>> futures;
        
        for (const auto& batch : batches) {
            futures.push_back(m_threadPool->enqueue([this, &batch, &processItemFunc, &batchProcessingFunc,
                                                    &processedItems, &resultsMutex, &results]() {
                // If a batch processing function was provided, call it first
                if (batchProcessingFunc) {
                    batchProcessingFunc(batch);
                }
                
                // Process each item in the batch
                std::vector<result_type> batchResults;
                batchResults.reserve(batch.size());
                
                for (const auto& item : batch) {
                    batchResults.push_back(processItemFunc(item));
                    processedItems++;
                }
                
                // Add batch results to the global results
                {
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    results.insert(results.end(), batchResults.begin(), batchResults.end());
                }
            }));
        }
        
        // Wait for all batches to complete
        for (auto& future : futures) {
            future.get();
        }
        
        // Stop progress reporting
        stopProgress();
        
        return results;
    }
    
private:
    std::shared_ptr<ThreadPool> m_threadPool; ///< ThreadPool to optimize
    std::shared_ptr<Logger> m_logger;         ///< Logger instance
};

}  // namespace BoxStrategy 