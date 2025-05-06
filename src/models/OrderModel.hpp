 /**
 * @file OrderModel.hpp
 * @brief Model for order details
 */

#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>

namespace BoxStrategy {

/**
 * @enum OrderType
 * @brief Types of orders
 */
enum class OrderType {
    UNKNOWN,
    MARKET,
    LIMIT,
    STOP_LOSS,
    STOP_LOSS_MARKET
};

/**
 * @enum TransactionType
 * @brief Types of transactions
 */
enum class TransactionType {
    UNKNOWN,
    BUY,
    SELL
};

/**
 * @enum OrderStatus
 * @brief Status of an order
 */
enum class OrderStatus {
    UNKNOWN,
    OPEN,
    PENDING,
    COMPLETE,
    REJECTED,
    CANCELLED,
    TRIGGER_PENDING
};

/**
 * @enum ProductType
 * @brief Product types for orders
 */
enum class ProductType {
    UNKNOWN,
    CNC,     // Cash and Carry
    NRML,    // Normal (Delivery)
    MIS,     // Margin Intraday Square-off
    CO,      // Cover Order
    BO       // Bracket Order
};

/**
 * @enum Variety
 * @brief Order variety
 */
enum class Variety {
    UNKNOWN,
    REGULAR,
    AMO,     // After Market Order
    CO,      // Cover Order
    BO       // Bracket Order
};

/**
 * @enum Validity
 * @brief Order validity
 */
enum class Validity {
    UNKNOWN,
    DAY,
    IOC      // Immediate or Cancel
};

/**
 * @struct OrderModel
 * @brief Model for an order
 */
struct OrderModel {
    std::string orderId;                             ///< Unique order ID
    std::string exchangeOrderId;                     ///< Exchange order ID
    std::string parentOrderId;                       ///< Parent order ID for CO/BO
    
    std::string tradingSymbol;                       ///< Trading symbol
    std::string exchange;                            ///< Exchange
    uint64_t instrumentToken;                        ///< Instrument token
    
    TransactionType transactionType;                 ///< Transaction type (buy/sell)
    OrderType orderType;                             ///< Order type
    ProductType productType;                         ///< Product type
    Variety variety;                                 ///< Order variety
    Validity validity;                               ///< Order validity
    
    uint64_t quantity;                               ///< Order quantity
    uint64_t disclosedQuantity;                      ///< Disclosed quantity
    uint64_t filledQuantity;                         ///< Filled quantity
    uint64_t pendingQuantity;                        ///< Pending quantity
    uint64_t cancelledQuantity;                      ///< Cancelled quantity
    
    double price;                                    ///< Order price
    double triggerPrice;                             ///< Trigger price for SL orders
    double averagePrice;                             ///< Average execution price
    
    OrderStatus status;                              ///< Order status
    std::string statusMessage;                       ///< Status message
    
    std::chrono::system_clock::time_point orderTime; ///< Time of order placement
    std::chrono::system_clock::time_point exchangeUpdateTime; ///< Time of last exchange update
    
    std::string tag;                                 ///< User-defined tag for the order
    
    /**
     * @brief Default constructor
     */
    OrderModel();
    
    /**
     * @brief Convert order type to string
     * @param type Order type
     * @return String representation of the order type
     */
    static std::string orderTypeToString(OrderType type);
    
    /**
     * @brief Convert string to order type
     * @param typeStr String representation of the order type
     * @return Order type
     */
    static OrderType stringToOrderType(const std::string& typeStr);
    
    /**
     * @brief Convert transaction type to string
     * @param type Transaction type
     * @return String representation of the transaction type
     */
    static std::string transactionTypeToString(TransactionType type);
    
    /**
     * @brief Convert string to transaction type
     * @param typeStr String representation of the transaction type
     * @return Transaction type
     */
    static TransactionType stringToTransactionType(const std::string& typeStr);
    
    /**
     * @brief Convert order status to string
     * @param status Order status
     * @return String representation of the order status
     */
    static std::string orderStatusToString(OrderStatus status);
    
    /**
     * @brief Convert string to order status
     * @param statusStr String representation of the order status
     * @return Order status
     */
    static OrderStatus stringToOrderStatus(const std::string& statusStr);
    
    /**
     * @brief Convert product type to string
     * @param type Product type
     * @return String representation of the product type
     */
    static std::string productTypeToString(ProductType type);
    
    /**
     * @brief Convert string to product type
     * @param typeStr String representation of the product type
     * @return Product type
     */
    static ProductType stringToProductType(const std::string& typeStr);
    
    /**
     * @brief Convert variety to string
     * @param variety Order variety
     * @return String representation of the variety
     */
    static std::string varietyToString(Variety variety);
    
    /**
     * @brief Convert string to variety
     * @param varietyStr String representation of the variety
     * @return Order variety
     */
    static Variety stringToVariety(const std::string& varietyStr);
    
    /**
     * @brief Convert validity to string
     * @param validity Order validity
     * @return String representation of the validity
     */
    static std::string validityToString(Validity validity);
    
    /**
     * @brief Convert string to validity
     * @param validityStr String representation of the validity
     * @return Order validity
     */
    static Validity stringToValidity(const std::string& validityStr);
    
    /**
     * @brief Parse date time string to system_clock time point
     * @param dateTimeStr Date time string in format YYYY-MM-DD HH:MM:SS
     * @return Parsed time point
     */
    static std::chrono::system_clock::time_point parseDateTime(const std::string& dateTimeStr);
    
    /**
     * @brief Format time point to date time string
     * @param tp Time point to format
     * @return Formatted date time string in format YYYY-MM-DD HH:MM:SS
     */
    static std::string formatDateTime(const std::chrono::system_clock::time_point& tp);
};

}  // namespace BoxStrategy