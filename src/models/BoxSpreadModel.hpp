/**
 * @file BoxSpreadModel.hpp
 * @brief Model for box spread strategy
 */

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <array>
#include "../models/InstrumentModel.hpp"
#include "../models/OrderModel.hpp"

namespace BoxStrategy {

/**
 * @struct BoxSpreadModel
 * @brief Model for a box spread strategy
 */
struct BoxSpreadModel {
    std::string id;                       ///< Unique identifier for the box spread
    std::string underlying;               ///< Underlying instrument
    std::string exchange;                 ///< Exchange
    
    std::array<double, 2> strikePrices;   ///< Strike prices for the box spread
    std::chrono::system_clock::time_point expiry;  ///< Expiry date
    
    // Box spread consists of 4 options:
    // 1. Long call at lower strike (buy call option at lower strike)
    // 2. Short call at higher strike (sell call option at higher strike)
    // 3. Long put at higher strike (buy put option at higher strike)
    // 4. Short put at lower strike (sell put option at lower strike)
    InstrumentModel longCallLower;       ///< Long call option at lower strike
    InstrumentModel shortCallHigher;     ///< Short call option at higher strike
    InstrumentModel longPutHigher;       ///< Long put option at higher strike
    InstrumentModel shortPutLower;       ///< Short put option at lower strike
    
    // Orders for each leg
    OrderModel longCallLowerOrder;       ///< Order for long call option at lower strike
    OrderModel shortCallHigherOrder;     ///< Order for short call option at higher strike
    OrderModel longPutHigherOrder;       ///< Order for long put option at higher strike
    OrderModel shortPutLowerOrder;       ///< Order for short put option at lower strike
    
    double netPremium;                   ///< Net premium paid/received
    double maxProfit;                    ///< Maximum potential profit
    double maxLoss;                      ///< Maximum potential loss
    double breakEven;                    ///< Break-even point
    double profitability;                ///< Profitability score
    double slippage;                     ///< Estimated slippage
    double fees;                         ///< Total fees (brokerage, STT, GST, etc.)
    double margin;                       ///< Margin required
    double originalMargin;               ///< Original margin calculation when using average margin
    double roi;                          ///< Return on investment
    
    bool allLegsExecuted;                ///< Whether all legs have been executed
    
    /**
     * @brief Default constructor
     */
    BoxSpreadModel();
    
    /**
     * @brief Constructor with underlying and strikes
     * @param underlying Underlying instrument
     * @param exchange Exchange
     * @param lowerStrike Lower strike price
     * @param higherStrike Higher strike price
     * @param expiry Expiry date
     */
    BoxSpreadModel(const std::string& underlying, const std::string& exchange,
                  double lowerStrike, double higherStrike,
                  const std::chrono::system_clock::time_point& expiry);
    
    /**
     * @brief Calculate the theoretical value of the box spread
     * @return Theoretical value of the box spread
     */
    double calculateTheoreticalValue() const;
    
    /**
     * @brief Calculate the net premium of the box spread
     * @return Net premium of the box spread
     */
    double calculateNetPremium() const;
    
    /**
     * @brief Calculate the profit/loss at expiry
     * @return Profit/loss at expiry
     */
    double calculateProfitLoss() const;
    
    /**
     * @brief Calculate the return on investment
     * @return Return on investment
     */
    double calculateROI() const;
    
    /**
     * @brief Check if the box spread has mispricings that can be exploited
     * @return True if there are mispricings, false otherwise
     */
    bool hasMispricings() const;
    
    /**
     * @brief Check if all required market data is available
     * @return True if all required market data is available, false otherwise
     */
    bool hasCompleteMarketData() const;
    
    /**
     * @brief Generate a unique identifier for the box spread
     * @return Unique identifier for the box spread
     */
    std::string generateId() const;
    
    /**
     * @brief Calculate the total slippage for the box spread
     * @param marketDepth Market depth for each leg
     * @param quantity Quantity for each leg
     * @return Estimated slippage
     */
    double calculateSlippage(uint64_t quantity) const;
    
    /**
     * @brief Calculate the total fees for the box spread
     * @param quantity Quantity for each leg
     * @return Total fees
     */
    double calculateFees(uint64_t quantity) const;
};

}  // namespace BoxStrategy