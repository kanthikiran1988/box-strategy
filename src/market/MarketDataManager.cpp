/**
 * @file MarketDataManager.cpp
 * @brief Implementation of the MarketDataManager class
 */

#include "../market/MarketDataManager.hpp"
#include <sstream>
#include <algorithm>
#include <iterator>
#include "../external/json.hpp"
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace BoxStrategy {

MarketDataManager::MarketDataManager(
    std::shared_ptr<AuthManager> authManager,
    std::shared_ptr<HttpClient> httpClient,
    std::shared_ptr<Logger> logger,
    std::shared_ptr<ConfigManager> configManager
) : m_authManager(authManager),
    m_httpClient(httpClient),
    m_logger(logger),
    m_configManager(configManager) {
    
    m_logger->info("Initializing MarketDataManager");
    
    // Initialize rate limits for various endpoints
    // These are conservative defaults, adjust based on your API usage
    {
        std::lock_guard<std::mutex> lock(m_rateLimitMutex);
        m_rateLimits["/instruments"] = RateLimitInfo(1);    // 1 request per minute
        m_rateLimits["/quote"] = RateLimitInfo(15);          // 15 requests per minute
        m_rateLimits["/quote/ltp"] = RateLimitInfo(15);      // 15 requests per minute
        m_rateLimits["/quote/ohlc"] = RateLimitInfo(15);     // 15 requests per minute
        m_rateLimits["default"] = RateLimitInfo(10);        // 10 requests per minute for other endpoints
    }
    
    // Override from config if available
    int instrumentsRateLimit = m_configManager->getIntValue("api/rate_limits/instruments", 1);
    int quoteRateLimit = m_configManager->getIntValue("api/rate_limits/quote", 15);
    int ltpRateLimit = m_configManager->getIntValue("api/rate_limits/ltp", 15);
    int ohlcRateLimit = m_configManager->getIntValue("api/rate_limits/ohlc", 15);
    int defaultRateLimit = m_configManager->getIntValue("api/rate_limits/default", 10);
    
    // Update with config values
    {
        std::lock_guard<std::mutex> lock(m_rateLimitMutex);
        m_rateLimits["/instruments"].requestsPerMinute = instrumentsRateLimit;
        m_rateLimits["/quote"].requestsPerMinute = quoteRateLimit;
        m_rateLimits["/quote/ltp"].requestsPerMinute = ltpRateLimit;
        m_rateLimits["/quote/ohlc"].requestsPerMinute = ohlcRateLimit;
        m_rateLimits["default"].requestsPerMinute = defaultRateLimit;
    }
    
    // Set the instruments cache TTL
    int cacheTTLMinutes = m_configManager->getIntValue("api/instruments_cache_ttl_minutes", 1440);
    m_instrumentsCacheTTL = std::chrono::minutes(cacheTTLMinutes);
    
    m_logger->info("MarketDataManager initialized with cache TTL: {} minutes", cacheTTLMinutes);
}

MarketDataManager::~MarketDataManager() {
    m_logger->info("MarketDataManager destroyed");
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getAllInstruments() {
    return std::async(std::launch::async, [this]() {
        m_logger->info("Getting all instruments");
        
        std::vector<InstrumentModel> instruments;
        
        // First, check if we have a valid cache
        if (isInstrumentsCacheValid()) {
            m_logger->info("Using cached instruments data");
            std::string csvData = loadInstrumentsFromCache();
            
            if (!csvData.empty()) {
                instruments = parseInstrumentsCSV(csvData);
                
                if (!instruments.empty()) {
                    m_logger->info("Loaded {} instruments from cache", instruments.size());
                    
                    // Update memory cache
                    {
                        std::lock_guard<std::mutex> lock(m_cacheMutex);
                        
                        for (const auto& instrument : instruments) {
                            m_instrumentCache[instrument.instrumentToken] = instrument;
                            
                            std::string key = instrument.tradingSymbol + ":" + instrument.exchange;
                            m_symbolToTokenMap[key] = instrument.instrumentToken;
                        }
                        
                        m_instrumentsCached = true;
                    }
                    
                    return instruments;
                }
            }
            
            m_logger->warn("Failed to load instruments from cache");
        }
        
        // Cache not valid or failed to load, fetch from API
        m_logger->info("Fetching instruments from API");
        
        HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/instruments");
        
        if (response.statusCode == 200) {
            // Cache the response to file
            if (saveInstrumentsToCache(response.body)) {
                m_logger->info("Saved instruments data to cache");
            } else {
                m_logger->warn("Failed to save instruments data to cache");
            }
            
            instruments = parseInstrumentsCSV(response.body);
            
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                
                // Update cache
                for (const auto& instrument : instruments) {
                    m_instrumentCache[instrument.instrumentToken] = instrument;
                    
                    std::string key = instrument.tradingSymbol + ":" + instrument.exchange;
                    m_symbolToTokenMap[key] = instrument.instrumentToken;
                }
                
                m_instrumentsCached = true;
                m_lastInstrumentsFetch = std::chrono::system_clock::now();
            }
            
            m_logger->info("Fetched {} instruments", instruments.size());
            return instruments;
        } else {
            m_logger->error("Failed to fetch instruments. Status code: {}, Response: {}", 
                          response.statusCode, response.body);
            return std::vector<InstrumentModel>();
        }
    });
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getInstrumentsByExchange(
    const std::string& exchange) {
    
    return std::async(std::launch::async, [this, exchange]() {
        m_logger->info("Fetching instruments for exchange: {}", exchange);
        
        // First, ensure we have all instruments
        auto allInstrumentsFuture = getAllInstruments();
        std::vector<InstrumentModel> allInstruments = allInstrumentsFuture.get();
        
        // Filter by exchange
        std::vector<InstrumentModel> filteredInstruments;
        std::copy_if(allInstruments.begin(), allInstruments.end(), 
                    std::back_inserter(filteredInstruments),
                    [&exchange](const InstrumentModel& instrument) {
                        return instrument.exchange == exchange;
                    });
        
        m_logger->info("Fetched {} instruments for exchange {}", filteredInstruments.size(), exchange);
        
        // Debug: Count instrument types
        int optionCount = 0;
        int futureCount = 0;
        int equityCount = 0;
        int indexCount = 0;
        int otherCount = 0;
        
        // Count NIFTY instruments 
        int niftyInstrumentCount = 0;
        int niftyOptionCount = 0;
        
        for (const auto& instrument : filteredInstruments) {
            switch (instrument.type) {
                case InstrumentType::OPTION:   optionCount++; break;
                case InstrumentType::FUTURE:   futureCount++; break;
                case InstrumentType::EQUITY:   equityCount++; break;
                case InstrumentType::INDEX:    indexCount++; break;
                default:                      otherCount++; break;
            }
            
            // Check if trading symbol starts with "NIFTY"
            if (instrument.tradingSymbol.find("NIFTY") == 0) {
                niftyInstrumentCount++;
                
                // Additionally check for NIFTY options specifically
                if (instrument.type == InstrumentType::OPTION) {
                    niftyOptionCount++;
                    
                    // Debug NIFTY option information
                    if (niftyOptionCount <= 10) {
                        m_logger->debug("NIFTY Option Detail: symbol={}, underlying='{}', type={}, strike={}, optionType={}, expiry={}", 
                                      instrument.tradingSymbol, 
                                      instrument.underlying,
                                      InstrumentModel::instrumentTypeToString(instrument.type),
                                      instrument.strikePrice,
                                      InstrumentModel::optionTypeToString(instrument.optionType),
                                      InstrumentModel::formatDate(instrument.expiry));
                    }
                }
                
                if (niftyInstrumentCount <= 10) {
                    m_logger->debug("NIFTY Instrument: symbol={}, underlying='{}', type={}", 
                                  instrument.tradingSymbol, 
                                  instrument.underlying,
                                  InstrumentModel::instrumentTypeToString(instrument.type));
                }
            }
        }
        
        m_logger->info("Instrument type counts: OPTIONS={}, FUTURES={}, EQUITY={}, INDEX={}, OTHER={}",
                     optionCount, futureCount, equityCount, indexCount, otherCount);
                     
        m_logger->info("Found {} NIFTY instruments, including {} NIFTY options", niftyInstrumentCount, niftyOptionCount);
        
        return filteredInstruments;
    });
}

std::future<InstrumentModel> MarketDataManager::getInstrumentByToken(uint64_t instrumentToken) {
    return std::async(std::launch::async, [this, instrumentToken]() {
        m_logger->debug("Getting instrument by token: {}", instrumentToken);
        
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            
            // Check cache first
            auto it = m_instrumentCache.find(instrumentToken);
            if (it != m_instrumentCache.end()) {
                return it->second;
            }
        }
        
        // Not in cache, fetch all instruments and find the one we want
        auto allInstrumentsFuture = getAllInstruments();
        std::vector<InstrumentModel> allInstruments = allInstrumentsFuture.get();
        
        for (const auto& instrument : allInstruments) {
            if (instrument.instrumentToken == instrumentToken) {
                return instrument;
            }
        }
        
        // Not found
        m_logger->warn("Instrument with token {} not found", instrumentToken);
        return InstrumentModel();
    });
}

std::future<InstrumentModel> MarketDataManager::getInstrumentBySymbol(
    const std::string& tradingSymbol, const std::string& exchange) {
    
    return std::async(std::launch::async, [this, tradingSymbol, exchange]() {
        m_logger->debug("Getting instrument by symbol: {}:{}", tradingSymbol, exchange);
        
        std::string key = tradingSymbol + ":" + exchange;
        
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            
            // Check cache first
            auto it = m_symbolToTokenMap.find(key);
            if (it != m_symbolToTokenMap.end()) {
                uint64_t token = it->second;
                auto instrumentIt = m_instrumentCache.find(token);
                if (instrumentIt != m_instrumentCache.end()) {
                    return instrumentIt->second;
                }
            }
        }
        
        // Not in cache, fetch all instruments and find the one we want
        auto allInstrumentsFuture = getAllInstruments();
        std::vector<InstrumentModel> allInstruments = allInstrumentsFuture.get();
        
        for (const auto& instrument : allInstruments) {
            if (instrument.tradingSymbol == tradingSymbol && instrument.exchange == exchange) {
                return instrument;
            }
        }
        
        // Not found
        m_logger->warn("Instrument with symbol {}:{} not found", tradingSymbol, exchange);
        return InstrumentModel();
    });
}

