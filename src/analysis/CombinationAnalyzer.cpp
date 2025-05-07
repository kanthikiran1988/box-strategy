/**
 * @file CombinationAnalyzer.cpp
 * @brief Implementation of the CombinationAnalyzer class
 */

#include "../analysis/CombinationAnalyzer.hpp"
#include <algorithm>
#include <set>
#include <map>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <unordered_map>

namespace BoxStrategy {

CombinationAnalyzer::CombinationAnalyzer(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<MarketDataManager> marketDataManager,
    std::shared_ptr<ExpiryManager> expiryManager,
    std::shared_ptr<FeeCalculator> feeCalculator,
    std::shared_ptr<RiskCalculator> riskCalculator,
    std::shared_ptr<ThreadPool> threadPool,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_marketDataManager(marketDataManager),
    m_expiryManager(expiryManager),
    m_feeCalculator(feeCalculator),
    m_riskCalculator(riskCalculator),
    m_threadPool(threadPool),
    m_logger(logger) {
    
    m_logger->info("Initializing CombinationAnalyzer");
}

std::vector<BoxSpreadModel> CombinationAnalyzer::findProfitableSpreads(
    const std::string& underlying, 
    const std::string& exchange) {
    
    m_logger->info("Finding profitable spreads for {}:{}", underlying, exchange);
    
    std::vector<BoxSpreadModel> result;
    
    // Get available expiries
    auto expiries = m_expiryManager->getNextExpiries(underlying, exchange, 
                                                   m_configManager->getIntValue("expiry/max_count", 3));
    
    m_logger->info("Found {} expiries to analyze", expiries.size());
    
    // Check if we should process expiries in parallel or sequentially
    bool processInParallel = m_configManager->getBoolValue("expiry/process_in_parallel", false);
    
    if (processInParallel) {
        // Process expiries in parallel
        std::vector<std::future<std::vector<BoxSpreadModel>>> futures;
        for (const auto& expiry : expiries) {
            futures.push_back(m_threadPool->enqueue(
                [this, underlying, exchange, expiry]() {
                    return this->findProfitableSpreadsForExpiry(underlying, exchange, expiry);
                }
            ));
        }
        
        // Collect results
        for (auto& future : futures) {
            auto spreads = future.get();
            m_logger->info("Found {} profitable spreads for an expiry", spreads.size());
            result.insert(result.end(), spreads.begin(), spreads.end());
        }
    } else {
        // Process expiries sequentially
        for (const auto& expiry : expiries) {
            m_logger->info("Processing expiry {}", InstrumentModel::formatDate(expiry));
            auto spreads = findProfitableSpreadsForExpiry(underlying, exchange, expiry);
            m_logger->info("Found {} profitable spreads for expiry {}", 
                         spreads.size(), InstrumentModel::formatDate(expiry));
            result.insert(result.end(), spreads.begin(), spreads.end());
            
            // Add a delay between expiries to avoid rate limiting
            int delayMs = m_configManager->getIntValue("option_chain/pipeline/delay_between_expiries_ms", 1000);
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
    }
    
    // Sort by profitability
    result = sortByProfitability(result);
    
    m_logger->info("Found a total of {} profitable spreads", result.size());
    return result;
}

std::vector<BoxSpreadModel> CombinationAnalyzer::findProfitableSpreadsForExpiry(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry) {
    
    m_logger->info("Finding profitable spreads for {}:{} with expiry {}", 
                 underlying, exchange, InstrumentModel::formatDate(expiry));
    
    // Find available strikes using filtered option chain
    std::vector<double> strikes;
    
    try {
        // Use the new filtered option chain method
        auto filteredChainFuture = m_marketDataManager->getFilteredOptionChain(underlying, expiry, exchange);
        auto filteredChain = filteredChainFuture.get();
        
        // Extract unique strikes
        std::set<double> uniqueStrikes;
        for (const auto& option : filteredChain) {
            uniqueStrikes.insert(option.strikePrice);
        }
        
        // Convert to vector and sort
        strikes.assign(uniqueStrikes.begin(), uniqueStrikes.end());
        std::sort(strikes.begin(), strikes.end());
        
        m_logger->info("Found {} strikes after filtering for {}:{} with expiry {}", 
                     strikes.size(), underlying, exchange, InstrumentModel::formatDate(expiry));
    } catch (const std::exception& e) {
        m_logger->error("Error getting filtered option chain: {}", e.what());
        
        // Fall back to the original method
        strikes = findAvailableStrikes(underlying, exchange, expiry);
        m_logger->info("Fallback: Found {} strikes for {}:{} with expiry {}", 
                     strikes.size(), underlying, exchange, InstrumentModel::formatDate(expiry));
    }
    
    if (strikes.size() < 2) {
        m_logger->warn("Not enough strikes to form a box spread");
        return {};
    }
    
    // Generate all possible strike combinations
    auto combinations = generateStrikeCombinations(underlying, exchange, expiry, strikes);
    m_logger->info("Generated {} strike combinations", combinations.size());
    
    // Configure batch processing 
    size_t batchSize = m_configManager->getIntValue("option_chain/pipeline/batch_size", 3);
    int delayBetweenBatchesMs = m_configManager->getIntValue("option_chain/pipeline/delay_between_batches_ms", 4000);
    
    // Process in smaller batches to avoid rate limits
    std::vector<BoxSpreadModel> allSpreads;
    
    // Step 1: Pre-load all required options for all combinations
    // This has two benefits:
    // 1. We cache the option data to reduce redundant getAllInstruments calls
    // 2. We can make fewer, larger batched quote requests instead of many small ones
    
    m_logger->info("Pre-loading options for all combinations");
    
    std::unordered_map<double, std::pair<InstrumentModel, InstrumentModel>> optionsByStrike;
    std::vector<uint64_t> allRequiredOptionTokens;
    
    // Get all instruments once
    auto instrumentsFuture = m_marketDataManager->getAllInstruments();
    auto allInstruments = instrumentsFuture.get();
    
    // First pass: Find all required options by strike
    for (const auto& strike : strikes) {
        // Find call option for this strike
        InstrumentModel callOption;
        InstrumentModel putOption;
        
        for (const auto& instrument : allInstruments) {
            if (instrument.type == InstrumentType::OPTION && 
                instrument.underlying == underlying &&
                instrument.exchange == exchange &&
                instrument.expiry == expiry &&
                std::abs(instrument.strikePrice - strike) < 0.01) {
                
                if (instrument.optionType == OptionType::CALL && callOption.instrumentToken == 0) {
                    callOption = instrument;
                } else if (instrument.optionType == OptionType::PUT && putOption.instrumentToken == 0) {
                    putOption = instrument;
                }
                
                // If we found both, break early
                if (callOption.instrumentToken != 0 && putOption.instrumentToken != 0) {
                    break;
                }
            }
        }
        
        // Store the options for this strike
        if (callOption.instrumentToken != 0 && putOption.instrumentToken != 0) {
            optionsByStrike[strike] = std::make_pair(callOption, putOption);
            allRequiredOptionTokens.push_back(callOption.instrumentToken);
            allRequiredOptionTokens.push_back(putOption.instrumentToken);
        }
    }
    
    m_logger->info("Found options for {} strikes, requiring {} quotes", 
                 optionsByStrike.size(), allRequiredOptionTokens.size());
    
    // Step 2: Fetch all required quotes in batches of up to 500 instruments per API call
    std::unordered_map<uint64_t, InstrumentModel> quotesCache;
    const size_t maxQuoteBatchSize = 500; // Zerodha API limit
    
    for (size_t i = 0; i < allRequiredOptionTokens.size(); i += maxQuoteBatchSize) {
        size_t batchEnd = std::min(i + maxQuoteBatchSize, allRequiredOptionTokens.size());
        std::vector<uint64_t> batchTokens(allRequiredOptionTokens.begin() + i, 
                                        allRequiredOptionTokens.begin() + batchEnd);
        
        m_logger->info("Fetching quotes for batch of {} options", batchTokens.size());
        
        auto quotesFuture = m_marketDataManager->getQuotes(batchTokens);
        auto quotes = quotesFuture.get();
        
        // Add to cache
        for (const auto& [token, quote] : quotes) {
            quotesCache[token] = quote;
        }
        
        // Add delay if there are more batches to process
        if (batchEnd < allRequiredOptionTokens.size()) {
            m_logger->debug("Waiting {} ms before fetching next batch of quotes", delayBetweenBatchesMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(delayBetweenBatchesMs));
        }
    }
    
    m_logger->info("Successfully fetched quotes for {}/{} options", 
                 quotesCache.size(), allRequiredOptionTokens.size());
    
    // Step 3: Now process combinations in batches using the cached quotes
    for (size_t batchStart = 0; batchStart < combinations.size(); batchStart += batchSize) {
        size_t batchEnd = std::min(batchStart + batchSize, combinations.size());
        size_t currentBatchSize = batchEnd - batchStart;
        
        m_logger->info("Processing batch {}/{} ({} combinations)",
                    (batchStart / batchSize) + 1,
                    (combinations.size() + batchSize - 1) / batchSize,
                    currentBatchSize);
        
        // Analyze combinations in this batch
        std::vector<std::future<BoxSpreadModel>> futures;
        for (size_t i = batchStart; i < batchEnd; ++i) {
            const auto& combination = combinations[i];
            futures.push_back(m_threadPool->enqueue(
                [this, underlying, exchange, expiry, combination, &optionsByStrike, &quotesCache]() {
                    // Create box spread with cached options
                    BoxSpreadModel boxSpread(underlying, exchange, combination.first, combination.second, expiry);
                    
                    // Get options from cache
                    auto lowerStrikeIt = optionsByStrike.find(combination.first);
                    auto higherStrikeIt = optionsByStrike.find(combination.second);
                    
                    if (lowerStrikeIt != optionsByStrike.end() && higherStrikeIt != optionsByStrike.end()) {
                        // Set options
                        boxSpread.longCallLower = lowerStrikeIt->second.first;   // Call at lower strike
                        boxSpread.shortPutLower = lowerStrikeIt->second.second;  // Put at lower strike
                        boxSpread.shortCallHigher = higherStrikeIt->second.first; // Call at higher strike
                        boxSpread.longPutHigher = higherStrikeIt->second.second;  // Put at higher strike
                        
                        // Get market data from quotes cache
                        auto updateFromCache = [&quotesCache](InstrumentModel& option) {
                            auto it = quotesCache.find(option.instrumentToken);
                            if (it != quotesCache.end()) {
                                option = it->second;
                                return true;
                            }
                            return false;
                        };
                        
                        // Update all options with market data
                        bool dataComplete = 
                            updateFromCache(boxSpread.longCallLower) &&
                            updateFromCache(boxSpread.shortPutLower) &&
                            updateFromCache(boxSpread.shortCallHigher) &&
                            updateFromCache(boxSpread.longPutHigher);
                        
                        if (!dataComplete) {
                            m_logger->warn("Incomplete market data for box spread: {}", boxSpread.id);
                        }
                    }
                    
                    return analyzeBoxSpread(boxSpread);
                }
            ));
        }
        
        // Collect results for this batch
        for (auto& future : futures) {
            auto boxSpread = future.get();
            if (boxSpread.hasCompleteMarketData()) {
                allSpreads.push_back(boxSpread);
            }
        }
        
        // Add delay between batches to avoid rate limits
        if (batchEnd < combinations.size()) {
            m_logger->debug("Waiting {} ms before processing next batch", delayBetweenBatchesMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(delayBetweenBatchesMs));
        }
    }
    
    // Filter for profitable spreads
    auto profitableSpreads = filterProfitableSpreads(allSpreads);
    
    m_logger->info("Found {} profitable spreads out of {} combinations", 
                 profitableSpreads.size(), combinations.size());
    
    return profitableSpreads;
}

std::vector<double> CombinationAnalyzer::findAvailableStrikes(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry) {
    
    m_logger->debug("Finding available strikes for {}:{} with expiry {}", 
                  underlying, exchange, InstrumentModel::formatDate(expiry));
    
    // Check cache first
    std::string cacheKey = generateStrikesCacheKey(underlying, exchange, expiry);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_strikesCache.find(cacheKey);
        if (it != m_strikesCache.end()) {
            m_logger->debug("Using cached strikes");
            return it->second;
        }
    }
    
    // Get all instruments
    auto instrumentsFuture = m_marketDataManager->getAllInstruments();
    auto instruments = instrumentsFuture.get();
    
    // Filter for options of the given underlying, exchange, and expiry
    std::set<double, std::less<double>> uniqueStrikes;
    for (const auto& instrument : instruments) {
        if (instrument.type == InstrumentType::OPTION && 
            instrument.underlying == underlying &&
            instrument.exchange == exchange &&
            instrument.expiry == expiry) {
            uniqueStrikes.insert(instrument.strikePrice);
        }
    }
    
    // Convert to vector and sort
    std::vector<double> result(uniqueStrikes.begin(), uniqueStrikes.end());
    std::sort(result.begin(), result.end());
    
    // Update cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_strikesCache[cacheKey] = result;
    }
    
    m_logger->debug("Found {} unique strikes", result.size());
    return result;
}

std::vector<std::pair<double, double>> CombinationAnalyzer::generateStrikeCombinations(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry,
    const std::vector<double>& strikes) {
    
    m_logger->debug("Generating strike combinations for {}:{} with expiry {}", 
                  underlying, exchange, InstrumentModel::formatDate(expiry));
    
    std::vector<std::pair<double, double>> combinations;
    
    // Get configuration
    double minStrikeDiff = m_configManager->getDoubleValue("strategy/min_strike_diff", 100.0);
    double maxStrikeDiff = m_configManager->getDoubleValue("strategy/max_strike_diff", 1000.0);
    
    // Generate combinations
    for (size_t i = 0; i < strikes.size(); ++i) {
        for (size_t j = i + 1; j < strikes.size(); ++j) {
            double lowerStrike = strikes[i];
            double higherStrike = strikes[j];
            double diff = higherStrike - lowerStrike;
            
            // Check if strike difference is within range
            if (diff >= minStrikeDiff && diff <= maxStrikeDiff) {
                combinations.emplace_back(lowerStrike, higherStrike);
            }
        }
    }
    
    m_logger->debug("Generated {} combinations with strike difference between {} and {}", 
                  combinations.size(), minStrikeDiff, maxStrikeDiff);
    
    return combinations;
}

BoxSpreadModel CombinationAnalyzer::analyzeBoxSpread(BoxSpreadModel boxSpread) {
    m_logger->debug("Analyzing box spread: {}", boxSpread.id);
    
    if (!boxSpread.hasCompleteMarketData()) {
        m_logger->warn("Box spread does not have complete market data: {}", boxSpread.id);
        return boxSpread;
    }
    
    // Get configuration
    uint64_t quantity = m_configManager->getIntValue("strategy/quantity", 1);
    double capital = m_configManager->getDoubleValue("strategy/capital", 75000.0);
    double minProfitPercentage = m_configManager->getDoubleValue("strategy/min_profit_percentage", 0.5);
    
    // Calculate theoretical value
    boxSpread.maxProfit = boxSpread.calculateTheoreticalValue();
    
    // Calculate net premium
    boxSpread.netPremium = boxSpread.calculateNetPremium();
    
    // Calculate profit/loss
    double profitLoss = boxSpread.calculateProfitLoss();
    
    // Calculate slippage
    boxSpread.slippage = boxSpread.calculateSlippage(quantity);
    
    // Calculate fees
    boxSpread.fees = boxSpread.calculateFees(quantity);
    
    // Calculate margin required
    boxSpread.margin = m_riskCalculator->calculateMarginRequired(boxSpread, quantity);
    
    // Calculate adjusted profit/loss
    double adjustedProfitLoss = profitLoss - boxSpread.slippage - boxSpread.fees;
    
    // Calculate ROI
    if (boxSpread.margin > 0) {
        boxSpread.roi = (adjustedProfitLoss / boxSpread.margin) * 100.0;
    } else {
        boxSpread.roi = 0.0;
    }
    
    // Calculate profitability score
    // Higher ROI and higher absolute profit both contribute to a higher score
    boxSpread.profitability = boxSpread.roi * std::log(1.0 + std::abs(adjustedProfitLoss));
    
    m_logger->debug("Box spread analysis: ROI={}%, ProfitLoss={}, Slippage={}, Fees={}, Margin={}",
                  boxSpread.roi, profitLoss, boxSpread.slippage, boxSpread.fees, boxSpread.margin);
    
    return boxSpread;
}

BoxSpreadModel CombinationAnalyzer::getBoxSpreadOptions(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry,
    double lowerStrike, 
    double higherStrike) {
    
    m_logger->debug("Getting box spread options for {}:{} with expiry {}, strikes {}/{}",
                  underlying, exchange, InstrumentModel::formatDate(expiry), lowerStrike, higherStrike);
    
    // Create box spread model
    BoxSpreadModel boxSpread(underlying, exchange, lowerStrike, higherStrike, expiry);
    
    // Get options for each leg
    boxSpread.longCallLower = findMostLiquidOption(underlying, exchange, expiry, lowerStrike, OptionType::CALL);
    boxSpread.shortCallHigher = findMostLiquidOption(underlying, exchange, expiry, higherStrike, OptionType::CALL);
    boxSpread.longPutHigher = findMostLiquidOption(underlying, exchange, expiry, higherStrike, OptionType::PUT);
    boxSpread.shortPutLower = findMostLiquidOption(underlying, exchange, expiry, lowerStrike, OptionType::PUT);
    
    // Get market data for each option
    std::vector<uint64_t> instrumentTokens = {
        boxSpread.longCallLower.instrumentToken,
        boxSpread.shortCallHigher.instrumentToken,
        boxSpread.longPutHigher.instrumentToken,
        boxSpread.shortPutLower.instrumentToken
    };
    
    auto quotesFuture = m_marketDataManager->getQuotes(instrumentTokens);
    auto quotes = quotesFuture.get();
    
    // Update market data
    for (const auto& token : instrumentTokens) {
        auto it = quotes.find(token);
        if (it != quotes.end()) {
            if (token == boxSpread.longCallLower.instrumentToken) {
                boxSpread.longCallLower = it->second;
            } else if (token == boxSpread.shortCallHigher.instrumentToken) {
                boxSpread.shortCallHigher = it->second;
            } else if (token == boxSpread.longPutHigher.instrumentToken) {
                boxSpread.longPutHigher = it->second;
            } else if (token == boxSpread.shortPutLower.instrumentToken) {
                boxSpread.shortPutLower = it->second;
            }
        }
    }
    
    return boxSpread;
}

InstrumentModel CombinationAnalyzer::findMostLiquidOption(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry,
    double strike, 
    OptionType optionType) {
    
    m_logger->debug("Finding most liquid {} option for {}:{} with expiry {}, strike {}",
                  InstrumentModel::optionTypeToString(optionType), underlying, exchange,
                  InstrumentModel::formatDate(expiry), strike);
    
    // Check cache first
    std::string cacheKey = generateOptionsCacheKey(underlying, exchange, expiry, strike);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_optionsCache.find(cacheKey);
        if (it != m_optionsCache.end()) {
            auto& pairMap = it->second;
            auto callPutPair = pairMap.find(strike);
            if (callPutPair != pairMap.end()) {
                const auto& pair = callPutPair->second;
                if (optionType == OptionType::CALL) {
                    return pair.first;
                } else {
                    return pair.second;
                }
            }
        }
    }
    
