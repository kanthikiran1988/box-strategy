 /**
 * @file PaperTrader.cpp
 * @brief Implementation of the PaperTrader class
 */

#include "../trading/PaperTrader.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace BoxStrategy {

PaperTrader::PaperTrader(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<MarketDataManager> marketDataManager,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_marketDataManager(marketDataManager),
    m_logger(logger) {
    
    m_logger->info("Initializing PaperTrader");
}

PaperTradeResult PaperTrader::simulateBoxSpreadTrade(
    const BoxSpreadModel& boxSpread, uint64_t quantity) {
    
    m_logger->info("Simulating box spread trade: {}, quantity: {}", boxSpread.id, quantity);
    
    // Create paper trade result
    PaperTradeResult result;
    result.id = generateTradeId();
    result.symbol = boxSpread.underlying;
    result.exchange = boxSpread.exchange;
    result.transactionType = TransactionType::BUY; // For box spreads, this is arbitrary
    result.quantity = quantity;
    result.executionTime = std::chrono::system_clock::now();
    result.isBox = true;
    result.boxId = boxSpread.id;
    
    // Calculate execution price, slippage, fees, and profit
    double longCallPrice = boxSpread.longCallLower.lastPrice;
    double shortCallPrice = boxSpread.shortCallHigher.lastPrice;
    double longPutPrice = boxSpread.longPutHigher.lastPrice;
    double shortPutPrice = boxSpread.shortPutLower.lastPrice;
    
    // Simulate slippage (the values from the box spread model should already include this)
    double slippage = boxSpread.slippage;
    
    // Calculate fees (the values from the box spread model should already include this)
    double fees = boxSpread.fees;
    
    // Calculate theoretical value of the box spread
    double theoreticalValue = boxSpread.calculateTheoreticalValue();
    
    // Calculate net premium
    double netPremium = (
        -longCallPrice + shortCallPrice - longPutPrice + shortPutPrice
    );
    
    // Calculate profit/loss
    double profit = (theoreticalValue - netPremium - slippage - fees) * quantity;
    
    // Calculate execution price (just for the result, not meaningful for a box spread)
    double executionPrice = netPremium;
    
    // Set result values
    result.executionPrice = executionPrice;
    result.slippage = slippage;
    result.fees = fees;
    result.profit = profit;
    
    // Add to trade results
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tradeResults.push_back(result);
    }
    
    m_logger->info("Box spread trade simulated: {}, profit: {}", result.id, result.profit);
    
    return result;
}

PaperTradeResult PaperTrader::simulateOrder(const OrderModel& order) {
    m_logger->info("Simulating order: {}, {}, {}, {}",
                 order.tradingSymbol, order.exchange,
                 OrderModel::transactionTypeToString(order.transactionType),
                 order.quantity);
    
    // Create paper trade result
    PaperTradeResult result;
    result.id = generateTradeId();
    result.symbol = order.tradingSymbol;
    result.exchange = order.exchange;
    result.transactionType = order.transactionType;
    result.quantity = order.quantity;
    result.executionTime = std::chrono::system_clock::now();
    result.isBox = false;
    result.boxId = "";
    
    // Calculate execution price based on order type
    double executionPrice = 0.0;
    
    if (order.orderType == OrderType::MARKET) {
        // For market orders, use the last price of the instrument
        auto instrumentFuture = m_marketDataManager->getInstrumentBySymbol(
            order.tradingSymbol, order.exchange);
        auto instrument = instrumentFuture.get();
        
        executionPrice = instrument.lastPrice;
        
        // Add some randomness to simulate market execution
        // Random number between -0.5% and 0.5% of the last price
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(-0.005, 0.005);
        
        double randomFactor = dis(gen);
        executionPrice *= (1.0 + randomFactor);
    } else if (order.orderType == OrderType::LIMIT) {
        // For limit orders, use the specified price
        executionPrice = order.price;
    } else if (order.orderType == OrderType::STOP_LOSS || order.orderType == OrderType::STOP_LOSS_MARKET) {
        // For stop-loss orders, use the trigger price
        executionPrice = order.triggerPrice;
    }
    
    // Calculate slippage
    double slippage = calculateSlippage(order);
    
    // Calculate fees
    double fees = calculateFees(order);
    
    // Calculate profit/loss (this is just an estimate, not meaningful for individual orders)
    double profit = 0.0;
    
    // Set result values
    result.executionPrice = executionPrice;
    result.slippage = slippage;
    result.fees = fees;
    result.profit = profit;
    
    // Add to trade results
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tradeResults.push_back(result);
    }
    
    m_logger->info("Order simulated: {}, execution price: {}", result.id, result.executionPrice);
    
    return result;
}

std::vector<PaperTradeResult> PaperTrader::getAllResults() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tradeResults;
}