std::future<InstrumentModel> MarketDataManager::getQuote(uint64_t instrumentToken) {
    return std::async(std::launch::async, [this, instrumentToken]() {
        m_logger->debug("Getting quote for instrument: {}", instrumentToken);
        
        // Construct query parameters
        std::unordered_map<std::string, std::string> params = {
            {"i", std::to_string(instrumentToken)}
        };
        
        HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote", params);
        
        if (response.statusCode == 200) {
            try {
                json responseJson = json::parse(response.body);
                
                if (responseJson["status"] == "success") {
                    json data = responseJson["data"];
                    
                    auto instrumentTokenStr = std::to_string(instrumentToken);
                    if (data.find(instrumentTokenStr) != data.end()) {
                        InstrumentModel instrument = parseQuoteJson(instrumentTokenStr, data[instrumentTokenStr]);
                        
                        // Update cache
                        {
                            std::lock_guard<std::mutex> lock(m_cacheMutex);
                            
                            auto it = m_instrumentCache.find(instrumentToken);
                            if (it != m_instrumentCache.end()) {
                                // Update existing instrument with new quote data
                                it->second.lastPrice = instrument.lastPrice;
                                it->second.openPrice = instrument.openPrice;
                                it->second.highPrice = instrument.highPrice;
                                it->second.lowPrice = instrument.lowPrice;
                                it->second.closePrice = instrument.closePrice;
                                it->second.averagePrice = instrument.averagePrice;
                                it->second.volume = instrument.volume;
                                it->second.buyQuantity = instrument.buyQuantity;
                                it->second.sellQuantity = instrument.sellQuantity;
                                it->second.openInterest = instrument.openInterest;
                                it->second.buyDepth = instrument.buyDepth;
                                it->second.sellDepth = instrument.sellDepth;
                                
                                instrument = it->second;
                            } else {
                                m_instrumentCache[instrumentToken] = instrument;
                                
                                // If we have the trading symbol, add to symbol to token map
                                if (!instrument.tradingSymbol.empty() && !instrument.exchange.empty()) {
                                    std::string key = instrument.tradingSymbol + ":" + instrument.exchange;
                                    m_symbolToTokenMap[key] = instrumentToken;
                                }
                            }
                        }
                        
                        m_logger->debug("Got quote for instrument: {}", instrumentToken);
                        return instrument;
                    } else {
                        m_logger->warn("Quote data for instrument {} not found in response", instrumentToken);
                    }
                } else {
                    m_logger->error("Failed to get quote: {}", responseJson["message"].get<std::string>());
                }
            } catch (const std::exception& e) {
                m_logger->error("Exception while parsing quote response: {}", e.what());
            }
        } else {
            m_logger->error("Failed to get quote. Status code: {}, Response: {}", 
                          response.statusCode, response.body);
        }
        
        return InstrumentModel();
    });
}

