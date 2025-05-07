/**
 * @file ExpiryManager.cpp
 * @brief Implementation of the ExpiryManager class
 */

#include "../market/ExpiryManager.hpp"
#include <algorithm>
#include <set>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <map>
#include <cctype>  // for tolower

namespace BoxStrategy {

// Helper function for case-insensitive string comparison
bool caseInsensitiveStringCompare(const std::string& str1, const std::string& str2) {
    if (str1.size() != str2.size()) {
        return false;
    }
    
    for (size_t i = 0; i < str1.size(); i++) {
        if (tolower(str1[i]) != tolower(str2[i])) {
            return false;
        }
    }
    
    return true;
}

// Helper function to check if a string starts with another string (case insensitive)
bool caseInsensitiveStartsWith(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) {
        return false;
    }
    
    return caseInsensitiveStringCompare(str.substr(0, prefix.size()), prefix);
}

ExpiryManager::ExpiryManager(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<MarketDataManager> marketDataManager,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_marketDataManager(marketDataManager),
    m_logger(logger) {
    
    m_logger->info("ExpiryManager initialized");
}

std::pair<std::vector<std::chrono::system_clock::time_point>, 
          std::vector<std::chrono::system_clock::time_point>> 
ExpiryManager::getExpiries(bool includeWeekly, bool includeMonthly) {
    std::vector<std::chrono::system_clock::time_point> weeklyExpiries;
    std::vector<std::chrono::system_clock::time_point> monthlyExpiries;
    
    // Get underlying from config
    std::string underlying = m_configManager->getStringValue("strategy/underlying", "NIFTY");
    std::string exchange = m_configManager->getStringValue("strategy/exchange", "NFO");
    
    m_logger->info("Getting expiries for underlying: {}, exchange: {}", underlying, exchange);
    
    // Get all option instruments for the underlying from Market Data Manager
    auto futureInstruments = m_marketDataManager->getInstrumentsByExchange(exchange);
    std::vector<InstrumentModel> instruments = futureInstruments.get();
    
    m_logger->info("Retrieved {} instruments from exchange {}", instruments.size(), exchange);
    
    // Debug: Log first few instruments to see what's being returned
    int debugCount = 0;
    for (const auto& instrument : instruments) {
        if (debugCount < 5) {
            m_logger->debug("Sample instrument: type={}, symbol={}, underlying={}, exchange={}", 
                         InstrumentModel::instrumentTypeToString(instrument.type),
                         instrument.tradingSymbol,
                         instrument.underlying,
                         instrument.exchange);
            debugCount++;
        }
    }
    
    // Filter for options of the given underlying
    std::set<std::chrono::system_clock::time_point> uniqueExpiries;
    int totalOptionCount = 0;
    int filteredOptionCount = 0;
    int niftyOptionsWithExpiryCount = 0;
    
    for (const auto& instrument : instruments) {
        // First check if it's an option
        if (instrument.type == InstrumentType::OPTION) {
            totalOptionCount++;
            
            // Check if it's for the specified underlying (case insensitive)
            bool isTargetOption = false;
            
            // Check underlying if available (case insensitive)
            if (!instrument.underlying.empty() && 
                 caseInsensitiveStringCompare(instrument.underlying, underlying)) {
                isTargetOption = true;
                m_logger->debug("Found option by underlying match: {}", instrument.tradingSymbol);
            }
            // Also check trading symbol (more reliable method, case insensitive)
            else if (caseInsensitiveStartsWith(instrument.tradingSymbol, underlying)) {
                isTargetOption = true;
                m_logger->debug("Found option by trading symbol: {}", instrument.tradingSymbol);
            }
            
            if (isTargetOption) {
                filteredOptionCount++;
                
                // Debug expiry value
                auto timeValue = std::chrono::system_clock::to_time_t(instrument.expiry);
                auto time_t_val = std::chrono::system_clock::to_time_t(instrument.expiry);
                std::tm* tm = std::localtime(&time_t_val);
                
                std::stringstream ss;
                ss << std::put_time(tm, "%Y-%m-%d");
                
                // Only add non-zero expiry dates
                if (timeValue > 0) {
                    niftyOptionsWithExpiryCount++;
                    uniqueExpiries.insert(instrument.expiry);
                    
                    m_logger->debug("Found option with valid expiry: tradingSymbol={}, expiry={}, strike={}, type={}",
                                 instrument.tradingSymbol,
                                 ss.str(),
                                 instrument.strikePrice,
                                 InstrumentModel::optionTypeToString(instrument.optionType));
                } else {
                    m_logger->warn("Found option with INVALID expiry: tradingSymbol={}, expiry time value={}",
                                 instrument.tradingSymbol, timeValue);
                }
            }
        }
    }
    
    m_logger->info("Found {} total options, {} filtered for {}, {} with valid expiry, {} unique expiry dates", 
                 totalOptionCount, filteredOptionCount, underlying, niftyOptionsWithExpiryCount, uniqueExpiries.size());
    
    // Debug: Print all unique expiry dates found
    for (const auto& expiry : uniqueExpiries) {
        auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
        std::tm* tm = std::localtime(&time_t_val);
        
        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d");
        
        m_logger->debug("Unique expiry date: {}", ss.str());
    }
    
    // Now classify expiries as weekly or monthly
    for (const auto& expiry : uniqueExpiries) {
        // Determine if this is a monthly expiry (last Thursday of the month)
        bool isMonthly = isLastThursdayOfMonth(expiry);
        
        // Update cache for this expiry
        std::string expiryKey = generateExpiryKey(expiry);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_monthlyExpiries[expiryKey] = isMonthly;
            m_weeklyExpiries[expiryKey] = !isMonthly;
        }
        
        auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
        std::tm* tm = std::localtime(&time_t_val);
        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d");
        m_logger->debug("Classified expiry {}: isMonthly={}, isWeekly={}", 
                      ss.str(), isMonthly, !isMonthly);
        
        if (isMonthly && includeMonthly) {
            monthlyExpiries.push_back(expiry);
        } else if (!isMonthly && includeWeekly) {
            weeklyExpiries.push_back(expiry);
        }
    }
    
    // Sort the expiries chronologically
    std::sort(weeklyExpiries.begin(), weeklyExpiries.end());
    std::sort(monthlyExpiries.begin(), monthlyExpiries.end());
    
    m_logger->info("Found {} weekly expiries and {} monthly expiries for {}", 
                 weeklyExpiries.size(), monthlyExpiries.size(), underlying);
    
    return {weeklyExpiries, monthlyExpiries};
}

