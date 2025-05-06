/**
 * @file BoxSpreadModel.cpp
 * @brief Implementation of the BoxSpreadModel
 */

#include "../models/BoxSpreadModel.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace BoxStrategy {

BoxSpreadModel::BoxSpreadModel()
    : netPremium(0.0),
      maxProfit(0.0),
      maxLoss(0.0),
      breakEven(0.0),
      profitability(0.0),
      slippage(0.0),
      fees(0.0),
      margin(0.0),
      roi(0.0),
      allLegsExecuted(false) {
    
    strikePrices[0] = 0.0;
    strikePrices[1] = 0.0;
}

BoxSpreadModel::BoxSpreadModel(const std::string& underlying, const std::string& exchange,
                             double lowerStrike, double higherStrike,
                             const std::chrono::system_clock::time_point& expiry)
    : underlying(underlying),
      exchange(exchange),
      expiry(expiry),
      netPremium(0.0),
      maxProfit(0.0),
      maxLoss(0.0),
      breakEven(0.0),
      profitability(0.0),
      slippage(0.0),
      fees(0.0),
      margin(0.0),
      roi(0.0),
      allLegsExecuted(false) {
    
    strikePrices[0] = lowerStrike;
    strikePrices[1] = higherStrike;
    
    // Generate a unique ID for this box spread
    id = generateId();
}

double BoxSpreadModel::calculateTheoreticalValue() const {
    // The theoretical value of a box spread is the difference between the strike prices
    return strikePrices[1] - strikePrices[0];
}

double BoxSpreadModel::calculateNetPremium() const {
    // Net premium is the sum of all the premiums paid and received
    // Long positions are paid (negative cash flow)
    // Short positions are received (positive cash flow)
    
    double longCallPremium = -longCallLower.lastPrice;
    double shortCallPremium = shortCallHigher.lastPrice;
    double longPutPremium = -longPutHigher.lastPrice;
    double shortPutPremium = shortPutLower.lastPrice;
    
    return longCallPremium + shortCallPremium + longPutPremium + shortPutPremium;
}

double BoxSpreadModel::calculateProfitLoss() const {
    // Profit/loss at expiry is the difference between the theoretical value and the net premium
    return calculateTheoreticalValue() - calculateNetPremium();
}

double BoxSpreadModel::calculateROI() const {
    // ROI is the profit/loss divided by the margin required
    if (margin <= 0.0) {
        return 0.0;
    }
    
    return calculateProfitLoss() / margin * 100.0;
}

bool BoxSpreadModel::hasMispricings() const {
    // Box spread has mispricings if the net premium is not equal to the theoretical value
    // We need some tolerance for rounding errors
    const double tolerance = 0.01;
    
    double diff = std::abs(calculateNetPremium() - calculateTheoreticalValue());
    return diff > tolerance;
}

bool BoxSpreadModel::hasCompleteMarketData() const {
    // Check if all required market data is available
    if (longCallLower.lastPrice <= 0.0 || shortCallHigher.lastPrice <= 0.0 ||
        longPutHigher.lastPrice <= 0.0 || shortPutLower.lastPrice <= 0.0) {
        return false;
    }
    
    return true;
}

std::string BoxSpreadModel::generateId() const {
    std::ostringstream ss;
    ss << underlying << "_"
       << exchange << "_"
       << std::fixed << std::setprecision(2) << strikePrices[0] << "_"
       << std::fixed << std::setprecision(2) << strikePrices[1] << "_"
       << InstrumentModel::formatDate(expiry);
    
    return ss.str();
}

