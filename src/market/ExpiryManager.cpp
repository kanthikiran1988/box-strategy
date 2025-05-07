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
#include <regex>   // for regex pattern matching

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

// Improved is_last_thursday helper function using 7-day look-ahead
static bool is_last_thursday(const std::chrono::system_clock::time_point& date) {
    // Convert to time_t
    auto time_t_val = std::chrono::system_clock::to_time_t(date);
    std::tm* tm = std::localtime(&time_t_val);
    
    // Check if it's a Thursday
    if (tm->tm_wday != 4) { // 0 = Sunday, 4 = Thursday
        return false;
    }
    
    // Check if it's the last Thursday of the month by seeing if 7 days later is next month
    std::tm nextWeek = *tm;
    nextWeek.tm_mday += 7; // One week later
    std::time_t nextWeekTime = std::mktime(&nextWeek);
    std::tm* nextWeekTm = std::localtime(&nextWeekTime);
    
    // If the next week is in a different month, then this is the last Thursday
    return nextWeekTm->tm_mon != tm->tm_mon;
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
            
            // Check if it's for the specified underlying (improved matching)
            bool isTargetOption = false;
            
            // Method 1: Check underlying field directly (case insensitive)
            if (!instrument.underlying.empty() && 
                caseInsensitiveStringCompare(instrument.underlying, underlying)) {
                isTargetOption = true;
                m_logger->debug("Found option by underlying match: {}", instrument.tradingSymbol);
            }
            // Method 2: Check trading symbol for NIFTY-specific patterns
            else if (underlying == "NIFTY" && instrument.tradingSymbol.find("NIFTY") == 0) {
                // Check if it has either CE or PE suffix for options
                if (instrument.tradingSymbol.find("CE") != std::string::npos || 
                    instrument.tradingSymbol.find("PE") != std::string::npos) {
                    isTargetOption = true;
                    m_logger->debug("Found NIFTY option by pattern match: {}", instrument.tradingSymbol);
                }
            }
            // Method 3: General trading symbol prefix check (fallback)
            else if (caseInsensitiveStartsWith(instrument.tradingSymbol, underlying)) {
                isTargetOption = true;
                m_logger->debug("Found option by trading symbol prefix: {}", instrument.tradingSymbol);
            }
            
            if (isTargetOption) {
                filteredOptionCount++;
                
                // Enhanced expiry validation and fallback for NIFTY
                auto timeValue = std::chrono::system_clock::to_time_t(instrument.expiry);
                auto time_t_val = std::chrono::system_clock::to_time_t(instrument.expiry);
                std::tm* tm = std::localtime(&time_t_val);
                
                std::stringstream ss;
                ss << std::put_time(tm, "%Y-%m-%d");
                
                // If original expiry is invalid, try to extract from symbol for NIFTY
                if (timeValue <= 0 && underlying == "NIFTY" && 
                    instrument.tradingSymbol.find("NIFTY") == 0) {
                    // Try to extract expiry from symbol
                    // For NIFTY options, format might be NIFTY23JUN21xxxx
                    auto extractedExpiry = extractExpiryFromNiftySymbol(instrument.tradingSymbol);
                    if (extractedExpiry.time_since_epoch().count() > 0) {
                        m_logger->debug("Extracted expiry date from symbol {}: {}", 
                                      instrument.tradingSymbol, 
                                      InstrumentModel::formatDate(extractedExpiry));
                        
                        // Add to unique expiries if valid
                        uniqueExpiries.insert(extractedExpiry);
                        niftyOptionsWithExpiryCount++;
                    }
                }
                // Otherwise use original expiry if valid
                else if (timeValue > 0) {
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
    
    // FIX 1: Filter out past dates first
    std::vector<std::chrono::system_clock::time_point> upcoming;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& expiry : uniqueExpiries) {
        if (expiry > now) {
            upcoming.push_back(expiry);
        }
    }
    
    m_logger->debug("After filtering past dates: {} upcoming expiry dates", upcoming.size());
    
    // FIX 3: Simplify classification - identify weekly and monthly expiries
    for (const auto& expiry : upcoming) {
        // Use the improved is_last_thursday helper
        if (is_last_thursday(expiry)) {
            if (includeMonthly) {
                monthlyExpiries.push_back(expiry);
            }
        } else {
            // Must be a Thursday but not the last Thursday of the month
            auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
            std::tm* tm = std::localtime(&time_t_val);
            
            if (tm->tm_wday == 4 && includeWeekly) { // 4 = Thursday
                weeklyExpiries.push_back(expiry);
            }
        }
    }
    
    // Sort the expiries chronologically
    std::sort(weeklyExpiries.begin(), weeklyExpiries.end());
    std::sort(monthlyExpiries.begin(), monthlyExpiries.end());
    
    // FIX 5: Add more detailed debug logging
    m_logger->debug("weekly={} monthly={}", 
                  weeklyExpiries.size(), monthlyExpiries.size());
    
    m_logger->info("Found {} weekly expiries and {} monthly expiries for {}", 
                 weeklyExpiries.size(), monthlyExpiries.size(), underlying);
    
    return {weeklyExpiries, monthlyExpiries};
}

std::chrono::system_clock::time_point ExpiryManager::extractExpiryFromNiftySymbol(const std::string& symbol) {
    m_logger->debug("Attempting to extract expiry from symbol: {}", symbol);
    
    // Common formats for NIFTY:
    // NIFTY23JUN22 - YY MM DD
    // NIFTY2306 - YYMM
    
    std::chrono::system_clock::time_point result;
    
    // Try to match pattern like NIFTY23JUN22
    std::regex yearMonthDayPattern("NIFTY(\\d{2})([A-Z]{3})(\\d{2})");
    std::smatch matches;
    
    if (std::regex_search(symbol, matches, yearMonthDayPattern) && matches.size() == 4) {
        int year = std::stoi(matches[1].str()) + 2000; // Convert YY to YYYY
        std::string monthStr = matches[2].str();
        int day = std::stoi(matches[3].str());
        
        // Map month string to month number
        std::map<std::string, int> monthMap = {
            {"JAN", 0}, {"FEB", 1}, {"MAR", 2}, {"APR", 3},
            {"MAY", 4}, {"JUN", 5}, {"JUL", 6}, {"AUG", 7},
            {"SEP", 8}, {"OCT", 9}, {"NOV", 10}, {"DEC", 11}
        };
        
        if (monthMap.find(monthStr) != monthMap.end()) {
            int month = monthMap[monthStr];
            
            // Create expiry date
            std::tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon = month;
            tm.tm_mday = day;
            
            std::time_t time = std::mktime(&tm);
            result = std::chrono::system_clock::from_time_t(time);
            
            m_logger->debug("Extracted expiry from NIFTY symbol using YY-MMM-DD pattern: {}-{}-{}", 
                          year, monthStr, day);
        }
    }
    // Try to match pattern like NIFTY2306 (YYMM)
    else {
        std::regex yearMonthPattern("NIFTY(\\d{2})(\\d{2})");
        if (std::regex_search(symbol, matches, yearMonthPattern) && matches.size() == 3) {
            int year = std::stoi(matches[1].str()) + 2000; // Convert YY to YYYY
            int month = std::stoi(matches[2].str()) - 1;   // Convert 1-12 to 0-11
            
            // Find the last Thursday of this month
            std::tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon = month;
            tm.tm_mday = 1; // Start with first day
            
            // Get days in month by going to next month's first day - 1
            tm.tm_mon++; // Next month
            if (tm.tm_mon > 11) {
                tm.tm_mon = 0;
                tm.tm_year++;
            }
            std::time_t nextMonthTime = std::mktime(&tm);
            std::tm* nextMonth = std::localtime(&nextMonthTime);
            
            // Go back one day to get last day of current month
            nextMonth->tm_mday -= 1;
            std::time_t lastDayTime = std::mktime(nextMonth);
            std::tm* lastDay = std::localtime(&lastDayTime);
            
            // Find the last Thursday
            int lastDayOfMonth = lastDay->tm_mday;
            for (int day = lastDayOfMonth; day >= 1; day--) {
                lastDay->tm_mday = day;
                std::time_t currentDayTime = std::mktime(lastDay);
                std::tm* currentDay = std::localtime(&currentDayTime);
                
                if (currentDay->tm_wday == 4) { // Thursday = 4
                    result = std::chrono::system_clock::from_time_t(currentDayTime);
                    
                    m_logger->debug("Extracted expiry from NIFTY symbol using YYMM pattern: {}-{}, found last Thursday: {}", 
                                  year, month + 1, day);
                    break;
                }
            }
        }
    }
    
    return result;
}

std::vector<std::chrono::system_clock::time_point> ExpiryManager::refreshExpiries(
    const std::string& underlying, 
    const std::string& exchange) {
    
    m_logger->info("Refreshing expiries for {}:{}", underlying, exchange);
    
    // Get configuration
    bool includeWeekly = m_configManager->getBoolValue("expiry/include_weekly", true);
    bool includeMonthly = m_configManager->getBoolValue("expiry/include_monthly", true);
    
    // Get expiry classifications using the improved method
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
    int minDaysToExpiry = m_configManager->getIntValue("expiry/min_days", 1);
    int maxDaysToExpiry = m_configManager->getIntValue("expiry/max_days", 90);
    
    m_logger->debug("Expiry filter config: includeWeekly={}, includeMonthly={}, maxExpiries={}, minDays={}, maxDays={}",
                  includeWeekly, includeMonthly, maxExpiries, minDaysToExpiry, maxDaysToExpiry);
    
    std::vector<std::chrono::system_clock::time_point> filtered;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& expiry : expiries) {
        // Skip expiries that are too close or too far
        auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(expiry - now).count() / 24;
        if (daysDiff < minDaysToExpiry || daysDiff > maxDaysToExpiry) {
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
            }
            
            auto weeklyIt = m_weeklyExpiries.find(expiryKey);
            if (weeklyIt != m_weeklyExpiries.end()) {
                isWeekly = weeklyIt->second;
            }
        }
        
        if ((isMonthly && includeMonthly) || (isWeekly && includeWeekly)) {
            filtered.push_back(expiry);
        }
    }
    
    // Sort by date
    std::sort(filtered.begin(), filtered.end());
    
    // Limit to max count
    if (filtered.size() > static_cast<size_t>(maxExpiries)) {
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
    
    // If not found, calculate using improved is_last_thursday helper
    auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
    std::tm* tm = std::localtime(&time_t_val);
    
    // Check if it's a Thursday (4 = Thursday)
    bool isThursday = (tm->tm_wday == 4);
    bool isMonthly = isThursday && is_last_thursday(expiry);
    
    m_monthlyExpiries[expiryKey] = isMonthly;
    m_weeklyExpiries[expiryKey] = !isMonthly && isThursday; // Only Thursdays are valid weekly expiries
    
    return !isMonthly && isThursday;
}

bool ExpiryManager::isMonthlyExpiry(const std::chrono::system_clock::time_point& expiry) {
    std::string expiryKey = generateExpiryKey(expiry);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_monthlyExpiries.find(expiryKey);
    
    if (it != m_monthlyExpiries.end()) {
        return it->second;
    }
    
    // If not found, calculate using improved is_last_thursday helper
    auto time_t_val = std::chrono::system_clock::to_time_t(expiry);
    std::tm* tm = std::localtime(&time_t_val);
    
    // Check if it's a Thursday (4 = Thursday)
    bool isThursday = (tm->tm_wday == 4);
    bool isMonthly = isThursday && is_last_thursday(expiry);
    
    m_monthlyExpiries[expiryKey] = isMonthly;
    m_weeklyExpiries[expiryKey] = !isMonthly && isThursday; // Only Thursdays are valid weekly expiries
    
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

// We're replacing this with the improved is_last_thursday helper function
bool ExpiryManager::isLastThursdayOfMonth(
    const std::chrono::system_clock::time_point& expiry) {
    
    // Now just delegate to our improved helper
    return is_last_thursday(expiry);
}

}  // namespace BoxStrategy