std::future<std::unordered_map<uint64_t, InstrumentModel>> MarketDataManager::getQuotes(
    const std::vector<uint64_t>& instrumentTokens) {
    
    return std::async(std::launch::async, [this, instrumentTokens]() {
        m_logger->debug("Getting quotes for {} instruments", instrumentTokens.size());
        
        std::unordered_map<uint64_t, InstrumentModel> result;
        
        // Zerodha API allows up to 250 instruments in one go
        const size_t maxBatchSize = 250;
        
        for (size_t i = 0; i < instrumentTokens.size(); i += maxBatchSize) {
            size_t batchSize = std::min(maxBatchSize, instrumentTokens.size() - i);
            std::vector<uint64_t> batch(instrumentTokens.begin() + i, 
                                       instrumentTokens.begin() + i + batchSize);
            
            // Construct query parameters
            std::unordered_map<std::string, std::string> params;
            for (size_t j = 0; j < batch.size(); ++j) {
                params["i"] = params["i"].empty() ? 
                    std::to_string(batch[j]) : 
                    params["i"] + "&i=" + std::to_string(batch[j]);
            }
            
            HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote", params);
            
            if (response.statusCode == 200) {
                try {
                    json responseJson = json::parse(response.body);
                    
                    if (responseJson["status"] == "success") {
                        json data = responseJson["data"];
                        
                        for (const auto& token : batch) {
                            auto tokenStr = std::to_string(token);
                            if (data.find(tokenStr) != data.end()) {
                                InstrumentModel instrument = parseQuoteJson(tokenStr, data[tokenStr]);
                                
                                // Update cache
                                {
                                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                                    
                                    auto it = m_instrumentCache.find(token);
                                    if (it != m_instrumentCache.end()) {
                                        // Update existing instrument with new quote data
                                        it->second.lastPrice = instrument.lastPrice;
                                        it->second.openPrice = instrument.openPrice;
                                        it->second.highPrice = instrument.highPrice;
                                        it->second.lowPrice = instrument.lowPrice;
                                        it->second.closePrice = instrument.closePrice;
                                        it->second.averagePrice = instrument.averagePrice;
                                        it->second.volume = instrument.volume;
                                        it->second.buyQuantity = instrument.buyQuantity;
                                        it->second.sellQuantity = instrument.sellQuantity;
                                        it->second.openInterest = instrument.openInterest;
                                        it->second.buyDepth = instrument.buyDepth;
                                        it->second.sellDepth = instrument.sellDepth;
                                        
                                        instrument = it->second;
                                    } else {
                                        m_instrumentCache[token] = instrument;
                                        
                                        // If we have the trading symbol, add to symbol to token map
                                        if (!instrument.tradingSymbol.empty() && !instrument.exchange.empty()) {
                                            std::string key = instrument.tradingSymbol + ":" + instrument.exchange;
                                            m_symbolToTokenMap[key] = token;
                                        }
                                    }
                                }
                                
                                result[token] = instrument;
                            }
                        }
                    } else {
                        m_logger->error("Failed to get quotes: {}", responseJson["message"].get<std::string>());
                    }
                } catch (const std::exception& e) {
                    m_logger->error("Exception while parsing quotes response: {}", e.what());
                }
            } else {
                m_logger->error("Failed to get quotes. Status code: {}, Response: {}", 
                              response.statusCode, response.body);
            }
        }
        
        m_logger->debug("Got quotes for {} instruments", result.size());
        return result;
    });
}

std::future<double> MarketDataManager::getLTP(uint64_t instrumentToken) {
    return std::async(std::launch::async, [this, instrumentToken]() {
        m_logger->debug("Getting LTP for instrument: {}", instrumentToken);
        
        // Construct query parameters
        std::unordered_map<std::string, std::string> params = {
            {"i", std::to_string(instrumentToken)}
        };
        
        HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote/ltp", params);
        
        if (response.statusCode == 200) {
            try {
                json responseJson = json::parse(response.body);
                
                if (responseJson["status"] == "success") {
                    json data = responseJson["data"];
                    
                    auto instrumentTokenStr = std::to_string(instrumentToken);
                    if (data.find(instrumentTokenStr) != data.end()) {
                        double ltp = parseLTPJson(instrumentTokenStr, data[instrumentTokenStr]);
                        
                        // Update cache
                        {
                            std::lock_guard<std::mutex> lock(m_cacheMutex);
                            
                            auto it = m_instrumentCache.find(instrumentToken);
                            if (it != m_instrumentCache.end()) {
                                it->second.lastPrice = ltp;
                            }
                        }
                        
                        m_logger->debug("Got LTP for instrument {}: {}", instrumentToken, ltp);
                        return ltp;
                    } else {
                        m_logger->warn("LTP data for instrument {} not found in response", instrumentToken);
                    }
                } else {
                    m_logger->error("Failed to get LTP: {}", responseJson["message"].get<std::string>());
                }
            } catch (const std::exception& e) {
                m_logger->error("Exception while parsing LTP response: {}", e.what());
            }
        } else {
            m_logger->error("Failed to get LTP. Status code: {}, Response: {}", 
                          response.statusCode, response.body);
        }
        
        return 0.0;
    });
}

std::future<std::unordered_map<uint64_t, double>> MarketDataManager::getLTPs(
    const std::vector<uint64_t>& instrumentTokens) {
    
    return std::async(std::launch::async, [this, instrumentTokens]() {
        m_logger->debug("Getting LTPs for {} instruments", instrumentTokens.size());
        
        std::unordered_map<uint64_t, double> result;
        
        // Zerodha API allows up to 250 instruments in one go
        const size_t maxBatchSize = 250;
        
        for (size_t i = 0; i < instrumentTokens.size(); i += maxBatchSize) {
            size_t batchSize = std::min(maxBatchSize, instrumentTokens.size() - i);
            std::vector<uint64_t> batch(instrumentTokens.begin() + i, 
                                       instrumentTokens.begin() + i + batchSize);
            
            // Construct query parameters
            std::unordered_map<std::string, std::string> params;
            for (size_t j = 0; j < batch.size(); ++j) {
                params["i"] = params["i"].empty() ? 
                    std::to_string(batch[j]) : 
                    params["i"] + "&i=" + std::to_string(batch[j]);
            }
            
            HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote/ltp", params);
            
            if (response.statusCode == 200) {
                try {
                    json responseJson = json::parse(response.body);
                    
                    if (responseJson["status"] == "success") {
                        json data = responseJson["data"];
                        
                        for (const auto& token : batch) {
                            auto tokenStr = std::to_string(token);
                            if (data.find(tokenStr) != data.end()) {
                                double ltp = parseLTPJson(tokenStr, data[tokenStr]);
                                
                                // Update cache
                                {
                                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                                    
                                    auto it = m_instrumentCache.find(token);
                                    if (it != m_instrumentCache.end()) {
                                        it->second.lastPrice = ltp;
                                    }
                                }
                                
                                result[token] = ltp;
                            }
                        }
                    } else {
                        m_logger->error("Failed to get LTPs: {}", responseJson["message"].get<std::string>());
                    }
                } catch (const std::exception& e) {
                    m_logger->error("Exception while parsing LTPs response: {}", e.what());
                }
            } else {
                m_logger->error("Failed to get LTPs. Status code: {}, Response: {}", 
                              response.statusCode, response.body);
            }
        }
        
        m_logger->debug("Got LTPs for {} instruments", result.size());
        return result;
    });
}