std::vector<PaperTradeResult> PaperTrader::getResultsForBox(const std::string& boxId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<PaperTradeResult> boxResults;
    
    // Filter results for the specified box spread
    for (const auto& result : m_tradeResults) {
        if (result.isBox && result.boxId == boxId) {
            boxResults.push_back(result);
        }
    }
    
    return boxResults;
}

double PaperTrader::getTotalProfitLoss() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    double totalProfit = 0.0;
    
    for (const auto& result : m_tradeResults) {
        totalProfit += result.profit;
    }
    
    return totalProfit;
}

double PaperTrader::getBoxProfitLoss(const std::string& boxId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    double boxProfit = 0.0;
    
    // Calculate total profit for the specified box spread
    for (const auto& result : m_tradeResults) {
        if (result.isBox && result.boxId == boxId) {
            boxProfit += result.profit;
        }
    }
    
    return boxProfit;
}

void PaperTrader::clearResults() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tradeResults.clear();
    m_logger->info("Paper trade results cleared");
}

std::string PaperTrader::generateTradeId() const {
    // Generate a unique ID based on current time
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << "paper_trade_" << std::put_time(std::localtime(&now_c), "%Y%m%d%H%M%S") 
       << std::setfill('0') << std::setw(3) << now_ms;
    
    return ss.str();
}

double PaperTrader::calculateSlippage(const OrderModel& order) const {
    // Simulate slippage based on order size and market conditions
    // This is a simplified model and might not accurately reflect real market behavior
    
    double baseSlippagePercent = m_configManager->getDoubleValue(
        "paper_trading/base_slippage_percent", 0.1);
    
    double marketVolatilityFactor = m_configManager->getDoubleValue(
        "paper_trading/market_volatility_factor", 1.0);
    
    // Adjust slippage based on order type
    if (order.orderType == OrderType::MARKET) {
        // Market orders have higher slippage
        baseSlippagePercent *= 2.0;
    }
    
    // Generate a random factor for slippage (0.5 to 1.5)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.5, 1.5);
    
    double randomFactor = dis(gen);
    
    // Calculate slippage as a percentage of the order value
    double slippagePercent = baseSlippagePercent * marketVolatilityFactor * randomFactor;
    
    // Calculate order value
    double orderValue = order.price * order.quantity;
    
    // Calculate slippage
    double slippage = orderValue * (slippagePercent / 100.0);
    
    return slippage;
}

double PaperTrader::calculateFees(const OrderModel& order) const {
    // Simulate fees based on Zerodha's fee structure
    // This is a simplified model and might not accurately reflect real market fees
    
    // Calculate order value
    double orderValue = order.price * order.quantity;
    
    // Brokerage (flat fee per executed order or 0.03% of turnover, whichever is lower)
    double brokeragePercent = m_configManager->getDoubleValue(
        "fees/brokerage_percentage", 0.03);
    double maxBrokeragePerOrder = m_configManager->getDoubleValue(
        "fees/max_brokerage_per_order", 20.0);
    
    double brokerageByPercent = orderValue * (brokeragePercent / 100.0);
    double brokerage = std::min(brokerageByPercent, maxBrokeragePerOrder);
    
    // STT (Securities Transaction Tax)
    double sttPercent = m_configManager->getDoubleValue(
        "fees/stt_percentage", 0.025);
    double stt = 0.0;
    
    // STT is applicable only on sell side for options
    if (order.transactionType == TransactionType::SELL) {
        stt = orderValue * (sttPercent / 100.0);
    }
    
    // Exchange transaction charges
    double exchangeChargesPercent = m_configManager->getDoubleValue(
        "fees/exchange_charges_percentage", 0.00053);
    double exchangeCharges = orderValue * (exchangeChargesPercent / 100.0);
    
    // GST (Goods and Services Tax)
    double gstPercent = m_configManager->getDoubleValue(
        "fees/gst_percentage", 18.0);
    double gst = (brokerage + exchangeCharges) * (gstPercent / 100.0);
    
    // SEBI charges
    double sebiChargesPerCrore = m_configManager->getDoubleValue(
        "fees/sebi_charges_per_crore", 10.0);
    double sebiCharges = orderValue * (sebiChargesPerCrore / 10000000.0); // 1 crore = 10^7
    
    // Stamp duty
    double stampDutyPercent = m_configManager->getDoubleValue(
        "fees/stamp_duty_percentage", 0.003);
    double stampDuty = 0.0;
    
    // Stamp duty is applicable only on buy side
    if (order.transactionType == TransactionType::BUY) {
        stampDuty = orderValue * (stampDutyPercent / 100.0);
    }
    
    // Total fees
    double totalFees = brokerage + stt + exchangeCharges + gst + sebiCharges + stampDuty;
    
    return totalFees;
}

}  // namespace BoxStrategy