std::vector<std::chrono::system_clock::time_point> ExpiryManager::refreshExpiries(
    const std::string& underlying, 
    const std::string& exchange) {
    
    m_logger->info("Refreshing expiries for {}:{}", underlying, exchange);
    
    // Get configuration
    bool includeWeekly = m_configManager->getBoolValue("expiry/include_weekly", true);
    bool includeMonthly = m_configManager->getBoolValue("expiry/include_monthly", true);
    
    // Get expiry classifications using the new method
    auto [weekly, monthly] = getExpiries(includeWeekly, includeMonthly);
    
    // Combine the results based on configuration
    std::vector<std::chrono::system_clock::time_point> result;
    
    if (includeWeekly) {
        result.insert(result.end(), weekly.begin(), weekly.end());
    }
    
    if (includeMonthly) {
        result.insert(result.end(), monthly.begin(), monthly.end());
    }
    
    // Sort the combined results
    std::sort(result.begin(), result.end());
    
    // Update cache
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = generateCacheKey(underlying, exchange);
        m_expiriesCache[key] = result;
    }
    
    m_logger->info("Found {} unique expiries for {}:{}", result.size(), underlying, exchange);
    return result;
}

std::vector<std::chrono::system_clock::time_point> ExpiryManager::getAvailableExpiries(
    const std::string& underlying, 
    const std::string& exchange) {
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string key = generateCacheKey(underlying, exchange);
    auto it = m_expiriesCache.find(key);
    
    if (it != m_expiriesCache.end() && !it->second.empty()) {
        m_logger->debug("Using cached expiries for {}:{}", underlying, exchange);
        return it->second;
    }
    
    // Release the lock before making the API call
    lock.~lock_guard();
    
    // Cache not found or empty, refresh
    return refreshExpiries(underlying, exchange);
}

