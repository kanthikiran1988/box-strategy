 /**
 * @file FeeCalculator.cpp
 * @brief Implementation of the FeeCalculator class
 */

#include "../risk/FeeCalculator.hpp"
#include <algorithm>
#include <cmath>

namespace BoxStrategy {

FeeCalculator::FeeCalculator(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_logger(logger) {
    
    m_logger->info("Initializing FeeCalculator");
}

double FeeCalculator::calculateTotalFees(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating total fees for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Calculate brokerage, STT, exchange charges, and SEBI charges
    double brokerage = calculateBrokerage(boxSpread, quantity);
    double stt = calculateSTT(boxSpread, quantity);
    double exchangeCharges = calculateExchangeCharges(boxSpread, quantity);
    double gst = calculateGST(boxSpread, quantity, brokerage, exchangeCharges);
    double sebiCharges = calculateSEBICharges(boxSpread, quantity);
    double stampDuty = calculateStampDuty(boxSpread, quantity);
    
    // Total fees
    double totalFees = brokerage + stt + exchangeCharges + gst + sebiCharges + stampDuty;
    
    m_logger->debug("Total fees for box spread {}: {} (Brokerage: {}, STT: {}, Exchange: {}, GST: {}, SEBI: {}, Stamp: {})",
                  boxSpread.id, totalFees, brokerage, stt, exchangeCharges, gst, sebiCharges, stampDuty);
    
    return totalFees;
}

double FeeCalculator::calculateBrokerage(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating brokerage for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Zerodha charges a flat fee of Rs. 20 per executed order or 0.03% of turnover, whichever is lower
    // For a box spread, we have 4 legs, so 4 orders
    double turnover = calculateTurnover(boxSpread, quantity);
    
    double brokeragePercentage = m_configManager->getDoubleValue("fees/brokerage_percentage", 0.03);
    double maxBrokeragePerOrder = m_configManager->getDoubleValue("fees/max_brokerage_per_order", 20.0);
    
    double brokerageByPercentage = turnover * (brokeragePercentage / 100.0);
    double brokerageByFlat = maxBrokeragePerOrder * 4; // 4 legs
    
    double brokerage = std::min(brokerageByPercentage, brokerageByFlat);
    
    m_logger->debug("Brokerage for box spread {}: {}", boxSpread.id, brokerage);
    
    return brokerage;
}

double FeeCalculator::calculateSTT(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating STT for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // STT is charged on the sell side for options at 0.05% of turnover
    // For a box spread, we have 2 sell legs (short call and short put)
    double sellTurnover = (
        boxSpread.shortCallHigher.lastPrice + 
        boxSpread.shortPutLower.lastPrice
    ) * quantity;
    
    double sttPercentage = m_configManager->getDoubleValue("fees/stt_percentage", 0.05);
    double stt = sellTurnover * (sttPercentage / 100.0);
    
    m_logger->debug("STT for box spread {}: {}", boxSpread.id, stt);
    
    return stt;
}

double FeeCalculator::calculateExchangeCharges(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating exchange charges for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Exchange transaction charges are 0.00053% of turnover for options
    double turnover = calculateTurnover(boxSpread, quantity);
    
    double exchangeChargesPercentage = m_configManager->getDoubleValue("fees/exchange_charges_percentage", 0.00053);
    double exchangeCharges = turnover * (exchangeChargesPercentage / 100.0);
    
    m_logger->debug("Exchange charges for box spread {}: {}", boxSpread.id, exchangeCharges);
    
    return exchangeCharges;
}

double FeeCalculator::calculateGST(const BoxSpreadModel& boxSpread, uint64_t quantity,
                                double brokerage, double exchangeCharges) {
    m_logger->debug("Calculating GST for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // GST is 18% on (brokerage + exchange charges)
    double gstPercentage = m_configManager->getDoubleValue("fees/gst_percentage", 18.0);
    double gst = (brokerage + exchangeCharges) * (gstPercentage / 100.0);
    
    m_logger->debug("GST for box spread {}: {}", boxSpread.id, gst);
    
    return gst;
}

double FeeCalculator::calculateSEBICharges(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating SEBI charges for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // SEBI charges are Rs. 10 per crore of turnover
    double turnover = calculateTurnover(boxSpread, quantity);
    
    double sebiChargesPerCrore = m_configManager->getDoubleValue("fees/sebi_charges_per_crore", 10.0);
    double sebiCharges = turnover * (sebiChargesPerCrore / 10000000.0); // 1 crore = 10^7
    
    m_logger->debug("SEBI charges for box spread {}: {}", boxSpread.id, sebiCharges);
    
    return sebiCharges;
}

double FeeCalculator::calculateStampDuty(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating stamp duty for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Stamp duty is charged on the buy side at 0.003% of turnover
    // For a box spread, we have 2 buy legs (long call and long put)
    double buyTurnover = (
        boxSpread.longCallLower.lastPrice + 
        boxSpread.longPutHigher.lastPrice
    ) * quantity;
    
    double stampDutyPercentage = m_configManager->getDoubleValue("fees/stamp_duty_percentage", 0.003);
    double stampDuty = buyTurnover * (stampDutyPercentage / 100.0);
    
    m_logger->debug("Stamp duty for box spread {}: {}", boxSpread.id, stampDuty);
    
    return stampDuty;
}

double FeeCalculator::calculateTurnover(const BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->debug("Calculating turnover for box spread: {}, quantity: {}", 
                  boxSpread.id, quantity);
    
    // Turnover is the sum of the premium of all legs
    double turnover = (
        boxSpread.longCallLower.lastPrice + 
        boxSpread.shortCallHigher.lastPrice + 
        boxSpread.longPutHigher.lastPrice + 
        boxSpread.shortPutLower.lastPrice
    ) * quantity;
    
    m_logger->debug("Turnover for box spread {}: {}", boxSpread.id, turnover);
    
    return turnover;
}

}  // namespace BoxStrategy