std::future<std::tuple<double, double, double, double>> MarketDataManager::getOHLC(
    uint64_t instrumentToken) {
    
    return std::async(std::launch::async, [this, instrumentToken]() {
        m_logger->debug("Getting OHLC for instrument: {}", instrumentToken);
        
        // Construct query parameters
        std::unordered_map<std::string, std::string> params = {
            {"i", std::to_string(instrumentToken)}
        };
        
        HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote/ohlc", params);
        
        if (response.statusCode == 200) {
            try {
                json responseJson = json::parse(response.body);
                
                if (responseJson["status"] == "success") {
                    json data = responseJson["data"];
                    
                    auto instrumentTokenStr = std::to_string(instrumentToken);
                    if (data.find(instrumentTokenStr) != data.end()) {
                        auto ohlc = parseOHLCJson(instrumentTokenStr, data[instrumentTokenStr]);
                        
                        // Update cache
                        {
                            std::lock_guard<std::mutex> lock(m_cacheMutex);
                            
                            auto it = m_instrumentCache.find(instrumentToken);
                            if (it != m_instrumentCache.end()) {
                                it->second.openPrice = std::get<0>(ohlc);
                                it->second.highPrice = std::get<1>(ohlc);
                                it->second.lowPrice = std::get<2>(ohlc);
                                it->second.closePrice = std::get<3>(ohlc);
                            }
                        }
                        
                        m_logger->debug("Got OHLC for instrument: {}", instrumentToken);
                        return ohlc;
                    } else {
                        m_logger->warn("OHLC data for instrument {} not found in response", instrumentToken);
                    }
                } else {
                    m_logger->error("Failed to get OHLC: {}", responseJson["message"].get<std::string>());
                }
            } catch (const std::exception& e) {
                m_logger->error("Exception while parsing OHLC response: {}", e.what());
            }
        } else {
            m_logger->error("Failed to get OHLC. Status code: {}, Response: {}", 
                          response.statusCode, response.body);
        }
        
        return std::make_tuple(0.0, 0.0, 0.0, 0.0);
    });
}

std::future<std::unordered_map<uint64_t, std::tuple<double, double, double, double>>> 
MarketDataManager::getOHLCs(const std::vector<uint64_t>& instrumentTokens) {
    
    return std::async(std::launch::async, [this, instrumentTokens]() {
        m_logger->debug("Getting OHLCs for {} instruments", instrumentTokens.size());
        
        std::unordered_map<uint64_t, std::tuple<double, double, double, double>> result;
        
        // Zerodha API allows up to 250 instruments in one go
        const size_t maxBatchSize = 250;
        
        for (size_t i = 0; i < instrumentTokens.size(); i += maxBatchSize) {
            size_t batchSize = std::min(maxBatchSize, instrumentTokens.size() - i);
            std::vector<uint64_t> batch(instrumentTokens.begin() + i, 
                                       instrumentTokens.begin() + i + batchSize);
            
            // Construct query parameters
            std::unordered_map<std::string, std::string> params;
            for (size_t j = 0; j < batch.size(); ++j) {
                params["i"] = params["i"].empty() ? 
                    std::to_string(batch[j]) : 
                    params["i"] + "&i=" + std::to_string(batch[j]);
            }
            
            HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/quote/ohlc", params);
            
            if (response.statusCode == 200) {
                try {
                    json responseJson = json::parse(response.body);
                    
                    if (responseJson["status"] == "success") {
                        json data = responseJson["data"];
                        
                        for (const auto& token : batch) {
                            auto tokenStr = std::to_string(token);
                            if (data.find(tokenStr) != data.end()) {
                                auto ohlc = parseOHLCJson(tokenStr, data[tokenStr]);
                                
                                // Update cache
                                {
                                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                                    
                                    auto it = m_instrumentCache.find(token);
                                    if (it != m_instrumentCache.end()) {
                                        it->second.openPrice = std::get<0>(ohlc);
                                        it->second.highPrice = std::get<1>(ohlc);
                                        it->second.lowPrice = std::get<2>(ohlc);
                                        it->second.closePrice = std::get<3>(ohlc);
                                    }
                                }
                                
                                result[token] = ohlc;
                            }
                        }
                    } else {
                        m_logger->error("Failed to get OHLCs: {}", responseJson["message"].get<std::string>());
                    }
                } catch (const std::exception& e) {
                    m_logger->error("Exception while parsing OHLCs response: {}", e.what());
                }
            } else {
                m_logger->error("Failed to get OHLCs. Status code: {}, Response: {}", 
                              response.statusCode, response.body);
            }
        }
        
        m_logger->debug("Got OHLCs for {} instruments", result.size());
        return result;
    });
}

std::future<InstrumentModel> MarketDataManager::getMarketDepth(uint64_t instrumentToken) {
    // Market depth is included in quote response, so we can just call getQuote
    return getQuote(instrumentToken);
}

