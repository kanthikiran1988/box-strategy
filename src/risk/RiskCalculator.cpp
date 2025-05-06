 /**
 * @file RiskCalculator.cpp
 * @brief Implementation of the RiskCalculator class
 */

#include "../risk/RiskCalculator.hpp"
#include <algorithm>
#include <cmath>

namespace BoxStrategy {

RiskCalculator::RiskCalculator(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_logger(logger) {
    
    m_logger->info("Initializing RiskCalculator");
}

double RiskCalculator::calculateMarginRequired(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating margin required for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // For box spreads, the margin required is calculated based on the maximum potential loss
    double maxLoss = calculateMaxLoss(boxSpread, quantity);
    
    // Add a margin buffer as per Zerodha's SPAN margin requirements
    double marginBuffer = m_configManager->getDoubleValue("risk/margin_buffer_percentage", 25.0);
    double spanMargin = maxLoss * (1.0 + marginBuffer / 100.0);
    
    // Add exposure margin
    double exposureMarginPercentage = m_configManager->getDoubleValue("risk/exposure_margin_percentage", 3.0);
    
    // Calculate total premium
    double totalPremium = (
        boxSpread.longCallLower.lastPrice + 
        boxSpread.shortCallHigher.lastPrice + 
        boxSpread.longPutHigher.lastPrice + 
        boxSpread.shortPutLower.lastPrice
    ) * quantity;
    
    double exposureMargin = totalPremium * (exposureMarginPercentage / 100.0);
    
    // Total margin required
    double totalMargin = spanMargin + exposureMargin;
    
    m_logger->debug("Margin required for box spread {}: {}", boxSpread.id, totalMargin);
    
    return totalMargin;
}

double RiskCalculator::calculateMaxLoss(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating maximum loss for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // For a box spread, the maximum loss depends on the net premium paid/received
    double netPremium = boxSpread.calculateNetPremium();
    
    // If net premium is negative (paid premium), then maximum loss is the premium paid
    double maxLoss = 0.0;
    if (netPremium < 0) {
        maxLoss = -netPremium * quantity;
    } else {
        // If net premium is positive (received premium), then there is no maximum loss
        // But we consider the potential fees and slippage
        maxLoss = (boxSpread.fees + boxSpread.slippage) * quantity;
    }
    
    m_logger->debug("Maximum loss for box spread {}: {}", boxSpread.id, maxLoss);
    
    return maxLoss;
}

double RiskCalculator::calculateMaxProfit(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating maximum profit for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // For a box spread, the maximum profit is the difference between the theoretical value and the net premium
    double theoreticalValue = boxSpread.calculateTheoreticalValue();
    double netPremium = boxSpread.calculateNetPremium();
    
    double profitLoss = theoreticalValue - netPremium;
    
    // Adjust for fees and slippage
    double adjustedProfitLoss = profitLoss - boxSpread.fees - boxSpread.slippage;
    
    // Maximum profit
    double maxProfit = adjustedProfitLoss * quantity;
    if (maxProfit < 0) {
        maxProfit = 0.0;
    }
    
    m_logger->debug("Maximum profit for box spread {}: {}", boxSpread.id, maxProfit);
    
    return maxProfit;
}

double RiskCalculator::calculateROI(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating ROI for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    double maxProfit = calculateMaxProfit(boxSpread, quantity);
    double marginRequired = calculateMarginRequired(boxSpread, quantity);
    
    double roi = 0.0;
    if (marginRequired > 0) {
        roi = (maxProfit / marginRequired) * 100.0;
    }
    
    m_logger->debug("ROI for box spread {}: {}%", boxSpread.id, roi);
    
    return roi;
}

double RiskCalculator::calculateBreakEven(const BoxSpreadModel& boxSpread) {
    m_logger->debug("Calculating break-even point for box spread: {}", boxSpread.id);
    
    // For a box spread, the break-even point depends on the net premium and the strike prices
    double netPremium = boxSpread.calculateNetPremium();
    
    // Break-even is not really applicable for box spreads in the traditional sense
    // since they have a fixed payout at expiry. However, we can calculate a 
    // "synthetic" break-even based on the fees and slippage.
    
    double breakEven = boxSpread.fees + boxSpread.slippage;
    
    m_logger->debug("Break-even for box spread {}: {}", boxSpread.id, breakEven);
    
    return breakEven;
}

bool RiskCalculator::meetsRiskCriteria(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Checking risk criteria for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Get configuration
    double minRoi = m_configManager->getDoubleValue("risk/min_roi_percentage", 0.5);
    double maxLossPercentage = m_configManager->getDoubleValue("risk/max_loss_percentage", 2.0);
    double availableCapital = m_configManager->getDoubleValue("strategy/capital", 75000.0);
    
    // Calculate risk metrics
    double roi = calculateROI(boxSpread, quantity);
    double maxLoss = calculateMaxLoss(boxSpread, quantity);
    double calculatedMaxLossPercentage = (maxLoss / availableCapital) * 100.0;
    
    // Check if the box spread meets the risk criteria
    bool meetsRoi = roi >= minRoi;
    bool meetsMaxLoss = calculatedMaxLossPercentage <= maxLossPercentage;
    
    bool meetsCriteria = meetsRoi && meetsMaxLoss;
    
    m_logger->debug("Box spread {} {} risk criteria. ROI: {}%, Max Loss: {}%, Max Loss Percentage: {}%", 
                  boxSpread.id, meetsCriteria ? "meets" : "does not meet", 
                  roi, maxLoss, calculatedMaxLossPercentage);
    
    return meetsCriteria;
}

uint64_t RiskCalculator::calculateMaxQuantity(const BoxSpreadModel& boxSpread, double availableCapital) {
    m_logger->debug("Calculating maximum quantity for box spread: {}, available capital: {}", 
                  boxSpread.id, availableCapital);
    
    // Calculate margin required for a single quantity
    double marginPerUnit = calculateMarginRequired(boxSpread, 1);
    
    // Calculate maximum quantity based on available capital
    uint64_t maxQuantity = static_cast<uint64_t>(availableCapital / marginPerUnit);
    
    // Apply a safety factor to avoid using up all capital
    double safetyFactor = m_configManager->getDoubleValue("risk/capital_safety_factor", 0.9);
    maxQuantity = static_cast<uint64_t>(maxQuantity * safetyFactor);
    
    // Ensure maxQuantity is at least 1
    maxQuantity = std::max<uint64_t>(1, maxQuantity);
    
    m_logger->debug("Maximum quantity for box spread {}: {}", boxSpread.id, maxQuantity);
    
    return maxQuantity;
}

}  // namespace BoxStrategy