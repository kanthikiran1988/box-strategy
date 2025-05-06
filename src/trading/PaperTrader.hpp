/**
 * @file PaperTrader.hpp
 * @brief Simulates trading without actual execution
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "../utils/Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../market/MarketDataManager.hpp"
#include "../models/OrderModel.hpp"
#include "../models/BoxSpreadModel.hpp"

namespace BoxStrategy {

/**
 * @struct PaperTradeResult
 * @brief Result of a paper trade
 */
struct PaperTradeResult {
    std::string id;                          ///< Unique identifier for the paper trade
    std::string symbol;                      ///< Trading symbol
    std::string exchange;                    ///< Exchange
    TransactionType transactionType;         ///< Transaction type (buy/sell)
    uint64_t quantity;                       ///< Quantity traded
    double executionPrice;                   ///< Execution price
    double slippage;                         ///< Slippage
    double fees;                             ///< Fees
    double profit;                           ///< Profit/loss
    std::chrono::system_clock::time_point executionTime; ///< Execution time
    bool isBox;                              ///< Whether this is a box spread trade
    std::string boxId;                       ///< Box spread ID if applicable
};

/**
 * @class PaperTrader
 * @brief Simulates trading without actual execution
 */
class PaperTrader {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param marketDataManager Market data manager
     * @param logger Logger instance
     */
    PaperTrader(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<MarketDataManager> marketDataManager,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~PaperTrader() = default;
    
    /**
     * @brief Simulate a box spread trade
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Paper trade result
     */
    PaperTradeResult simulateBoxSpreadTrade(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Simulate an individual order
     * @param order Order model
     * @return Paper trade result
     */
    PaperTradeResult simulateOrder(const OrderModel& order);
    
    /**
     * @brief Get all paper trade results
     * @return Vector of paper trade results
     */
    std::vector<PaperTradeResult> getAllResults() const;
    
    /**
     * @brief Get paper trade results for a specific box spread
     * @param boxId Box spread ID
     * @return Vector of paper trade results
     */
    std::vector<PaperTradeResult> getResultsForBox(const std::string& boxId) const;
    
    /**
     * @brief Get the total profit/loss from all paper trades
     * @return Total profit/loss
     */
    double getTotalProfitLoss() const;
    
    /**
     * @brief Get the total profit/loss for a specific box spread
     * @param boxId Box spread ID
     * @return Total profit/loss
     */
    double getBoxProfitLoss(const std::string& boxId) const;
    
    /**
     * @brief Clear all paper trade results
     */
    void clearResults();

private:
    std::shared_ptr<ConfigManager> m_configManager;        ///< Configuration manager
    std::shared_ptr<MarketDataManager> m_marketDataManager; ///< Market data manager
    std::shared_ptr<Logger> m_logger;                      ///< Logger instance
    
    std::vector<PaperTradeResult> m_tradeResults;  ///< Paper trade results
    
    mutable std::mutex m_mutex;  ///< Mutex for thread safety
    
    /**
     * @brief Generate a unique ID for a paper trade
     * @return Unique ID
     */
    std::string generateTradeId() const;
    
    /**
     * @brief Calculate slippage for an order
     * @param order Order model
     * @return Estimated slippage
     */
    double calculateSlippage(const OrderModel& order) const;
    
    /**
     * @brief Calculate fees for an order
     * @param order Order model
     * @return Estimated fees
     */
    double calculateFees(const OrderModel& order) const;
};

}  // namespace BoxStrategy