    // Get all instruments
    auto instrumentsFuture = m_marketDataManager->getAllInstruments();
    auto instruments = instrumentsFuture.get();
    
    // Filter for options of the given underlying, exchange, expiry, strike, and type
    std::vector<InstrumentModel> matchingOptions;
    for (const auto& instrument : instruments) {
        if (instrument.type == InstrumentType::OPTION && 
            instrument.underlying == underlying &&
            instrument.exchange == exchange &&
            instrument.expiry == expiry &&
            std::abs(instrument.strikePrice - strike) < 0.01 &&
            instrument.optionType == optionType) {
            matchingOptions.push_back(instrument);
        }
    }
    
    if (matchingOptions.empty()) {
        m_logger->warn("No matching options found");
        return InstrumentModel();
    }
    
    // Sort by trading symbol (to get a deterministic result if there are multiple matches)
    std::sort(matchingOptions.begin(), matchingOptions.end(),
             [](const InstrumentModel& a, const InstrumentModel& b) {
                 return a.tradingSymbol < b.tradingSymbol;
             });
    
    // Get market data for all matching options
    std::vector<uint64_t> instrumentTokens;
    for (const auto& option : matchingOptions) {
        instrumentTokens.push_back(option.instrumentToken);
    }
    
    auto quotesFuture = m_marketDataManager->getQuotes(instrumentTokens);
    auto quotes = quotesFuture.get();
    
