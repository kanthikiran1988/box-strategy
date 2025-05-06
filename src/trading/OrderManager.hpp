 /**
 * @file OrderManager.hpp
 * @brief Manages orders for box spreads
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <future>
#include "../utils/Logger.hpp"
#include "../utils/HttpClient.hpp"
#include "../config/ConfigManager.hpp"
#include "../auth/AuthManager.hpp"
#include "../models/OrderModel.hpp"
#include "../models/BoxSpreadModel.hpp"

namespace BoxStrategy {

/**
 * @class OrderManager
 * @brief Manages orders for box spreads
 */
class OrderManager {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param authManager Authentication manager
     * @param httpClient HTTP client
     * @param logger Logger instance
     */
    OrderManager(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<AuthManager> authManager,
        std::shared_ptr<HttpClient> httpClient,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~OrderManager() = default;
    
    /**
     * @brief Place a box spread order
     * @param boxSpread Box spread model
     * @param quantity Quantity to trade
     * @return True if successful, false otherwise
     */
    bool placeBoxSpreadOrder(BoxSpreadModel& boxSpread, uint64_t quantity);
    
    /**
     * @brief Place an individual order
     * @param order Order model
     * @return Order ID if successful, empty string otherwise
     */
    std::string placeOrder(OrderModel& order);
    
    /**
     * @brief Place an order asynchronously
     * @param order Order model
     * @return Future with order ID if successful, empty string otherwise
     */
    std::future<std::string> placeOrderAsync(OrderModel order);
    
    /**
     * @brief Modify an order
     * @param orderId Order ID
     * @param order Updated order model
     * @return True if successful, false otherwise
     */
    bool modifyOrder(const std::string& orderId, OrderModel& order);
    
    /**
     * @brief Cancel an order
     * @param orderId Order ID
     * @return True if successful, false otherwise
     */
    bool cancelOrder(const std::string& orderId);
    
    /**
     * @brief Get order status
     * @param orderId Order ID
     * @return Order model with status
     */
    OrderModel getOrderStatus(const std::string& orderId);
    
    /**
     * @brief Get all orders
     * @return Vector of order models
     */
    std::vector<OrderModel> getAllOrders();
    
    /**
     * @brief Get all trades
     * @return Vector of order models with trade information
     */
    std::vector<OrderModel> getAllTrades();
    
    /**
     * @brief Create a market order
     * @param tradingSymbol Trading symbol
     * @param exchange Exchange
     * @param transactionType Transaction type (buy/sell)
     * @param quantity Quantity to trade
     * @param productType Product type
     * @return Order model
     */
    OrderModel createMarketOrder(
        const std::string& tradingSymbol,
        const std::string& exchange,
        TransactionType transactionType,
        uint64_t quantity,
        ProductType productType = ProductType::NRML
    );
    
    /**
     * @brief Create a limit order
     * @param tradingSymbol Trading symbol
     * @param exchange Exchange
     * @param transactionType Transaction type (buy/sell)
     * @param quantity Quantity to trade
     * @param price Limit price
     * @param productType Product type
     * @return Order model
     */
    OrderModel createLimitOrder(
        const std::string& tradingSymbol,
        const std::string& exchange,
        TransactionType transactionType,
        uint64_t quantity,
        double price,
        ProductType productType = ProductType::NRML
    );
    
    /**
     * @brief Wait for all legs of a box spread to be executed
     * @param boxSpread Box spread model
     * @param timeout Timeout in seconds
     * @return Updated box spread model with execution status
     */
    BoxSpreadModel waitForBoxSpreadExecution(
        BoxSpreadModel boxSpread, 
        int timeout = 60
    );
    
    /**
     * @brief Check if all legs of a box spread have been executed
     * @param boxSpread Box spread model
     * @return True if all legs have been executed, false otherwise
     */
    bool isBoxSpreadExecuted(const BoxSpreadModel& boxSpread);

private:
    std::shared_ptr<ConfigManager> m_configManager;  ///< Configuration manager
    std::shared_ptr<AuthManager> m_authManager;      ///< Authentication manager
    std::shared_ptr<HttpClient> m_httpClient;        ///< HTTP client
    std::shared_ptr<Logger> m_logger;                ///< Logger instance
    
    // Cache of orders
    std::unordered_map<std::string, OrderModel> m_orderCache;
    
    // Mutex for thread safety
    std::mutex m_cacheMutex;
    
    /**
     * @brief Make API request with proper authentication
     * @param method HTTP method
     * @param endpoint API endpoint
     * @param params Optional query parameters
     * @param body Optional request body
     * @return HTTP response
     */
    HttpResponse makeApiRequest(
        HttpMethod method,
        const std::string& endpoint,
        const std::unordered_map<std::string, std::string>& params = {},
        const std::string& body = ""
    );
    
    /**
     * @brief Parse order from JSON
     * @param orderJson Order JSON
     * @return Order model
     */
    OrderModel parseOrderJson(const nlohmann::json& orderJson);
    
    /**
     * @brief Build order request body
     * @param order Order model
     * @return Request body as a string
     */
    std::string buildOrderRequestBody(const OrderModel& order);
    
    /**
     * @brief Update order cache
     * @param order Order model
     */
    void updateOrderCache(const OrderModel& order);
    
    /**
     * @brief Get order from cache
     * @param orderId Order ID
     * @return Order model if found, empty order otherwise
     */
    OrderModel getOrderFromCache(const std::string& orderId);
};

}  // namespace BoxStrategy