/**
 * @file ExpiryManager.hpp
 * @brief Manages available option expiries
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include "../utils/Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../market/MarketDataManager.hpp"
#include "../models/InstrumentModel.hpp"

namespace BoxStrategy {

/**
 * @class ExpiryManager
 * @brief Manages available option expiries for box spreads
 */
class ExpiryManager {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param marketDataManager Market data manager
     * @param logger Logger instance
     */
    ExpiryManager(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<MarketDataManager> marketDataManager,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~ExpiryManager() = default;
    
    /**
     * @brief Get weekly and monthly expiries 
     * @param includeWeekly Whether to include weekly expiries
     * @param includeMonthly Whether to include monthly expiries
     * @return Pair of vectors containing weekly and monthly expiry dates
     */
    std::pair<std::vector<std::chrono::system_clock::time_point>, 
              std::vector<std::chrono::system_clock::time_point>> 
    getExpiries(bool includeWeekly, bool includeMonthly);
    
    /**
     * @brief Refresh the list of available expiries
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @return Vector of available expiry dates
     */
    std::vector<std::chrono::system_clock::time_point> refreshExpiries(
        const std::string& underlying, 
        const std::string& exchange);
    
    /**
     * @brief Get available expiries for an underlying
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @return Vector of available expiry dates
     */
    std::vector<std::chrono::system_clock::time_point> getAvailableExpiries(
        const std::string& underlying, 
        const std::string& exchange);
    
    /**
     * @brief Filter expiries based on configuration
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param expiries Vector of available expiry dates
     * @return Vector of filtered expiry dates
     */
    std::vector<std::chrono::system_clock::time_point> filterExpiries(
        const std::string& underlying, 
        const std::string& exchange,
        const std::vector<std::chrono::system_clock::time_point>& expiries);
    
    /**
     * @brief Check if an expiry is a weekly expiry
     * @param expiry Expiry date
     * @return True if it's a weekly expiry, false otherwise
     */
    bool isWeeklyExpiry(const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Check if an expiry is a monthly expiry
     * @param expiry Expiry date
     * @return True if it's a monthly expiry, false otherwise
     */
    bool isMonthlyExpiry(const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Get the next N expiries
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param n Number of expiries to get
     * @return Vector of the next N expiry dates
     */
    std::vector<std::chrono::system_clock::time_point> getNextExpiries(
        const std::string& underlying, 
        const std::string& exchange,
        size_t n);
    
    /**
     * @brief Clear the expiry cache
     */
    void clearCache();

private:
    std::shared_ptr<ConfigManager> m_configManager;        ///< Configuration manager
    std::shared_ptr<MarketDataManager> m_marketDataManager; ///< Market data manager
    std::shared_ptr<Logger> m_logger;                      ///< Logger instance
    
    // Cache of available expiries for each underlying/exchange
    std::unordered_map<std::string, std::vector<std::chrono::system_clock::time_point>> m_expiriesCache;
    
    // Maps to track weekly and monthly expiries
    std::unordered_map<std::string, bool> m_weeklyExpiries;
    std::unordered_map<std::string, bool> m_monthlyExpiries;
    
    // Lock for thread safety
    std::mutex m_mutex;
    
    /**
     * @brief Generate key for expiry cache
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @return Cache key
     */
    std::string generateCacheKey(const std::string& underlying, const std::string& exchange);
    
    /**
     * @brief Generate key for expiry type maps
     * @param expiry Expiry date
     * @return Type map key
     */
    std::string generateExpiryKey(const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Is the expiry date of the month the last Thursday?
     * @param expiry Expiry date
     * @return True if it's the last Thursday of the month, false otherwise
     */
    bool isLastThursdayOfMonth(const std::chrono::system_clock::time_point& expiry);
};

}  // namespace BoxStrategy