    // Update market data
    for (auto& option : matchingOptions) {
        auto it = quotes.find(option.instrumentToken);
        if (it != quotes.end()) {
            option = it->second;
        }
    }
    
    // Find the most liquid option (highest volume)
    auto mostLiquidIt = std::max_element(matchingOptions.begin(), matchingOptions.end(),
                                        [](const InstrumentModel& a, const InstrumentModel& b) {
                                            return a.volume < b.volume;
                                        });
    
    InstrumentModel mostLiquid = (mostLiquidIt != matchingOptions.end()) ? *mostLiquidIt : matchingOptions[0];
    
    // Update cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_optionsCache.find(cacheKey);
        if (it != m_optionsCache.end()) {
            if (optionType == OptionType::CALL) {
                it->second[strike].first = mostLiquid;
            } else {
                it->second[strike].second = mostLiquid;
            }
        } else {
            std::map<double, std::pair<InstrumentModel, InstrumentModel>> pairMap;
            if (optionType == OptionType::CALL) {
                pairMap[strike].first = mostLiquid;
            } else {
                pairMap[strike].second = mostLiquid;
            }
            m_optionsCache[cacheKey] = pairMap;
        }
    }
    
    m_logger->debug("Found most liquid option: {}, volume: {}", 
                  mostLiquid.tradingSymbol, mostLiquid.volume);
    
    return mostLiquid;
}