std::vector<InstrumentModel> MarketDataManager::parseInstrumentsCSV(const std::string& csvData) {
    std::vector<InstrumentModel> instruments;
    
    std::istringstream stream(csvData);
    std::string line;
    
    // Skip header line
    std::getline(stream, line);
    
    // Debug info about header
    if (m_configManager->getBoolValue("debug/verbose", false)) {
        m_logger->debug("CSV Header: {}", line);
    }
    
    int lineCount = 0;
    int optionCount = 0;
    int futureCount = 0;
    int equityCount = 0;
    int niftyOptionCount = 0;
    
    while (std::getline(stream, line)) {
        lineCount++;
        std::istringstream lineStream(line);
        std::string field;
        std::vector<std::string> fields;
        
        while (std::getline(lineStream, field, ',')) {
            fields.push_back(field);
        }
        
        // Ensure we have enough fields
        if (fields.size() < 11) {
            continue;
        }
        
        InstrumentModel instrument;
        
        try {
            // Parse instrument token
            instrument.instrumentToken = std::stoull(fields[0]);
            
            // Parse exchange token
            instrument.exchangeToken = fields[1];
            
            // Parse trading symbol
            instrument.tradingSymbol = fields[2];
            
            // Parse name
            instrument.name = fields[3];
            
            // Parse last price
            if (!fields[4].empty()) {
                instrument.lastPrice = std::stod(fields[4]);
            }
            
            // Parse expiry
            if (fields.size() > 5 && !fields[5].empty()) {
                instrument.expiry = InstrumentModel::parseDate(fields[5]);
            }
            
            // Parse strike price
            if (fields.size() > 6 && !fields[6].empty()) {
                instrument.strikePrice = std::stod(fields[6]);
            }
            
            // Parse tick size
            // Not used in our model but available in fields[7]
            
            // Parse lot size
            // Not used in our model but available in fields[8]
            
            // Parse instrument type from Kite API
            if (fields.size() > 9) {
                instrument.type = InstrumentModel::stringToInstrumentType(fields[9]);
                
                // For options, special handling may be needed
                if (fields[9] == "CE") {
                    instrument.type = InstrumentType::OPTION;
                    instrument.optionType = OptionType::CALL;
                } else if (fields[9] == "PE") {
                    instrument.type = InstrumentType::OPTION;
                    instrument.optionType = OptionType::PUT;
                }
            }
            
            // Parse segment
            if (fields.size() > 10) {
                instrument.segment = fields[10];
                
                // Use segment to assist with instrument type detection
                if (instrument.segment.find("NFO-OPT") != std::string::npos) {
                    instrument.type = InstrumentType::OPTION;
                } else if (instrument.segment.find("NFO-FUT") != std::string::npos) {
                    instrument.type = InstrumentType::FUTURE;
                }
            }
            
            // Parse exchange
            if (fields.size() > 11) {
                instrument.exchange = fields[11];
            }
            
            // For NIFTY options/futures set the underlying field and extract info from trading symbol
            if (instrument.tradingSymbol.find("NIFTY") == 0) {
                instrument.underlying = "NIFTY";
                
                // Check for OPTION by examining trading symbol pattern (e.g., NIFTY23JUN2118000CE)
                if (instrument.tradingSymbol.find("CE") != std::string::npos) {
                    instrument.type = InstrumentType::OPTION;
                    instrument.optionType = OptionType::CALL;
                    niftyOptionCount++;
                } else if (instrument.tradingSymbol.find("PE") != std::string::npos) {
                    instrument.type = InstrumentType::OPTION;
                    instrument.optionType = OptionType::PUT;
                    niftyOptionCount++;
                }
                // Check for FUTURE by examining trading symbol pattern (e.g., NIFTYJUN23FUT)
                else if (instrument.tradingSymbol.find("FUT") != std::string::npos) {
                    instrument.type = InstrumentType::FUTURE;
                    futureCount++;
                }
            }
            
            // Count instrument types for logging
            if (instrument.type == InstrumentType::OPTION) {
                optionCount++;
            } else if (instrument.type == InstrumentType::FUTURE) {
                futureCount++;
            } else if (instrument.type == InstrumentType::EQUITY) {
                equityCount++;
            }
            
            instruments.push_back(instrument);
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing instrument CSV line {}: {}", lineCount, e.what());
        }
    }
    
    // Log statistics
    m_logger->info("Parsed {} instruments from CSV data", instruments.size());
    m_logger->info("Instrument counts: OPTIONS={}, FUTURES={}, EQUITY={}, NIFTY OPTIONS={}", 
                 optionCount, futureCount, equityCount, niftyOptionCount);
    
    // Print a few NIFTY options for debugging
    int debugCount = 0;
    for (const auto& instrument : instruments) {
        if (instrument.underlying == "NIFTY" && instrument.type == InstrumentType::OPTION && debugCount < 5) {
            auto time_t_val = std::chrono::system_clock::to_time_t(instrument.expiry);
            std::tm* tm = std::localtime(&time_t_val);
            
            std::stringstream ss;
            ss << std::put_time(tm, "%Y-%m-%d");
            
            m_logger->debug("NIFTY Option: symbol={}, expiry={}, strike={}, type={}", 
                          instrument.tradingSymbol, 
                          ss.str(),
                          instrument.strikePrice,
                          InstrumentModel::optionTypeToString(instrument.optionType));
            debugCount++;
        }
    }
    
    return instruments;
}

InstrumentModel MarketDataManager::parseQuoteJson(
    const std::string& instrumentTokenStr, const nlohmann::json& quoteJson) {
    
    InstrumentModel instrument;
    
    try {
        // Parse instrument token
        instrument.instrumentToken = std::stoull(instrumentTokenStr);
        
        // Parse last price
        if (quoteJson.contains("last_price")) {
            instrument.lastPrice = quoteJson["last_price"].get<double>();
        }
        
        // Parse OHLC
        if (quoteJson.contains("ohlc")) {
            auto& ohlc = quoteJson["ohlc"];
            
            if (ohlc.contains("open")) {
                instrument.openPrice = ohlc["open"].get<double>();
            }
            
            if (ohlc.contains("high")) {
                instrument.highPrice = ohlc["high"].get<double>();
            }
            
            if (ohlc.contains("low")) {
                instrument.lowPrice = ohlc["low"].get<double>();
            }
            
            if (ohlc.contains("close")) {
                instrument.closePrice = ohlc["close"].get<double>();
            }
        }
        
        // Parse average price
        if (quoteJson.contains("average_price")) {
            instrument.averagePrice = quoteJson["average_price"].get<double>();
        }
        
        // Parse volume
        if (quoteJson.contains("volume")) {
            instrument.volume = quoteJson["volume"].get<uint64_t>();
        }
        
        // Parse buy quantity
        if (quoteJson.contains("buy_quantity")) {
            instrument.buyQuantity = quoteJson["buy_quantity"].get<uint64_t>();
        }
        
        // Parse sell quantity
        if (quoteJson.contains("sell_quantity")) {
            instrument.sellQuantity = quoteJson["sell_quantity"].get<uint64_t>();
        }
        
        // Parse open interest
        if (quoteJson.contains("open_interest")) {
            instrument.openInterest = quoteJson["open_interest"].get<double>();
        }
        
        // Parse market depth
        if (quoteJson.contains("depth")) {
            auto& depth = quoteJson["depth"];
            
            // Parse buy depth
            if (depth.contains("buy")) {
                for (const auto& level : depth["buy"]) {
                    InstrumentModel::DepthItem item;
                    
                    if (level.contains("price")) {
                        item.price = level["price"].get<double>();
                    }
                    
                    if (level.contains("quantity")) {
                        item.quantity = level["quantity"].get<uint64_t>();
                    }
                    
                    if (level.contains("orders")) {
                        item.orders = level["orders"].get<uint32_t>();
                    }
                    
                    instrument.buyDepth.push_back(item);
                }
            }
            
            // Parse sell depth
            if (depth.contains("sell")) {
                for (const auto& level : depth["sell"]) {
                    InstrumentModel::DepthItem item;
                    
                    if (level.contains("price")) {
                        item.price = level["price"].get<double>();
                    }
                    
                    if (level.contains("quantity")) {
                        item.quantity = level["quantity"].get<uint64_t>();
                    }
                    
                    if (level.contains("orders")) {
                        item.orders = level["orders"].get<uint32_t>();
                    }
                    
                    instrument.sellDepth.push_back(item);
                }
            }
        }
    } catch (const std::exception& e) {
        m_logger->error("Exception while parsing quote JSON: {}", e.what());
    }
    
    return instrument;
}

double MarketDataManager::parseLTPJson(
    const std::string& instrumentTokenStr, const nlohmann::json& ltpJson) {
    
    try {
        if (ltpJson.contains("last_price")) {
            return ltpJson["last_price"].get<double>();
        }
    } catch (const std::exception& e) {
        m_logger->error("Exception while parsing LTP JSON: {}", e.what());
    }
    
    return 0.0;
}

std::tuple<double, double, double, double> MarketDataManager::parseOHLCJson(
    const std::string& instrumentTokenStr, const nlohmann::json& ohlcJson) {
    
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    
    try {
        if (ohlcJson.contains("ohlc")) {
            auto& ohlc = ohlcJson["ohlc"];
            
            if (ohlc.contains("open")) {
                open = ohlc["open"].get<double>();
            }
            
            if (ohlc.contains("high")) {
                high = ohlc["high"].get<double>();
            }
            
            if (ohlc.contains("low")) {
                low = ohlc["low"].get<double>();
            }
            
            if (ohlc.contains("close")) {
                close = ohlc["close"].get<double>();
            }
        }
    } catch (const std::exception& e) {
        m_logger->error("Exception while parsing OHLC JSON: {}", e.what());
    }
    
    return std::make_tuple(open, high, low, close);
}

