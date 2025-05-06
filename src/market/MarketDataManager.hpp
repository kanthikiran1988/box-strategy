/**
 * @file MarketDataManager.hpp
 * @brief Manages market data from the Zerodha Kite Connect API
 */

#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include <future>
#include <chrono>
#include <queue>
#include <fstream>
#include <filesystem>
#include "../utils/Logger.hpp"
#include "../utils/HttpClient.hpp"
#include "../auth/AuthManager.hpp"
#include "../models/InstrumentModel.hpp"
#include "../config/ConfigManager.hpp"

namespace BoxStrategy {

/**
 * @class MarketDataManager
 * @brief Manages market data from the Zerodha Kite Connect API
 */
class MarketDataManager {
public:
    /**
     * @brief Constructor
     * @param authManager Authentication manager
     * @param httpClient HTTP client
     * @param logger Logger instance
     */
    MarketDataManager(
        std::shared_ptr<AuthManager> authManager,
        std::shared_ptr<HttpClient> httpClient,
        std::shared_ptr<Logger> logger,
        std::shared_ptr<ConfigManager> configManager
    );
    
    /**
     * @brief Destructor
     */
    ~MarketDataManager();
    
    /**
     * @brief Get all instruments
     * @return Future with vector of instruments
     */
    std::future<std::vector<InstrumentModel>> getAllInstruments();
    
    /**
     * @brief Get instruments by exchange
     * @param exchange Exchange name
     * @return Future with vector of instruments
     */
    std::future<std::vector<InstrumentModel>> getInstrumentsByExchange(const std::string& exchange);
    
    /**
     * @brief Get instrument by token
     * @param instrumentToken Instrument token
     * @return Future with instrument
     */
    std::future<InstrumentModel> getInstrumentByToken(uint64_t instrumentToken);
    
    /**
     * @brief Get instrument by trading symbol and exchange
     * @param tradingSymbol Trading symbol
     * @param exchange Exchange name
     * @return Future with instrument
     */
    std::future<InstrumentModel> getInstrumentBySymbol(const std::string& tradingSymbol, 
                                                     const std::string& exchange);
    
    /**
     * @brief Get option chain for a specific underlying and expiry
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param expiry Expiry date
     * @param exchange Exchange name (default "NFO")
     * @param minStrike Minimum strike price (optional)
     * @param maxStrike Maximum strike price (optional)
     * @return Future with vector of option instruments
     */
    std::future<std::vector<InstrumentModel>> getOptionChain(
        const std::string& underlying,
        const std::chrono::system_clock::time_point& expiry,
        const std::string& exchange = "NFO",
        double minStrike = 0.0,
        double maxStrike = 0.0);
    
    /**
     * @brief Get option chain with live market data for a specific underlying and expiry
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param expiry Expiry date
     * @param exchange Exchange name (default "NFO")
     * @param minStrike Minimum strike price (optional)
     * @param maxStrike Maximum strike price (optional)
     * @return Future with vector of option instruments with live quotes
     */
    std::future<std::vector<InstrumentModel>> getOptionChainWithQuotes(
        const std::string& underlying,
        const std::chrono::system_clock::time_point& expiry,
        const std::string& exchange = "NFO",
        double minStrike = 0.0,
        double maxStrike = 0.0);
    
    /**
     * @brief Get filtered option chain based on spot price and configurable range
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param expiry Expiry date
     * @param exchange Exchange name (default "NFO")
     * @return Future with vector of filtered option instruments
     */
    std::future<std::vector<InstrumentModel>> getFilteredOptionChain(
        const std::string& underlying,
        const std::chrono::system_clock::time_point& expiry,
        const std::string& exchange = "NFO");
    
    /**
     * @brief Get filtered option chain with quotes based on spot price and configurable range
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param expiry Expiry date
     * @param exchange Exchange name (default "NFO")
     * @return Future with vector of filtered option instruments with quotes
     */
    std::future<std::vector<InstrumentModel>> getFilteredOptionChainWithQuotes(
        const std::string& underlying,
        const std::chrono::system_clock::time_point& expiry,
        const std::string& exchange = "NFO");
    
    /**
     * @brief Get quote for an instrument
     * @param instrumentToken Instrument token
     * @return Future with instrument model containing quote data
     */
    std::future<InstrumentModel> getQuote(uint64_t instrumentToken);
    
    /**
     * @brief Get quotes for multiple instruments
     * @param instrumentTokens Vector of instrument tokens
     * @return Future with map of instrument token to instrument model
     */
    std::future<std::unordered_map<uint64_t, InstrumentModel>> getQuotes(
        const std::vector<uint64_t>& instrumentTokens);
    
    /**
     * @brief Get last traded price for an instrument
     * @param instrumentToken Instrument token
     * @return Future with last traded price
     */
    std::future<double> getLTP(uint64_t instrumentToken);
    
    /**
     * @brief Get last traded prices for multiple instruments
     * @param instrumentTokens Vector of instrument tokens
     * @return Future with map of instrument token to last traded price
     */
    std::future<std::unordered_map<uint64_t, double>> getLTPs(
        const std::vector<uint64_t>& instrumentTokens);
    