std::vector<BoxSpreadModel> CombinationAnalyzer::filterProfitableSpreads(
    const std::vector<BoxSpreadModel>& boxSpreads) {
    
    m_logger->debug("Filtering {} box spreads for profitability", boxSpreads.size());
    
    // Get configuration
    double minRoi = m_configManager->getDoubleValue("strategy/min_roi", 0.5);
    double minProfitability = m_configManager->getDoubleValue("strategy/min_profitability", 0.1);
    double maxSlippage = m_configManager->getDoubleValue("strategy/max_slippage", 20.0);
    
    std::vector<BoxSpreadModel> filtered;
    
    for (const auto& boxSpread : boxSpreads) {
        // Check if the box spread is profitable
        if (boxSpread.roi >= minRoi && 
            boxSpread.profitability >= minProfitability &&
            boxSpread.slippage <= maxSlippage) {
            filtered.push_back(boxSpread);
        }
    }
    
    m_logger->debug("Filtered to {} profitable box spreads", filtered.size());
    
    return filtered;
}

std::vector<BoxSpreadModel> CombinationAnalyzer::sortByProfitability(
    const std::vector<BoxSpreadModel>& boxSpreads) {
    
    m_logger->debug("Sorting {} box spreads by profitability", boxSpreads.size());
    
    std::vector<BoxSpreadModel> sorted = boxSpreads;
    
    // Sort by profitability score in descending order
    std::sort(sorted.begin(), sorted.end(),
             [](const BoxSpreadModel& a, const BoxSpreadModel& b) {
                 return a.profitability > b.profitability;
             });
    
    return sorted;
}

std::string CombinationAnalyzer::generateStrikesCacheKey(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry) {
    
    std::stringstream ss;
    ss << underlying << ":" 
       << exchange << ":" 
       << InstrumentModel::formatDate(expiry)
       << ":strikes";
    
    return ss.str();
}

std::string CombinationAnalyzer::generateOptionsCacheKey(
    const std::string& underlying, 
    const std::string& exchange,
    const std::chrono::system_clock::time_point& expiry,
    double strike) {
    
    std::stringstream ss;
    ss << underlying << ":" 
       << exchange << ":" 
       << InstrumentModel::formatDate(expiry) << ":" 
       << std::fixed << std::setprecision(2) << strike;
    
    return ss.str();
}

}  // namespace BoxStrategy