bool MarketDataManager::checkRateLimit(const std::string& endpoint) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Find rate limit info for this endpoint or use default
    std::string rateKey = endpoint;
    std::lock_guard<std::mutex> globalLock(m_rateLimitMutex);
    
    if (m_rateLimits.find(endpoint) == m_rateLimits.end()) {
        rateKey = "default";
    }
    
    RateLimitInfo& rateInfo = m_rateLimits[rateKey];
    std::lock_guard<std::mutex> lock(*rateInfo.mtx);
    
    // Remove requests older than 1 minute
    auto oneMinuteAgo = now - std::chrono::minutes(1);
    while (!rateInfo.requestTimes.empty() && rateInfo.requestTimes.front() < oneMinuteAgo) {
        rateInfo.requestTimes.pop();
    }
    
    // Check if we can make a new request
    if (rateInfo.requestTimes.size() < static_cast<size_t>(rateInfo.requestsPerMinute)) {
        // Add this request to the queue
        rateInfo.requestTimes.push(now);
        return true;
    }
    
    // Calculate wait time if rate limit is exceeded
    auto oldestRequest = rateInfo.requestTimes.front();
    auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        (oldestRequest + std::chrono::minutes(1)) - now);
    
    m_logger->warn("Rate limit exceeded for {}. Waiting {} ms before retrying", 
                 endpoint, waitTime.count());
    
    // Release locks before sleeping
    lock.~lock_guard();
    globalLock.~lock_guard();
    
    // Sleep for the required wait time
    std::this_thread::sleep_for(waitTime);
    
    // Try again
    return checkRateLimit(endpoint);
}