    /**
     * @brief Get OHLC data for an instrument
     * @param instrumentToken Instrument token
     * @return Future with OHLC data
     */
    std::future<std::tuple<double, double, double, double>> getOHLC(uint64_t instrumentToken);
    
    /**
     * @brief Get OHLC data for multiple instruments
     * @param instrumentTokens Vector of instrument tokens
     * @return Future with map of instrument token to OHLC data
     */
    std::future<std::unordered_map<uint64_t, std::tuple<double, double, double, double>>> getOHLCs(
        const std::vector<uint64_t>& instrumentTokens);
    
    /**
     * @brief Get market depth for an instrument
     * @param instrumentToken Instrument token
     * @return Future with instrument model containing market depth data
     */
    std::future<InstrumentModel> getMarketDepth(uint64_t instrumentToken);

    /**
     * @brief Refresh the instruments cache by force
     * @return True if successful, false otherwise
     */
    bool refreshInstrumentsCache();

    /**
     * @brief Clear the instruments cache
     */
    void clearInstrumentsCache();
    
    /**
     * @brief Get the spot price for an underlying
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param exchange Exchange name (default "NSE")
     * @return Future with spot price
     */
    std::future<double> getSpotPrice(const std::string& underlying, const std::string& exchange = "NSE");

private:
    /**
     * @brief Parse instrument data from CSV
     * @param csvData CSV data
     * @return Vector of instruments
     */
    std::vector<InstrumentModel> parseInstrumentsCSV(const std::string& csvData);
    
    /**
     * @brief Parse quote data from JSON
     * @param quoteJson Quote JSON
     * @return Instrument model with quote data
     */
    InstrumentModel parseQuoteJson(const std::string& instrumentTokenStr, 
                                 const nlohmann::json& quoteJson);
    
    /**
     * @brief Parse LTP data from JSON
     * @param ltpJson LTP JSON
     * @return Last traded price
     */
    double parseLTPJson(const std::string& instrumentTokenStr, const nlohmann::json& ltpJson);
    
    /**
     * @brief Parse OHLC data from JSON
     * @param ohlcJson OHLC JSON
     * @return OHLC data
     */
    std::tuple<double, double, double, double> parseOHLCJson(
        const std::string& instrumentTokenStr, const nlohmann::json& ohlcJson);
    
    /**
     * @brief Rate-limited API request with proper authentication
     * @param method HTTP method
     * @param endpoint API endpoint
     * @param params Optional query parameters
     * @param body Optional request body
     * @return HTTP response
     */
    HttpResponse makeRateLimitedApiRequest(
        HttpMethod method,
        const std::string& endpoint,
        const std::unordered_map<std::string, std::string>& params = {},
        const std::string& body = "");
    
    /**
     * @brief Check rate limits and wait if necessary
     * @param endpoint API endpoint
     * @return True if request can proceed, false if rate limit is exceeded
     */
    bool checkRateLimit(const std::string& endpoint);

    /**
     * @brief Save instruments data to cache file
     * @param csvData CSV data to save
     * @return True if successful, false otherwise
     */
    bool saveInstrumentsToCache(const std::string& csvData);

    /**
     * @brief Load instruments data from cache file
     * @return CSV data if successful, empty string otherwise
     */
    std::string loadInstrumentsFromCache();

    /**
     * @brief Check if instruments cache is valid
     * @return True if valid, false otherwise
     */
    bool isInstrumentsCacheValid();

    /**
     * @brief Get cached instruments file path
     * @return Full path to cached instruments file
     */
    std::string getInstrumentsCacheFilePath();
    
    /**
     * @brief Calculate strike range based on spot price and config
     * @param spotPrice Current spot price of the underlying
     * @return Pair of min and max strike prices
     */
    std::pair<double, double> calculateStrikeRange(double spotPrice);
    
    // Rate limiting parameters
    struct RateLimitInfo {
        int requestsPerMinute;
        std::queue<std::chrono::system_clock::time_point> requestTimes;
        std::shared_ptr<std::mutex> mtx;
        
        RateLimitInfo() : requestsPerMinute(0), mtx(std::make_shared<std::mutex>()) {}
        
        RateLimitInfo(int rpm) : 
            requestsPerMinute(rpm), 
            mtx(std::make_shared<std::mutex>()) {}
    };
    
    std::unordered_map<std::string, RateLimitInfo> m_rateLimits;
    bool m_instrumentsCached = false;
    std::chrono::system_clock::time_point m_lastInstrumentsFetch;
    std::chrono::minutes m_instrumentsCacheTTL = std::chrono::minutes(30);
    std::mutex m_rateLimitMutex;
    
    std::shared_ptr<AuthManager> m_authManager;  ///< Authentication manager
    std::shared_ptr<HttpClient> m_httpClient;    ///< HTTP client
    std::shared_ptr<Logger> m_logger;            ///< Logger instance
    std::shared_ptr<ConfigManager> m_configManager;  ///< Config manager
    
    std::unordered_map<uint64_t, InstrumentModel> m_instrumentCache;  ///< Cache of instruments by token
    std::unordered_map<std::string, uint64_t> m_symbolToTokenMap;     ///< Map of symbol to token
    
    mutable std::mutex m_cacheMutex;  ///< Mutex for cache access
};

}  // namespace BoxStrategy