std::vector<std::chrono::system_clock::time_point> ExpiryManager::filterExpiries(
    const std::string& underlying, 
    const std::string& exchange,
    const std::vector<std::chrono::system_clock::time_point>& expiries) {
    
    m_logger->debug("Filtering {} expiries for {}:{}", expiries.size(), underlying, exchange);
    
    // Get configuration
    bool includeWeekly = m_configManager->getBoolValue("expiry/include_weekly", true);
    bool includeMonthly = m_configManager->getBoolValue("expiry/include_monthly", true);
    int maxExpiries = m_configManager->getIntValue("expiry/max_count", 3);
    int minDaysToExpiry = m_configManager->getIntValue("expiry/min_days", 0); // Changed from 1 to 0
    int maxDaysToExpiry = m_configManager->getIntValue("expiry/max_days", 30);
    
    m_logger->debug("Expiry filter config: includeWeekly={}, includeMonthly={}, maxExpiries={}, minDays={}, maxDays={}",
                  includeWeekly, includeMonthly, maxExpiries, minDaysToExpiry, maxDaysToExpiry);
    
    std::vector<std::chrono::system_clock::time_point> filtered;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& expiry : expiries) {
        auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
        std::tm* tm = std::localtime(&time_t_val);
        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d");
        std::string expiryStr = ss.str();
        
        // Skip expiries that are too close or too far
        auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(expiry - now).count() / 24;
        if (daysDiff < minDaysToExpiry || daysDiff > maxDaysToExpiry) {
            m_logger->debug("Skipping expiry {} (days diff: {}, outside range [{}, {}])", 
                          expiryStr, daysDiff, minDaysToExpiry, maxDaysToExpiry);
            continue;
        }
        
        // Check if weekly/monthly should be included
        std::string expiryKey = generateExpiryKey(expiry);
        bool isMonthly = false;
        bool isWeekly = false;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto monthlyIt = m_monthlyExpiries.find(expiryKey);
            if (monthlyIt != m_monthlyExpiries.end()) {
                isMonthly = monthlyIt->second;
            } else {
                // If not found in cache, calculate it
                isMonthly = isLastThursdayOfMonth(expiry);
                m_monthlyExpiries[expiryKey] = isMonthly;
                m_weeklyExpiries[expiryKey] = !isMonthly;
            }
            
            auto weeklyIt = m_weeklyExpiries.find(expiryKey);
            if (weeklyIt != m_weeklyExpiries.end()) {
                isWeekly = weeklyIt->second;
            } else {
                // If not found in cache, it's the opposite of monthly
                isWeekly = !isMonthly;
                m_weeklyExpiries[expiryKey] = isWeekly;
            }
        }
        
        m_logger->debug("Expiry {}: isMonthly={}, isWeekly={}", expiryStr, isMonthly, isWeekly);
        
        if ((isMonthly && includeMonthly) || (isWeekly && includeWeekly)) {
            filtered.push_back(expiry);
            m_logger->debug("Added expiry {} to filtered list", expiryStr);
        } else {
            m_logger->debug("Skipping expiry {} (isMonthly={}, includeMonthly={}, isWeekly={}, includeWeekly={})", 
                          expiryStr, isMonthly, includeMonthly, isWeekly, includeWeekly);
        }
    }
    
    // Sort by date
    std::sort(filtered.begin(), filtered.end());
    
    // Limit to max count
    if (filtered.size() > static_cast<size_t>(maxExpiries)) {
        m_logger->debug("Limiting filtered expiries from {} to {}", filtered.size(), maxExpiries);
        filtered.resize(maxExpiries);
    }
    
    m_logger->info("Filtered to {} expiries for {}:{}", filtered.size(), underlying, exchange);
    return filtered;
}

bool ExpiryManager::isWeeklyExpiry(const std::chrono::system_clock::time_point& expiry) {
    std::string expiryKey = generateExpiryKey(expiry);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_weeklyExpiries.find(expiryKey);
    
    if (it != m_weeklyExpiries.end()) {
        return it->second;
    }
    
    // If not found, calculate
    bool isMonthly = isLastThursdayOfMonth(expiry);
    m_monthlyExpiries[expiryKey] = isMonthly;
    m_weeklyExpiries[expiryKey] = !isMonthly;
    
    return !isMonthly;
}

bool ExpiryManager::isMonthlyExpiry(const std::chrono::system_clock::time_point& expiry) {
    std::string expiryKey = generateExpiryKey(expiry);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_monthlyExpiries.find(expiryKey);
    
    if (it != m_monthlyExpiries.end()) {
        return it->second;
    }
    
    // If not found, calculate
    bool isMonthly = isLastThursdayOfMonth(expiry);
    m_monthlyExpiries[expiryKey] = isMonthly;
    m_weeklyExpiries[expiryKey] = !isMonthly;
    
    return isMonthly;
}

std::vector<std::chrono::system_clock::time_point> ExpiryManager::getNextExpiries(
    const std::string& underlying, 
    const std::string& exchange,
    size_t n) {
    
    auto availableExpiries = getAvailableExpiries(underlying, exchange);
    auto filteredExpiries = filterExpiries(underlying, exchange, availableExpiries);
    
    if (filteredExpiries.size() > n) {
        filteredExpiries.resize(n);
    }
    
    return filteredExpiries;
}

void ExpiryManager::clearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_expiriesCache.clear();
    m_weeklyExpiries.clear();
    m_monthlyExpiries.clear();
    
    m_logger->info("Expiry cache cleared");
}

std::string ExpiryManager::generateCacheKey(
    const std::string& underlying, const std::string& exchange) {
    
    return underlying + ":" + exchange;
}

std::string ExpiryManager::generateExpiryKey(
    const std::chrono::system_clock::time_point& expiry) {
    
    auto time_t = std::chrono::system_clock::to_time_t(expiry);
    std::tm* tm = std::localtime(&time_t);
    
    std::stringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d");
    
    return ss.str();
}

bool ExpiryManager::isLastThursdayOfMonth(
    const std::chrono::system_clock::time_point& expiry) {
    
    // Convert to time_t
    auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
    std::tm* tm = std::localtime(&time_t_val);
    
    // Check if it's a Thursday
    if (tm->tm_wday != 4) { // 0 = Sunday, 4 = Thursday
        return false;
    }
    
    // Check if it's the last Thursday of the month
    // Move to the next Thursday
    std::tm nextThursday = *tm;
    nextThursday.tm_mday += 7;
    std::time_t nextThursdayTime = std::mktime(&nextThursday);
    std::tm* nextThursdayTm = std::localtime(&nextThursdayTime);
    
    // If the next Thursday is in a different month, then this is the last Thursday
    return nextThursdayTm->tm_mon != tm->tm_mon;
}

}  // namespace BoxStrategy