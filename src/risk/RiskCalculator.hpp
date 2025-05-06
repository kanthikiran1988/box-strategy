 /**
 * @file RiskCalculator.hpp
 * @brief Calculates risk metrics for box spreads
 */

#pragma once

#include <string>
#include <memory>
#include "../utils/Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../models/BoxSpreadModel.hpp"

namespace BoxStrategy {

/**
 * @class RiskCalculator
 * @brief Calculates risk metrics for box spreads
 */
class RiskCalculator {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param logger Logger instance
     */
    RiskCalculator(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~RiskCalculator() = default;
    
    /**
     * @brief Calculate margin required for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Margin required
     */
    double calculateMarginRequired(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate maximum loss for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Maximum potential loss
     */
    double calculateMaxLoss(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate maximum profit for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Maximum potential profit
     */
    double calculateMaxProfit(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate return on investment (ROI) for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Return on investment as a percentage
     */
    double calculateROI(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate break-even point for a box spread
     * @param boxSpread Box spread model
     * @return Break-even point price
     */
    double calculateBreakEven(const BoxSpreadModel& boxSpread);
    
    /**
     * @brief Check if a box spread meets the risk criteria
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return True if the box spread meets the risk criteria, false otherwise
     */
    bool meetsRiskCriteria(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate the maximum quantity that can be traded for a box spread
     * @param boxSpread Box spread model
     * @param availableCapital Available capital
     * @return Maximum quantity that can be traded
     */
    uint64_t calculateMaxQuantity(const BoxSpreadModel& boxSpread, double availableCapital);

private:
    std::shared_ptr<ConfigManager> m_configManager;  ///< Configuration manager
    std::shared_ptr<Logger> m_logger;                ///< Logger instance
};

}  // namespace BoxStrategy