double BoxSpreadModel::calculateSlippage(uint64_t quantity) const {
    double totalSlippage = 0.0;
    
    // Calculate slippage for each leg based on market depth
    // For long positions, we need to estimate how much our buy order would push the price up
    // For short positions, we need to estimate how much our sell order would push the price down
    
    // Long call at lower strike (buy order)
    if (!longCallLower.sellDepth.empty()) {
        uint64_t remainingQuantity = quantity;
        double avgPrice = 0.0;
        
        for (const auto& level : longCallLower.sellDepth) {
            uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
            avgPrice += executedQuantity * level.price;
            remainingQuantity -= executedQuantity;
            
            if (remainingQuantity == 0) {
                break;
            }
        }
        
        if (remainingQuantity == 0) {
            avgPrice /= quantity;
            totalSlippage += (avgPrice - longCallLower.lastPrice) * quantity;
        } else {
            // Not enough liquidity in the order book
            // Assume a worst-case scenario with significant slippage
            totalSlippage += longCallLower.lastPrice * quantity * 0.05;  // 5% slippage
        }
    } else {
        // No market depth available, assume worst-case scenario
        totalSlippage += longCallLower.lastPrice * quantity * 0.05;  // 5% slippage
    }
    
    // Short call at higher strike (sell order)
    if (!shortCallHigher.buyDepth.empty()) {
        uint64_t remainingQuantity = quantity;
        double avgPrice = 0.0;
        
        for (const auto& level : shortCallHigher.buyDepth) {
            uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
            avgPrice += executedQuantity * level.price;
            remainingQuantity -= executedQuantity;
            
            if (remainingQuantity == 0) {
                break;
            }
        }
        
        if (remainingQuantity == 0) {
            avgPrice /= quantity;
            totalSlippage += (shortCallHigher.lastPrice - avgPrice) * quantity;
        } else {
            // Not enough liquidity in the order book
            totalSlippage += shortCallHigher.lastPrice * quantity * 0.05;  // 5% slippage
        }
    } else {
        // No market depth available, assume worst-case scenario
        totalSlippage += shortCallHigher.lastPrice * quantity * 0.05;  // 5% slippage
    }
    
    // Long put at higher strike (buy order)
    if (!longPutHigher.sellDepth.empty()) {
        uint64_t remainingQuantity = quantity;
        double avgPrice = 0.0;
        
        for (const auto& level : longPutHigher.sellDepth) {
            uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
            avgPrice += executedQuantity * level.price;
            remainingQuantity -= executedQuantity;
            
            if (remainingQuantity == 0) {
                break;
            }
        }
        
        if (remainingQuantity == 0) {
            avgPrice /= quantity;
            totalSlippage += (avgPrice - longPutHigher.lastPrice) * quantity;
        } else {
            // Not enough liquidity in the order book
            totalSlippage += longPutHigher.lastPrice * quantity * 0.05;  // 5% slippage
        }
    } else {
        // No market depth available, assume worst-case scenario
        totalSlippage += longPutHigher.lastPrice * quantity * 0.05;  // 5% slippage
    }
    
    // Short put at lower strike (sell order)
    if (!shortPutLower.buyDepth.empty()) {
        uint64_t remainingQuantity = quantity;
        double avgPrice = 0.0;
        
        for (const auto& level : shortPutLower.buyDepth) {
            uint64_t executedQuantity = std::min(remainingQuantity, level.quantity);
            avgPrice += executedQuantity * level.price;
            remainingQuantity -= executedQuantity;
            
            if (remainingQuantity == 0) {
                break;
            }
        }
        
        if (remainingQuantity == 0) {
            avgPrice /= quantity;
            totalSlippage += (shortPutLower.lastPrice - avgPrice) * quantity;
        } else {
            // Not enough liquidity in the order book
            totalSlippage += shortPutLower.lastPrice * quantity * 0.05;  // 5% slippage
        }
    } else {
        // No market depth available, assume worst-case scenario
        totalSlippage += shortPutLower.lastPrice * quantity * 0.05;  // 5% slippage
    }
    
    return totalSlippage;
}

double BoxSpreadModel::calculateFees(uint64_t quantity) const {
    // Calculate the total fees for the box spread
    // Zerodha charges the following fees:
    // - Brokerage: 20 per executed order (max 0.05% of turnover)
    // - STT: 0.05% of turnover for options
    // - Transaction charges: 0.00053% of turnover
    // - GST: 18% on (brokerage + transaction charges)
    // - SEBI charges: 10 per crore of turnover
    
    // Calculate turnover for each leg
    double longCallTurnover = longCallLower.lastPrice * quantity;
    double shortCallTurnover = shortCallHigher.lastPrice * quantity;
    double longPutTurnover = longPutHigher.lastPrice * quantity;
    double shortPutTurnover = shortPutLower.lastPrice * quantity;
    
    double totalTurnover = longCallTurnover + shortCallTurnover + longPutTurnover + shortPutTurnover;
    
    // Brokerage
    double brokerage = std::min(40.0 * 4, totalTurnover * 0.0005);  // 20 Rs per order, max 0.05%
    
    // STT (Securities Transaction Tax)
    double stt = totalTurnover * 0.0005;  // 0.05%
    
    // Transaction charges
    double transactionCharges = totalTurnover * 0.0000053;  // 0.00053%
    
    // GST (Goods and Services Tax)
    double gst = (brokerage + transactionCharges) * 0.18;  // 18%
    
    // SEBI charges
    double sebiCharges = totalTurnover * 0.0000001;  // 10 Rs per crore
    
    return brokerage + stt + transactionCharges + gst + sebiCharges;
}

}  // namespace BoxStrategy