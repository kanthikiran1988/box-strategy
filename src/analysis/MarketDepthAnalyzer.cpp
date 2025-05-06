 /**
 * @file MarketDepthAnalyzer.cpp
 * @brief Implementation of the MarketDepthAnalyzer class
 */

#include "../analysis/MarketDepthAnalyzer.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace BoxStrategy {

MarketDepthAnalyzer::MarketDepthAnalyzer(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<MarketDataManager> marketDataManager,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_marketDataManager(marketDataManager),
    m_logger(logger) {
    
    m_logger->info("Initializing MarketDepthAnalyzer");
}

double MarketDepthAnalyzer::calculateSlippage(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating slippage for box spread: {}, quantity: {}", boxSpread.id, quantity);
    
    double totalSlippage = 0.0;
    
    // Long call at lower strike (buy order)
    totalSlippage += calculateOptionSlippage(boxSpread.longCallLower, quantity, true);
    
    // Short call at higher strike (sell order)
    totalSlippage += calculateOptionSlippage(boxSpread.shortCallHigher, quantity, false);
    
    // Long put at higher strike (buy order)
    totalSlippage += calculateOptionSlippage(boxSpread.longPutHigher, quantity, true);
    
    // Short put at lower strike (sell order)
    totalSlippage += calculateOptionSlippage(boxSpread.shortPutLower, quantity, false);
    
    m_logger->debug("Total slippage for box spread: {}: {}", boxSpread.id, totalSlippage);
    
    return totalSlippage;
}

double MarketDepthAnalyzer::calculateOptionSlippage(
    const InstrumentModel& instrument, uint64_t quantity, bool isBuy) {
    
    m_logger->debug("Calculating {} slippage for instrument: {}, quantity: {}", 
                  isBuy ? "buy" : "sell", instrument.tradingSymbol, quantity);
    
    double slippage = 0.0;
    
    if (isBuy) {
        // Buy order - need to check sell depth (asks)
        if (!instrument.sellDepth.empty()) {
            uint64_t remainingQuantity = quantity;
            double weightedAvgPrice = 0.0;
            
            for (const auto& level : instrument.sellDepth) {
                uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
                weightedAvgPrice += executedQuantity * level.price;
                remainingQuantity -= executedQuantity;
                
                if (remainingQuantity == 0) {
                    break;
                }
            }
            
            if (remainingQuantity == 0) {
                weightedAvgPrice /= quantity;
                slippage = (weightedAvgPrice - instrument.lastPrice) * quantity;
            } else {
                // Not enough liquidity in the order book
                // Assume a worst-case scenario with significant slippage
                double worstCaseSlippagePercent = m_configManager->getDoubleValue(
                    "strategy/worst_case_slippage_percent", 5.0);
                slippage = instrument.lastPrice * quantity * (worstCaseSlippagePercent / 100.0);
            }
        } else {
            // No market depth available, assume worst-case scenario
            double worstCaseSlippagePercent = m_configManager->getDoubleValue(
                "strategy/worst_case_slippage_percent", 5.0);
            slippage = instrument.lastPrice * quantity * (worstCaseSlippagePercent / 100.0);
        }
    } else {
        // Sell order - need to check buy depth (bids)
        if (!instrument.buyDepth.empty()) {
            uint64_t remainingQuantity = quantity;
            double weightedAvgPrice = 0.0;
            
            for (const auto& level : instrument.buyDepth) {
                uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
                weightedAvgPrice += executedQuantity * level.price;
                remainingQuantity -= executedQuantity;
                
                if (remainingQuantity == 0) {
                    break;
                }
            }
            
            if (remainingQuantity == 0) {
                weightedAvgPrice /= quantity;
                slippage = (instrument.lastPrice - weightedAvgPrice) * quantity;
            } else {
                // Not enough liquidity in the order book
                double worstCaseSlippagePercent = m_configManager->getDoubleValue(
                    "strategy/worst_case_slippage_percent", 5.0);
                slippage = instrument.lastPrice * quantity * (worstCaseSlippagePercent / 100.0);
            }
        } else {
            // No market depth available, assume worst-case scenario
            double worstCaseSlippagePercent = m_configManager->getDoubleValue(
                "strategy/worst_case_slippage_percent", 5.0);
            slippage = instrument.lastPrice * quantity * (worstCaseSlippagePercent / 100.0);
        }
    }
    
    m_logger->debug("Slippage for {} {}: {}", 
                  isBuy ? "buying" : "selling", instrument.tradingSymbol, slippage);
    
    return slippage;
}

bool MarketDepthAnalyzer::hasSufficientLiquidity(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Checking liquidity for box spread: {}, quantity: {}", boxSpread.id, quantity);
    
    // Check if there's enough liquidity to execute the box spread
    uint64_t availableLiquidity = calculateAvailableLiquidity(boxSpread);
    
    bool hasLiquidity = availableLiquidity >= quantity;
    
    m_logger->debug("Box spread: {} has {} liquidity. Required: {}, Available: {}", 
                  boxSpread.id, hasLiquidity ? "sufficient" : "insufficient", 
                  quantity, availableLiquidity);
    
    return hasLiquidity;
}

uint64_t MarketDepthAnalyzer::calculateAvailableLiquidity(const BoxSpreadModel& boxSpread) {
    m_logger->debug("Calculating available liquidity for box spread: {}", boxSpread.id);
    
    // Calculate available liquidity for each leg of the box spread
    uint64_t longCallLiquidity = 0;
    uint64_t shortCallLiquidity = 0;
    uint64_t longPutLiquidity = 0;
    uint64_t shortPutLiquidity = 0;
    
    // Long call at lower strike (buy order) - need sell depth (asks)
    for (const auto& level : boxSpread.longCallLower.sellDepth) {
        longCallLiquidity += level.quantity;
    }
    
    // Short call at higher strike (sell order) - need buy depth (bids)
    for (const auto& level : boxSpread.shortCallHigher.buyDepth) {
        shortCallLiquidity += level.quantity;
    }
    
    // Long put at higher strike (buy order) - need sell depth (asks)
    for (const auto& level : boxSpread.longPutHigher.sellDepth) {
        longPutLiquidity += level.quantity;
    }
    
    // Short put at lower strike (sell order) - need buy depth (bids)
    for (const auto& level : boxSpread.shortPutLower.buyDepth) {
        shortPutLiquidity += level.quantity;
    }
    
    // The available liquidity is the minimum across all legs
    uint64_t availableLiquidity = std::min({
        longCallLiquidity,
        shortCallLiquidity,
        longPutLiquidity,
        shortPutLiquidity
    });
    
    m_logger->debug("Available liquidity for box spread: {}: {}", boxSpread.id, availableLiquidity);
    
    return availableLiquidity;
}

BoxSpreadModel MarketDepthAnalyzer::refreshMarketDepth(BoxSpreadModel boxSpread) {
    m_logger->debug("Refreshing market depth for box spread: {}", boxSpread.id);
    
    // Get instrument tokens for all options in the box spread
    std::vector<uint64_t> instrumentTokens = {
        boxSpread.longCallLower.instrumentToken,
        boxSpread.shortCallHigher.instrumentToken,
        boxSpread.longPutHigher.instrumentToken,
        boxSpread.shortPutLower.instrumentToken
    };
    
    // Get quotes for all instruments
    auto quotesFuture = m_marketDataManager->getQuotes(instrumentTokens);
    auto quotes = quotesFuture.get();
    
    // Update box spread with fresh market depth data
    for (const auto& token : instrumentTokens) {
        auto it = quotes.find(token);
        if (it != quotes.end()) {
            const auto& quote = it->second;
            
            if (token == boxSpread.longCallLower.instrumentToken) {
                boxSpread.longCallLower = quote;
            } else if (token == boxSpread.shortCallHigher.instrumentToken) {
                boxSpread.shortCallHigher = quote;
            } else if (token == boxSpread.longPutHigher.instrumentToken) {
                boxSpread.longPutHigher = quote;
            } else if (token == boxSpread.shortPutLower.instrumentToken) {
                boxSpread.shortPutLower = quote;
            }
        }
    }
    
    m_logger->debug("Market depth refreshed for box spread: {}", boxSpread.id);
    
    return boxSpread;
}

double MarketDepthAnalyzer::calculateBidAskSpread(const InstrumentModel& instrument) {
    m_logger->debug("Calculating bid-ask spread for instrument: {}", instrument.tradingSymbol);
    
    double bidAskSpread = 0.0;
    
    // Check if market depth is available
    if (!instrument.buyDepth.empty() && !instrument.sellDepth.empty()) {
        // Get best bid and ask prices
        double bestBid = instrument.buyDepth[0].price;
        double bestAsk = instrument.sellDepth[0].price;
        
        // Calculate mid price
        double midPrice = (bestBid + bestAsk) / 2.0;
        
        // Calculate bid-ask spread as a percentage of mid price
        if (midPrice > 0) {
            bidAskSpread = ((bestAsk - bestBid) / midPrice) * 100.0;
        }
    }
    
    m_logger->debug("Bid-ask spread for instrument {}: {}%", 
                  instrument.tradingSymbol, bidAskSpread);
    
    return bidAskSpread;
}

std::vector<BoxSpreadModel> MarketDepthAnalyzer::filterByLiquidity(
    const std::vector<BoxSpreadModel>& boxSpreads, uint64_t quantity) {
    
    m_logger->debug("Filtering {} box spreads by liquidity for quantity: {}", 
                  boxSpreads.size(), quantity);
    
    std::vector<BoxSpreadModel> filtered;
    
    // Check each box spread for sufficient liquidity
    for (const auto& boxSpread : boxSpreads) {
        if (hasSufficientLiquidity(boxSpread, quantity)) {
            filtered.push_back(boxSpread);
        }
    }
    
    m_logger->debug("Filtered to {} box spreads with sufficient liquidity", filtered.size());
    
    return filtered;
}

std::vector<BoxSpreadModel> MarketDepthAnalyzer::sortByLiquidity(
    const std::vector<BoxSpreadModel>& boxSpreads) {
    
    m_logger->debug("Sorting {} box spreads by liquidity", boxSpreads.size());
    
    std::vector<BoxSpreadModel> sorted = boxSpreads;
    
    // Sort by available liquidity in descending order
    std::sort(sorted.begin(), sorted.end(),
             [this](const BoxSpreadModel& a, const BoxSpreadModel& b) {
                 return calculateAvailableLiquidity(a) > calculateAvailableLiquidity(b);
             });
    
    return sorted;
}

}  // namespace BoxStrategy