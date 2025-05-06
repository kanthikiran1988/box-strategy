 /**
 * @file MarketDepthAnalyzer.hpp
 * @brief Analyzes market depth to identify potential slippage
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../utils/Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../market/MarketDataManager.hpp"
#include "../models/BoxSpreadModel.hpp"

namespace BoxStrategy {

/**
 * @class MarketDepthAnalyzer
 * @brief Analyzes market depth to identify potential slippage
 */
class MarketDepthAnalyzer {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param marketDataManager Market data manager
     * @param logger Logger instance
     */
    MarketDepthAnalyzer(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<MarketDataManager> marketDataManager,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~MarketDepthAnalyzer() = default;
    
    /**
     * @brief Calculate slippage for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Estimated slippage
     */
    double calculateSlippage(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate slippage for a single option
     * @param instrument Option instrument
     * @param quantity Quantity to trade
     * @param isBuy Whether the order is a buy or sell
     * @return Estimated slippage
     */
    double calculateOptionSlippage(const InstrumentModel& instrument, uint64_t quantity, bool isBuy);
    
    /**
     * @brief Check if sufficient liquidity is available
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return True if sufficient liquidity is available, false otherwise
     */
    bool hasSufficientLiquidity(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate available liquidity for a box spread
     * @param boxSpread Box spread model
     * @return Available liquidity (minimum across all legs)
     */
    uint64_t calculateAvailableLiquidity(const BoxSpreadModel& boxSpread);
    
    /**
     * @brief Refresh market depth data for a box spread
     * @param boxSpread Box spread model to update
     * @return Updated box spread with fresh market depth data
     */
    BoxSpreadModel refreshMarketDepth(BoxSpreadModel boxSpread);
    
    /**
     * @brief Calculate the bid-ask spread for an option
     * @param instrument Option instrument
     * @return Bid-ask spread as a percentage of the mid price
     */
    double calculateBidAskSpread(const InstrumentModel& instrument);
    
    /**
     * @brief Filter box spreads based on liquidity criteria
     * @param boxSpreads Vector of box spreads to filter
     * @param quantity Quantity to trade
     * @return Vector of box spreads with sufficient liquidity
     */
    std::vector<BoxSpreadModel> filterByLiquidity(
        const std::vector<BoxSpreadModel>& boxSpreads, uint64_t quantity);
    
    /**
     * @brief Sort box spreads by liquidity
     * @param boxSpreads Vector of box spreads to sort
     * @return Sorted vector of box spreads
     */
    std::vector<BoxSpreadModel> sortByLiquidity(
        const std::vector<BoxSpreadModel>& boxSpreads);

private:
    std::shared_ptr<ConfigManager> m_configManager;        ///< Configuration manager
    std::shared_ptr<MarketDataManager> m_marketDataManager; ///< Market data manager
    std::shared_ptr<Logger> m_logger;                      ///< Logger instance
};

}  // namespace BoxStrategy