HttpResponse MarketDataManager::makeRateLimitedApiRequest(
    HttpMethod method,
    const std::string& endpoint,
    const std::unordered_map<std::string, std::string>& params,
    const std::string& body) {
    
    // Check if token is valid
    if (!m_authManager->isAccessTokenValid()) {
        m_logger->error("Access token is not valid for API request");
        return HttpResponse{401, "Access token is not valid", {}};
    }
    
    // Check and enforce rate limits
    if (!checkRateLimit(endpoint)) {
        m_logger->error("Rate limit exceeded for {}", endpoint);
        return HttpResponse{429, "Rate limit exceeded", {}};
    }
    
    // Special handling for instrument list - use cached version if available
    if (endpoint == "/instruments") {
        auto now = std::chrono::system_clock::now();
        if (m_instrumentsCached && (now - m_lastInstrumentsFetch) < m_instrumentsCacheTTL) {
            m_logger->info("Using cached instruments (cache valid for {} more minutes)", 
                         std::chrono::duration_cast<std::chrono::minutes>(
                             m_lastInstrumentsFetch + m_instrumentsCacheTTL - now).count());
            
            // Return cached instruments
            // But update the last fetch time to avoid multiple threads requesting at once
            m_lastInstrumentsFetch = now;
            
            // This would return the cached data, but since we don't have it yet,
            // we'll proceed with the API call for now
        }
    }
    
    std::string url = "https://api.kite.trade" + endpoint;
    
    // Add query parameters to URL
    if (!params.empty()) {
        url += "?";
        bool first = true;
        
        for (const auto& param : params) {
            if (!first) {
                url += "&";
            }
            
            url += param.first + "=" + param.second;
            first = false;
        }
    }
    
    // Set headers
    std::unordered_map<std::string, std::string> headers = {
        {"X-Kite-Version", "3"},
        {"Authorization", "token " + m_authManager->getApiKey() + ":" + m_authManager->getAccessToken()}
    };
    
    // Make the request
    HttpResponse response = m_httpClient->request(method, url, headers, body);
    
    // Update instrument cache metadata if instruments were fetched
    if (endpoint == "/instruments" && response.statusCode == 200) {
        m_instrumentsCached = true;
        m_lastInstrumentsFetch = std::chrono::system_clock::now();
        m_logger->info("Updated instruments cache");
    }
    
    // Check for authentication error
    if (response.statusCode == 403 || response.statusCode == 401) {
        m_logger->warn("Authentication error in API request. Status code: {}", response.statusCode);
        
        // Clear the access token so that it's renewed on the next request
        m_authManager->invalidateAccessToken();
    }
    
    // Handle rate limit errors
    if (response.statusCode == 429) {
        m_logger->warn("Rate limit error from API. Consider adjusting rate limits in config.");
        
        // Increase the backoff for this endpoint
        std::lock_guard<std::mutex> lock(m_rateLimitMutex);
        std::string rateKey = endpoint;
        
        if (m_rateLimits.find(endpoint) == m_rateLimits.end()) {
            rateKey = "default";
        }
        
        // Reduce rate limit by 20%
        m_rateLimits[rateKey].requestsPerMinute = 
            std::max(1, static_cast<int>(m_rateLimits[rateKey].requestsPerMinute * 0.8));
        
        m_logger->info("Adjusted rate limit for {} to {} requests per minute", 
                     endpoint, m_rateLimits[rateKey].requestsPerMinute);
    }
    
    return response;
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getOptionChain(
    const std::string& underlying,
    const std::chrono::system_clock::time_point& expiry,
    const std::string& exchange,
    double minStrike,
    double maxStrike) {
    
    return std::async(std::launch::async, [this, underlying, expiry, exchange, minStrike, maxStrike]() {
        m_logger->info("Getting option chain for {}, expiry: {}", 
                     underlying, InstrumentModel::formatDate(expiry));
        
        // Get all instruments from the specified exchange
        auto instrumentsFuture = getInstrumentsByExchange(exchange);
        std::vector<InstrumentModel> allInstruments = instrumentsFuture.get();
        
        // Filter for options with matching underlying and expiry
        std::vector<InstrumentModel> optionChain;
        int callCount = 0;
        int putCount = 0;
        
        for (const auto& instrument : allInstruments) {
            if (instrument.type == InstrumentType::OPTION) {
                // Check if it's for our underlying
                bool isTargetOption = false;
                
                if (instrument.underlying == underlying) {
                    isTargetOption = true;
                }
                else if (instrument.tradingSymbol.find(underlying) == 0) {
                    isTargetOption = true;
                }
                
                // Check if it's for our expiry date (within 1 day tolerance)
                if (isTargetOption) {
                    auto expiryDiff = std::abs(std::chrono::duration_cast<std::chrono::hours>(
                        instrument.expiry - expiry).count());
                    
                    if (expiryDiff <= 24) { // Within 1 day
                        // Apply strike price filter if specified
                        if ((minStrike <= 0.0 || instrument.strikePrice >= minStrike) &&
                            (maxStrike <= 0.0 || instrument.strikePrice <= maxStrike)) {
                            
                            optionChain.push_back(instrument);
                            
                            if (instrument.optionType == OptionType::CALL) {
                                callCount++;
                            } else if (instrument.optionType == OptionType::PUT) {
                                putCount++;
                            }
                        }
                    }
                }
            }
        }
        
        // Sort by strike price
        std::sort(optionChain.begin(), optionChain.end(), 
                 [](const InstrumentModel& a, const InstrumentModel& b) {
                     return a.strikePrice < b.strikePrice;
                 });
        
        m_logger->info("Found {} options ({} calls, {} puts) for {} with expiry {}",
                     optionChain.size(), callCount, putCount, underlying, 
                     InstrumentModel::formatDate(expiry));
        
        return optionChain;
    });
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getOptionChainWithQuotes(
    const std::string& underlying,
    const std::chrono::system_clock::time_point& expiry,
    const std::string& exchange,
    double minStrike,
    double maxStrike) {
    
    return std::async(std::launch::async, [this, underlying, expiry, exchange, minStrike, maxStrike]() {
        m_logger->info("Getting option chain with quotes for {}, expiry: {}", 
                     underlying, InstrumentModel::formatDate(expiry));
        
        // First get the option chain
        auto optionChainFuture = getOptionChain(underlying, expiry, exchange, minStrike, maxStrike);
        std::vector<InstrumentModel> optionChain = optionChainFuture.get();
        
        if (optionChain.empty()) {
            m_logger->warn("No options found for {} with expiry {}", 
                         underlying, InstrumentModel::formatDate(expiry));
            return optionChain;
        }
        
        // Extract instrument tokens for quote requests
        std::vector<uint64_t> instrumentTokens;
        for (const auto& option : optionChain) {
            instrumentTokens.push_back(option.instrumentToken);
        }
        
        // Batch the quote requests to respect API limits
        const size_t batchSize = 250; // Kite API limit per request
        std::vector<InstrumentModel> resultChain;
        
        for (size_t i = 0; i < instrumentTokens.size(); i += batchSize) {
            size_t currentBatchSize = std::min(batchSize, instrumentTokens.size() - i);
            std::vector<uint64_t> batch(instrumentTokens.begin() + i, 
                                      instrumentTokens.begin() + i + currentBatchSize);
            
            // Get quotes for this batch
            auto quotesFuture = getQuotes(batch);
            auto quotes = quotesFuture.get();
            
            // Add quotes to our result chain
            for (size_t j = 0; j < currentBatchSize; j++) {
                uint64_t token = batch[j];
                if (quotes.find(token) != quotes.end()) {
                    resultChain.push_back(quotes[token]);
                }
            }
            
            // Add a small delay to avoid rate limiting
            if (i + batchSize < instrumentTokens.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        
        m_logger->info("Got quotes for {}/{} options in the chain", 
                     resultChain.size(), optionChain.size());
        
        // Re-sort by strike price
        std::sort(resultChain.begin(), resultChain.end(), 
                 [](const InstrumentModel& a, const InstrumentModel& b) {
                     return a.strikePrice < b.strikePrice;
                 });
        
        return resultChain;
    });
}

bool MarketDataManager::refreshInstrumentsCache() {
    m_logger->info("Forcing refresh of instruments cache");
    
    HttpResponse response = makeRateLimitedApiRequest(HttpMethod::GET, "/instruments");
    
    if (response.statusCode == 200) {
        // Cache the response to file
        if (saveInstrumentsToCache(response.body)) {
            m_logger->info("Saved instruments data to cache");
            
            // Update in-memory cache
            auto instruments = parseInstrumentsCSV(response.body);
            
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                
                // Clear existing cache
                m_instrumentCache.clear();
                m_symbolToTokenMap.clear();
                
                // Update cache
                for (const auto& instrument : instruments) {
                    m_instrumentCache[instrument.instrumentToken] = instrument;
                    
                    std::string key = instrument.tradingSymbol + ":" + instrument.exchange;
                    m_symbolToTokenMap[key] = instrument.instrumentToken;
                }
                
                m_instrumentsCached = true;
                m_lastInstrumentsFetch = std::chrono::system_clock::now();
            }
            
            return true;
        } else {
            m_logger->warn("Failed to save instruments data to cache");
        }
    }
    
    return false;
}

void MarketDataManager::clearInstrumentsCache() {
    m_logger->info("Clearing instruments cache");
    
    // Remove file if it exists
    std::string cacheFilePath = getInstrumentsCacheFilePath();
    if (std::filesystem::exists(cacheFilePath)) {
        try {
            std::filesystem::remove(cacheFilePath);
            m_logger->info("Removed instruments cache file: {}", cacheFilePath);
        } catch (const std::exception& e) {
            m_logger->error("Failed to remove instruments cache file: {}", e.what());
        }
    }
    
    // Clear memory cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_instrumentCache.clear();
        m_symbolToTokenMap.clear();
        m_instrumentsCached = false;
    }
}

bool MarketDataManager::saveInstrumentsToCache(const std::string& csvData) {
    try {
        std::string cacheFilePath = getInstrumentsCacheFilePath();
        std::ofstream cacheFile(cacheFilePath, std::ios::out | std::ios::binary);
        
        if (!cacheFile.is_open()) {
            m_logger->error("Failed to open cache file for writing: {}", cacheFilePath);
            return false;
        }
        
        cacheFile.write(csvData.c_str(), csvData.size());
        cacheFile.close();
        
        // Update timestamp by touching the file
        m_lastInstrumentsFetch = std::chrono::system_clock::now();
        
        return true;
    } catch (const std::exception& e) {
        m_logger->error("Exception while saving instruments to cache: {}", e.what());
        return false;
    }
}

std::string MarketDataManager::loadInstrumentsFromCache() {
    try {
        std::string cacheFilePath = getInstrumentsCacheFilePath();
        
        if (!std::filesystem::exists(cacheFilePath)) {
            m_logger->warn("Cache file does not exist: {}", cacheFilePath);
            return "";
        }
        
        std::ifstream cacheFile(cacheFilePath, std::ios::in | std::ios::binary);
        
        if (!cacheFile.is_open()) {
            m_logger->error("Failed to open cache file for reading: {}", cacheFilePath);
            return "";
        }
        
        // Get file size
        cacheFile.seekg(0, std::ios::end);
        size_t fileSize = cacheFile.tellg();
        cacheFile.seekg(0, std::ios::beg);
        
        // Read file content
        std::string csvData(fileSize, '\0');
        cacheFile.read(&csvData[0], fileSize);
        cacheFile.close();
        
        return csvData;
    } catch (const std::exception& e) {
        m_logger->error("Exception while loading instruments from cache: {}", e.what());
        return "";
    }
}

bool MarketDataManager::isInstrumentsCacheValid() {
    try {
        std::string cacheFilePath = getInstrumentsCacheFilePath();
        
        if (!std::filesystem::exists(cacheFilePath)) {
            return false;
        }
        
        // Get file creation/modification timestamp
        auto cacheLastModified = std::filesystem::last_write_time(cacheFilePath);
        auto now = std::filesystem::file_time_type::clock::now();
        
        // Compute time difference (C++20 has std::chrono::clock_cast but we'll use a simpler approach)
        auto age = now - cacheLastModified;
        
        // Convert to a duration we can work with more easily
        auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(age).count();
        auto age_minutes = age_seconds / 60;
        
        // Get cache TTL from config
        int cacheTTLMinutes = m_configManager->getIntValue("api/instruments_cache_ttl_minutes", 1440);
        
        // Cache is valid if it's less than TTL minutes old
        bool isValid = age_minutes < cacheTTLMinutes;
        
        if (isValid) {
            m_logger->debug("Instruments cache is valid (age: {} minutes, TTL: {} minutes)", 
                          age_minutes, cacheTTLMinutes);
        } else {
            m_logger->debug("Instruments cache is expired (age: {} minutes, TTL: {} minutes)", 
                          age_minutes, cacheTTLMinutes);
        }
        
        return isValid;
    } catch (const std::exception& e) {
        m_logger->error("Exception while checking cache validity: {}", e.what());
        return false;
    }
}

std::string MarketDataManager::getInstrumentsCacheFilePath() {
    // First check config for cache file path
    std::string cacheFileName = m_configManager->getStringValue(
        "api/instruments_cache_file", "instruments_cache.csv");
    
    // If it's a relative path, prepend the current directory
    if (!cacheFileName.empty() && cacheFileName[0] != '/') {
        // Get current directory
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path cachePath = currentPath / cacheFileName;
        return cachePath.string();
    }
    
    return cacheFileName;
}

// New methods implementation
std::future<double> MarketDataManager::getSpotPrice(const std::string& underlying, const std::string& exchange) {
    return std::async(std::launch::async, [this, underlying, exchange]() {
        m_logger->debug("Getting spot price for {}:{}", underlying, exchange);
        
        try {
            // Get the spot instrument
            auto spotInstrumentFuture = getInstrumentBySymbol(underlying, exchange);
            auto spotInstrument = spotInstrumentFuture.get();
            
            if (spotInstrument.instrumentToken == 0) {
                m_logger->error("Failed to find spot instrument for {}:{}", underlying, exchange);
                return 0.0;
            }
            
            // Get LTP
            auto ltpFuture = getLTP(spotInstrument.instrumentToken);
            double spotPrice = ltpFuture.get();
            
            m_logger->debug("Spot price for {}:{} is {}", underlying, exchange, spotPrice);
            return spotPrice;
        } catch (const std::exception& e) {
            m_logger->error("Error getting spot price for {}:{}: {}", underlying, exchange, e.what());
            return 0.0;
        }
    });
}

std::pair<double, double> MarketDataManager::calculateStrikeRange(double spotPrice) {
    if (spotPrice <= 0.0) {
        return {0.0, 0.0}; // No filtering
    }
    
    // Get strike range percentage from config
    double rangePercent = m_configManager->getDoubleValue("option_chain/strike_range_percent", 5.0);
    
    // Calculate min and max strike
    double minStrike = spotPrice * (1.0 - rangePercent / 100.0);
    double maxStrike = spotPrice * (1.0 + rangePercent / 100.0);
    
    m_logger->debug("Calculated strike range: {} - {} (spot: {}, range: {}%)", 
                  minStrike, maxStrike, spotPrice, rangePercent);
                  
    return {minStrike, maxStrike};
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getFilteredOptionChain(
    const std::string& underlying,
    const std::chrono::system_clock::time_point& expiry,
    const std::string& exchange) {
    
    return std::async(std::launch::async, [this, underlying, expiry, exchange]() {
        m_logger->info("Getting filtered option chain for {}:{} with expiry {}", 
                     underlying, exchange, InstrumentModel::formatDate(expiry));
        
        // Get spot price to calculate strike range
        double spotPrice = 0.0;
        try {
            auto spotPriceFuture = getSpotPrice(underlying, "NSE");
            spotPrice = spotPriceFuture.get();
        } catch (const std::exception& e) {
            m_logger->warn("Failed to get spot price for filtering option chain: {}", e.what());
        }
        
        // Calculate strike range
        auto [minStrike, maxStrike] = calculateStrikeRange(spotPrice);
        
        // Get option chain with strike range
        auto optionChainFuture = getOptionChain(underlying, expiry, exchange, minStrike, maxStrike);
        auto optionChain = optionChainFuture.get();
        
        m_logger->info("Filtered option chain contains {} options for {}:{} with expiry {}", 
                     optionChain.size(), underlying, exchange, 
                     InstrumentModel::formatDate(expiry));
        
        return optionChain;
    });
}

std::future<std::vector<InstrumentModel>> MarketDataManager::getFilteredOptionChainWithQuotes(
    const std::string& underlying,
    const std::chrono::system_clock::time_point& expiry,
    const std::string& exchange) {
    
    return std::async(std::launch::async, [this, underlying, expiry, exchange]() {
        m_logger->info("Getting filtered option chain with quotes for {}:{} with expiry {}", 
                     underlying, exchange, InstrumentModel::formatDate(expiry));
        
        // Get filtered option chain
        auto filteredChainFuture = getFilteredOptionChain(underlying, expiry, exchange);
        auto filteredChain = filteredChainFuture.get();
        
        if (filteredChain.empty()) {
            m_logger->warn("No options found in filtered chain");
            return std::vector<InstrumentModel>();
        }
        
        // Extract instrument tokens
        std::vector<uint64_t> instrumentTokens;
        for (const auto& option : filteredChain) {
            instrumentTokens.push_back(option.instrumentToken);
        }
        
        // Get batch size from config
        int batchSize = m_configManager->getIntValue("option_chain/pipeline/batch_size", 100);
        
        // Get quotes in batches
        std::vector<InstrumentModel> resultChain;
        for (size_t i = 0; i < instrumentTokens.size(); i += batchSize) {
            size_t batchEnd = std::min(i + batchSize, instrumentTokens.size());
            std::vector<uint64_t> batchTokens(instrumentTokens.begin() + i, 
                                            instrumentTokens.begin() + batchEnd);
            
            m_logger->debug("Getting quotes for batch {}/{} (size: {})", 
                          (i / batchSize) + 1, 
                          (instrumentTokens.size() + batchSize - 1) / batchSize,
                          batchTokens.size());
            
            auto quotesFuture = getQuotes(batchTokens);
            auto quotes = quotesFuture.get();
            
            // Add to result chain
            for (size_t j = i; j < batchEnd; ++j) {
                uint64_t token = instrumentTokens[j];
                if (quotes.find(token) != quotes.end()) {
                    resultChain.push_back(quotes[token]);
                }
            }
            
            // Add a small delay to avoid rate limiting
            if (i + batchSize < instrumentTokens.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        
        m_logger->info("Got quotes for {}/{} options in the filtered chain", 
                     resultChain.size(), filteredChain.size());
        
        // Re-sort by strike price
        std::sort(resultChain.begin(), resultChain.end(), 
                 [](const InstrumentModel& a, const InstrumentModel& b) {
                     return a.strikePrice < b.strikePrice;
                 });
        
        return resultChain;
    });
}

}  // namespace BoxStrategy