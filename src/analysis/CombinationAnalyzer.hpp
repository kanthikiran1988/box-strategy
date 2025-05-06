/**
 * @file CombinationAnalyzer.hpp
 * @brief Analyzes different option combinations to find profitable box spreads
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>
#include <future>
#include <map>
#include "../utils/Logger.hpp"
#include "../utils/ThreadPool.hpp"
#include "../config/ConfigManager.hpp"
#include "../market/MarketDataManager.hpp"
#include "../market/ExpiryManager.hpp"
#include "../models/BoxSpreadModel.hpp"
#include "../risk/FeeCalculator.hpp"
#include "../risk/RiskCalculator.hpp"

namespace BoxStrategy {

/**
 * @class CombinationAnalyzer
 * @brief Analyzes different option combinations to find profitable box spreads
 */
class CombinationAnalyzer {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param marketDataManager Market data manager
     * @param expiryManager Expiry manager
     * @param feeCalculator Fee calculator
     * @param riskCalculator Risk calculator
     * @param threadPool Thread pool
     * @param logger Logger instance
     */
    CombinationAnalyzer(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<MarketDataManager> marketDataManager,
        std::shared_ptr<ExpiryManager> expiryManager,
        std::shared_ptr<FeeCalculator> feeCalculator,
        std::shared_ptr<RiskCalculator> riskCalculator,
        std::shared_ptr<ThreadPool> threadPool,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~CombinationAnalyzer() = default;
    
    /**
     * @brief Find profitable box spreads for an underlying
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @return Vector of profitable box spreads
     */
    std::vector<BoxSpreadModel> findProfitableSpreads(
        const std::string& underlying, 
        const std::string& exchange);
    
    /**
     * @brief Find profitable box spreads for an underlying and expiry
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @return Vector of profitable box spreads
     */
    std::vector<BoxSpreadModel> findProfitableSpreadsForExpiry(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Find all available strike prices for an underlying and expiry
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @return Vector of available strike prices
     */
    std::vector<double> findAvailableStrikes(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Generate all possible strike combinations for box spreads
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @param strikes Vector of available strike prices
     * @return Vector of strike price pairs
     */
    std::vector<std::pair<double, double>> generateStrikeCombinations(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry,
        const std::vector<double>& strikes);
    
    /**
     * @brief Analyze a box spread for profitability
     * @param boxSpread Box spread to analyze
     * @return Analyzed box spread with profitability metrics
     */
    BoxSpreadModel analyzeBoxSpread(BoxSpreadModel boxSpread);
    
    /**
     * @brief Get the required options for a box spread
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @param lowerStrike Lower strike price
     * @param higherStrike Higher strike price
     * @return Box spread model with options populated
     */
    BoxSpreadModel getBoxSpreadOptions(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry,
        double lowerStrike, 
        double higherStrike);
    
    /**
     * @brief Find the most liquid options for a strike price
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @param strike Strike price
     * @param optionType Option type (call/put)
     * @return Most liquid option
     */
    InstrumentModel findMostLiquidOption(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry,
        double strike, 
        OptionType optionType);
    
    /**
     * @brief Filter box spreads based on profitability criteria
     * @param boxSpreads Vector of box spreads to filter
     * @return Vector of profitable box spreads
     */
    std::vector<BoxSpreadModel> filterProfitableSpreads(
        const std::vector<BoxSpreadModel>& boxSpreads);
    
    /**
     * @brief Sort box spreads by profitability
     * @param boxSpreads Vector of box spreads to sort
     * @return Sorted vector of box spreads
     */
    std::vector<BoxSpreadModel> sortByProfitability(
        const std::vector<BoxSpreadModel>& boxSpreads);

private:
    std::shared_ptr<ConfigManager> m_configManager;        ///< Configuration manager
    std::shared_ptr<MarketDataManager> m_marketDataManager; ///< Market data manager
    std::shared_ptr<ExpiryManager> m_expiryManager;        ///< Expiry manager
    std::shared_ptr<FeeCalculator> m_feeCalculator;        ///< Fee calculator
    std::shared_ptr<RiskCalculator> m_riskCalculator;      ///< Risk calculator
    std::shared_ptr<ThreadPool> m_threadPool;              ///< Thread pool
    std::shared_ptr<Logger> m_logger;                      ///< Logger instance
    
    // Cache for available strikes and instruments
    std::unordered_map<std::string, std::vector<double>> m_strikesCache;
    std::unordered_map<std::string, std::map<double, std::pair<InstrumentModel, InstrumentModel>>> m_optionsCache;
    
    // Mutex for thread safety
    std::mutex m_cacheMutex;
    
    /**
     * @brief Generate cache key for strikes
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @return Cache key
     */
    std::string generateStrikesCacheKey(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Generate cache key for options
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiry Expiry date
     * @param strike Strike price
     * @return Cache key
     */
    std::string generateOptionsCacheKey(
        const std::string& underlying, 
        const std::string& exchange,
        const std::chrono::system_clock::time_point& expiry,
        double strike);
};

}  // namespace BoxStrategy