 /**
 * @file FeeCalculator.hpp
 * @brief Calculates trading fees for box spreads
 */

#pragma once

#include <string>
#include <memory>
#include "../utils/Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../models/BoxSpreadModel.hpp"

namespace BoxStrategy {

/**
 * @class FeeCalculator
 * @brief Calculates trading fees for box spreads
 */
class FeeCalculator {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param logger Logger instance
     */
    FeeCalculator(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~FeeCalculator() = default;
    
    /**
     * @brief Calculate total fees for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Total fees
     */
    double calculateTotalFees(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate brokerage for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Brokerage fees
     */
    double calculateBrokerage(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate STT (Securities Transaction Tax) for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return STT fees
     */
    double calculateSTT(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate exchange transaction charges for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Exchange transaction charges
     */
    double calculateExchangeCharges(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate GST (Goods and Services Tax) for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @param brokerage Brokerage fees
     * @param exchangeCharges Exchange transaction charges
     * @return GST fees
     */
    double calculateGST(const BoxSpreadModel& boxSpread, uint64_t quantity,
                      double brokerage, double exchangeCharges);
    
    /**
     * @brief Calculate SEBI (Securities and Exchange Board of India) charges for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return SEBI charges
     */
    double calculateSEBICharges(const BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Calculate stamp duty for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Stamp duty
     */
    double calculateStampDuty(const BoxSpreadModel& boxSpread, uint64_t quantity);

private:
    std::shared_ptr<ConfigManager> m_configManager;  ///< Configuration manager
    std::shared_ptr<Logger> m_logger;                ///< Logger instance
    
    /**
     * @brief Calculate turnover for a box spread
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return Total turnover
     */
    double calculateTurnover(const BoxSpreadModel& boxSpread, uint64_t quantity);
};

}  // namespace BoxStrategy