 /**
 * @file OrderManager.cpp
 * @brief Implementation of the OrderManager class
 */

#include "../trading/OrderManager.hpp"
#include <sstream>
#include <algorithm>
#include "../external/json.hpp"

using json = nlohmann::json;

namespace BoxStrategy {

OrderManager::OrderManager(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<AuthManager> authManager,
    std::shared_ptr<HttpClient> httpClient,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_authManager(authManager),
    m_httpClient(httpClient),
    m_logger(logger) {
    
    m_logger->info("Initializing OrderManager");
}

bool OrderManager::placeBoxSpreadOrder(BoxSpreadModel& boxSpread, uint64_t quantity) {
    m_logger->info("Placing box spread order for {}, quantity: {}", boxSpread.id, quantity);
    
    bool isPaperTrading = m_configManager->getBoolValue("strategy/paper_trading", true);
    if (isPaperTrading) {
        m_logger->info("Paper trading mode is enabled, not placing actual orders");
        // Set dummy order IDs for paper trading
        boxSpread.longCallLowerOrder.orderId = "paper_" + boxSpread.id + "_longCall";
        boxSpread.shortCallHigherOrder.orderId = "paper_" + boxSpread.id + "_shortCall";
        boxSpread.longPutHigherOrder.orderId = "paper_" + boxSpread.id + "_longPut";
        boxSpread.shortPutLowerOrder.orderId = "paper_" + boxSpread.id + "_shortPut";
        return true;
    }
    
    // Create orders for each leg of the box spread
    OrderModel longCallOrder = createLimitOrder(
        boxSpread.longCallLower.tradingSymbol,
        boxSpread.longCallLower.exchange,
        TransactionType::BUY,
        quantity,
        boxSpread.longCallLower.lastPrice
    );
    
    OrderModel shortCallOrder = createLimitOrder(
        boxSpread.shortCallHigher.tradingSymbol,
        boxSpread.shortCallHigher.exchange,
        TransactionType::SELL,
        quantity,
        boxSpread.shortCallHigher.lastPrice
    );
    
    OrderModel longPutOrder = createLimitOrder(
        boxSpread.longPutHigher.tradingSymbol,
        boxSpread.longPutHigher.exchange,
        TransactionType::BUY,
        quantity,
        boxSpread.longPutHigher.lastPrice
    );
    
    OrderModel shortPutOrder = createLimitOrder(
        boxSpread.shortPutLower.tradingSymbol,
        boxSpread.shortPutLower.exchange,
        TransactionType::SELL,
        quantity,
        boxSpread.shortPutLower.lastPrice
    );
    
    // Place orders
    std::vector<std::future<std::string>> orderFutures;
    orderFutures.push_back(placeOrderAsync(longCallOrder));
    orderFutures.push_back(placeOrderAsync(shortCallOrder));
    orderFutures.push_back(placeOrderAsync(longPutOrder));
    orderFutures.push_back(placeOrderAsync(shortPutOrder));
    
    // Wait for all orders to be placed
    bool allOrdersPlaced = true;
    std::string longCallOrderId = orderFutures[0].get();
    std::string shortCallOrderId = orderFutures[1].get();
    std::string longPutOrderId = orderFutures[2].get();
    std::string shortPutOrderId = orderFutures[3].get();
    
    if (longCallOrderId.empty() || shortCallOrderId.empty() ||
        longPutOrderId.empty() || shortPutOrderId.empty()) {
        allOrdersPlaced = false;
    }
    
    // Update box spread with order IDs
    if (allOrdersPlaced) {
        boxSpread.longCallLowerOrder = getOrderStatus(longCallOrderId);
        boxSpread.shortCallHigherOrder = getOrderStatus(shortCallOrderId);
        boxSpread.longPutHigherOrder = getOrderStatus(longPutOrderId);
        boxSpread.shortPutLowerOrder = getOrderStatus(shortPutOrderId);
    }
    
    m_logger->info("Box spread order {} placed: {}", boxSpread.id, allOrdersPlaced ? "success" : "failure");
    
    return allOrdersPlaced;
}

std::string OrderManager::placeOrder(OrderModel& order) {
    m_logger->debug("Placing order: {}, {}, {}, {}, {}",
                  order.tradingSymbol,
                  OrderModel::transactionTypeToString(order.transactionType),
                  OrderModel::orderTypeToString(order.orderType),
                  order.quantity,
                  order.price);
    
    std::string variety = OrderModel::varietyToString(order.variety);
    std::string endpoint = "/orders/" + variety;
    std::string requestBody = buildOrderRequestBody(order);
    
    HttpResponse response = makeApiRequest(HttpMethod::POST, endpoint, {}, requestBody);
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                std::string orderId = responseJson["data"]["order_id"].get<std::string>();
                m_logger->info("Order placed successfully. Order ID: {}", orderId);
                
                // Update order with the ID
                order.orderId = orderId;
                
                // Get full order details
                order = getOrderStatus(orderId);
                
                // Update cache
                updateOrderCache(order);
                
                return orderId;
            } else {
                m_logger->error("Failed to place order: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing order response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to place order. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return "";
}

std::future<std::string> OrderManager::placeOrderAsync(OrderModel order) {
    return std::async(std::launch::async, [this, order]() mutable {
        return this->placeOrder(order);
    });
}

bool OrderManager::modifyOrder(const std::string& orderId, OrderModel& order) {
    m_logger->debug("Modifying order: {}", orderId);
    
    std::string variety = OrderModel::varietyToString(order.variety);
    std::string endpoint = "/orders/" + variety + "/" + orderId;
    std::string requestBody = buildOrderRequestBody(order);
    
    HttpResponse response = makeApiRequest(HttpMethod::PUT, endpoint, {}, requestBody);
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                m_logger->info("Order modified successfully. Order ID: {}", orderId);
                
                // Get full order details
                order = getOrderStatus(orderId);
                
                // Update cache
                updateOrderCache(order);
                
                return true;
            } else {
                m_logger->error("Failed to modify order: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing order response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to modify order. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return false;
}

bool OrderManager::cancelOrder(const std::string& orderId) {
    m_logger->debug("Cancelling order: {}", orderId);
    
    // Get order from cache
    OrderModel order = getOrderFromCache(orderId);
    if (order.orderId.empty()) {
        m_logger->error("Order not found in cache: {}", orderId);
        return false;
    }
    
    std::string variety = OrderModel::varietyToString(order.variety);
    std::string endpoint = "/orders/" + variety + "/" + orderId;
    
    HttpResponse response = makeApiRequest(HttpMethod::DELETE, endpoint);
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                m_logger->info("Order cancelled successfully. Order ID: {}", orderId);
                
                // Get full order details
                order = getOrderStatus(orderId);
                
                // Update cache
                updateOrderCache(order);
                
                return true;
            } else {
                m_logger->error("Failed to cancel order: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing order response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to cancel order. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return false;
}

OrderModel OrderManager::getOrderStatus(const std::string& orderId) {
    m_logger->debug("Getting order status: {}", orderId);
    
    // Check if it's a paper trading order
    if (orderId.find("paper_") == 0) {
        m_logger->debug("Paper trading order, returning cached order");
        return getOrderFromCache(orderId);
    }
    
    std::string endpoint = "/orders/" + orderId;
    
    HttpResponse response = makeApiRequest(HttpMethod::GET, endpoint);
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                m_logger->debug("Order status retrieved successfully. Order ID: {}", orderId);
                
                // Parse order from JSON
                OrderModel order = parseOrderJson(responseJson["data"][0]);
                
                // Update cache
                updateOrderCache(order);
                
                return order;
            } else {
                m_logger->error("Failed to get order status: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing order response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to get order status. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return OrderModel();
}

std::vector<OrderModel> OrderManager::getAllOrders() {
    m_logger->debug("Getting all orders");
    
    std::string endpoint = "/orders";
    
    HttpResponse response = makeApiRequest(HttpMethod::GET, endpoint);
    
    std::vector<OrderModel> orders;
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                m_logger->debug("All orders retrieved successfully");
                
                json ordersJson = responseJson["data"];
                for (const auto& orderJson : ordersJson) {
                    OrderModel order = parseOrderJson(orderJson);
                    orders.push_back(order);
                    
                    // Update cache
                    updateOrderCache(order);
                }
            } else {
                m_logger->error("Failed to get all orders: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing orders response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to get all orders. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return orders;
}

std::vector<OrderModel> OrderManager::getAllTrades() {
    m_logger->debug("Getting all trades");
    
    std::string endpoint = "/trades";
    
    HttpResponse response = makeApiRequest(HttpMethod::GET, endpoint);
    
    std::vector<OrderModel> trades;
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                m_logger->debug("All trades retrieved successfully");
                
                json tradesJson = responseJson["data"];
                for (const auto& tradeJson : tradesJson) {
                    OrderModel trade = parseOrderJson(tradeJson);
                    trades.push_back(trade);
                }
            } else {
                m_logger->error("Failed to get all trades: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing trades response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to get all trades. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return trades;
}

OrderModel OrderManager::createMarketOrder(
    const std::string& tradingSymbol,
    const std::string& exchange,
    TransactionType transactionType,
    uint64_t quantity,
    ProductType productType
) {
    m_logger->debug("Creating market order: {}, {}, {}, {}",
                  tradingSymbol, exchange,
                  OrderModel::transactionTypeToString(transactionType),
                  quantity);
    
    OrderModel order;
    order.tradingSymbol = tradingSymbol;
    order.exchange = exchange;
    order.transactionType = transactionType;
    order.orderType = OrderType::MARKET;
    order.productType = productType;
    order.variety = Variety::REGULAR;
    order.validity = Validity::DAY;
    order.quantity = quantity;
    order.disclosedQuantity = 0;
    order.price = 0.0;
    order.triggerPrice = 0.0;
    
    return order;
}

OrderModel OrderManager::createLimitOrder(
    const std::string& tradingSymbol,
    const std::string& exchange,
    TransactionType transactionType,
    uint64_t quantity,
    double price,
    ProductType productType
) {
    m_logger->debug("Creating limit order: {}, {}, {}, {}, {}",
                  tradingSymbol, exchange,
                  OrderModel::transactionTypeToString(transactionType),
                  quantity, price);
    
    OrderModel order;
    order.tradingSymbol = tradingSymbol;
    order.exchange = exchange;
    order.transactionType = transactionType;
    order.orderType = OrderType::LIMIT;
    order.productType = productType;
    order.variety = Variety::REGULAR;
    order.validity = Validity::DAY;
    order.quantity = quantity;
    order.disclosedQuantity = 0;
    order.price = price;
    order.triggerPrice = 0.0;
    
    return order;
}

BoxSpreadModel OrderManager::waitForBoxSpreadExecution(
    BoxSpreadModel boxSpread, 
    int timeout
) {
    m_logger->info("Waiting for box spread execution: {}, timeout: {}s", boxSpread.id, timeout);
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        // Check if timeout has been reached
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - startTime).count();
        if (elapsedSeconds >= timeout) {
            m_logger->warn("Timeout reached while waiting for box spread execution: {}", boxSpread.id);
            break;
        }
        
        // Check if all legs have been executed
        if (isBoxSpreadExecuted(boxSpread)) {
            m_logger->info("All legs of box spread have been executed: {}", boxSpread.id);
            boxSpread.allLegsExecuted = true;
            break;
        }
        
        // Update order statuses
        if (!boxSpread.longCallLowerOrder.orderId.empty()) {
            boxSpread.longCallLowerOrder = getOrderStatus(boxSpread.longCallLowerOrder.orderId);
        }
        
        if (!boxSpread.shortCallHigherOrder.orderId.empty()) {
            boxSpread.shortCallHigherOrder = getOrderStatus(boxSpread.shortCallHigherOrder.orderId);
        }
        
        if (!boxSpread.longPutHigherOrder.orderId.empty()) {
            boxSpread.longPutHigherOrder = getOrderStatus(boxSpread.longPutHigherOrder.orderId);
        }
        
        if (!boxSpread.shortPutLowerOrder.orderId.empty()) {
            boxSpread.shortPutLowerOrder = getOrderStatus(boxSpread.shortPutLowerOrder.orderId);
        }
        
        // Sleep for a short time before checking again
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return boxSpread;
}

bool OrderManager::isBoxSpreadExecuted(const BoxSpreadModel& boxSpread) {
    m_logger->debug("Checking if box spread is executed: {}", boxSpread.id);
    
    // Check if all legs have been executed (filled completely)
    bool longCallExecuted = 
        boxSpread.longCallLowerOrder.status == OrderStatus::COMPLETE && 
        boxSpread.longCallLowerOrder.filledQuantity == boxSpread.longCallLowerOrder.quantity;
    
    bool shortCallExecuted = 
        boxSpread.shortCallHigherOrder.status == OrderStatus::COMPLETE && 
        boxSpread.shortCallHigherOrder.filledQuantity == boxSpread.shortCallHigherOrder.quantity;
    
    bool longPutExecuted = 
        boxSpread.longPutHigherOrder.status == OrderStatus::COMPLETE && 
        boxSpread.longPutHigherOrder.filledQuantity == boxSpread.longPutHigherOrder.quantity;
    
    bool shortPutExecuted = 
        boxSpread.shortPutLowerOrder.status == OrderStatus::COMPLETE && 
        boxSpread.shortPutLowerOrder.filledQuantity == boxSpread.shortPutLowerOrder.quantity;
    
    bool allExecuted = longCallExecuted && shortCallExecuted && longPutExecuted && shortPutExecuted;
    
    m_logger->debug("Box spread {} is {}", boxSpread.id, allExecuted ? "executed" : "not executed");
    
    return allExecuted;
}

HttpResponse OrderManager::makeApiRequest(
    HttpMethod method,
    const std::string& endpoint,
    const std::unordered_map<std::string, std::string>& params,
    const std::string& body
) {
    if (!m_authManager->isAccessTokenValid()) {
        m_logger->error("Access token is not valid for API request");
        return HttpResponse{401, "Access token is not valid", {}};
    }
    
    std::string url = "https://api.kite.trade" + endpoint;
    
    // Add query parameters to URL
    if (!params.empty()) {
        url += "?";
        bool first = true;
        
        for (const auto& param : params) {
            if (!first) {
                url += "&";
            }
            
            url += param.first + "=" + param.second;
            first = false;
        }
    }
    
    // Set headers
    std::unordered_map<std::string, std::string> headers = {
        {"X-Kite-Version", "3"},
        {"Authorization", "token " + m_authManager->getApiKey() + ":" + m_authManager->getAccessToken()},
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    
    // Make the request
    HttpResponse response = m_httpClient->request(method, url, headers, body);
    
    // Check for authentication error
    if (response.statusCode == 403 || response.statusCode == 401) {
        m_logger->warn("Authentication error in API request. Status code: {}", response.statusCode);
        
        // Clear the access token so that it's renewed on the next request
        m_authManager->invalidateAccessToken();
    }
    
    return response;
}

OrderModel OrderManager::parseOrderJson(const nlohmann::json& orderJson) {
    OrderModel order;
    
    try {
        // Parse order ID
        if (orderJson.contains("order_id")) {
            order.orderId = orderJson["order_id"].get<std::string>();
        }
        
        // Parse exchange order ID
        if (orderJson.contains("exchange_order_id")) {
            order.exchangeOrderId = orderJson["exchange_order_id"].get<std::string>();
        }
        
        // Parse parent order ID
        if (orderJson.contains("parent_order_id")) {
            order.parentOrderId = orderJson["parent_order_id"].get<std::string>();
        }
        
        // Parse trading symbol
        if (orderJson.contains("tradingsymbol")) {
            order.tradingSymbol = orderJson["tradingsymbol"].get<std::string>();
        }
        
        // Parse exchange
        if (orderJson.contains("exchange")) {
            order.exchange = orderJson["exchange"].get<std::string>();
        }
        
        // Parse instrument token
        if (orderJson.contains("instrument_token")) {
            order.instrumentToken = orderJson["instrument_token"].get<uint64_t>();
        }
        
        // Parse transaction type
        if (orderJson.contains("transaction_type")) {
            order.transactionType = OrderModel::stringToTransactionType(
                orderJson["transaction_type"].get<std::string>());
        }
        
        // Parse order type
        if (orderJson.contains("order_type")) {
            order.orderType = OrderModel::stringToOrderType(
                orderJson["order_type"].get<std::string>());
        }
        
        // Parse product type
        if (orderJson.contains("product")) {
            order.productType = OrderModel::stringToProductType(
                orderJson["product"].get<std::string>());
        }
        
        // Parse variety
        if (orderJson.contains("variety")) {
            order.variety = OrderModel::stringToVariety(
                orderJson["variety"].get<std::string>());
        }
        
        // Parse validity
        if (orderJson.contains("validity")) {
            order.validity = OrderModel::stringToValidity(
                orderJson["validity"].get<std::string>());
        }
        
        // Parse quantity
        if (orderJson.contains("quantity")) {
            order.quantity = orderJson["quantity"].get<uint64_t>();
        }
        
        // Parse disclosed quantity
        if (orderJson.contains("disclosed_quantity")) {
            order.disclosedQuantity = orderJson["disclosed_quantity"].get<uint64_t>();
        }
        
        // Parse filled quantity
        if (orderJson.contains("filled_quantity")) {
            order.filledQuantity = orderJson["filled_quantity"].get<uint64_t>();
        }
        
        // Parse pending quantity
        if (orderJson.contains("pending_quantity")) {
            order.pendingQuantity = orderJson["pending_quantity"].get<uint64_t>();
        }
        
        // Parse cancelled quantity
        if (orderJson.contains("cancelled_quantity")) {
            order.cancelledQuantity = orderJson["cancelled_quantity"].get<uint64_t>();
        }
        
        // Parse price
        if (orderJson.contains("price")) {
            order.price = orderJson["price"].get<double>();
        }
        
        // Parse trigger price
        if (orderJson.contains("trigger_price")) {
            order.triggerPrice = orderJson["trigger_price"].get<double>();
        }
        
        // Parse average price
        if (orderJson.contains("average_price")) {
            order.averagePrice = orderJson["average_price"].get<double>();
        }
        
        // Parse status
        if (orderJson.contains("status")) {
            order.status = OrderModel::stringToOrderStatus(
                orderJson["status"].get<std::string>());
        }
        
        // Parse status message
        if (orderJson.contains("status_message") && !orderJson["status_message"].is_null()) {
            order.statusMessage = orderJson["status_message"].get<std::string>();
        }
        
        // Parse order time
        if (orderJson.contains("order_timestamp") && !orderJson["order_timestamp"].is_null()) {
            order.orderTime = OrderModel::parseDateTime(
                orderJson["order_timestamp"].get<std::string>());
        }
        
        // Parse exchange update time
        if (orderJson.contains("exchange_update_timestamp") && !orderJson["exchange_update_timestamp"].is_null()) {
            order.exchangeUpdateTime = OrderModel::parseDateTime(
                orderJson["exchange_update_timestamp"].get<std::string>());
        }
        
        // Parse tag
        if (orderJson.contains("tag") && !orderJson["tag"].is_null()) {
            order.tag = orderJson["tag"].get<std::string>();
        }
    } catch (const std::exception& e) {
        m_logger->error("Exception while parsing order JSON: {}", e.what());
    }
    
    return order;
}

std::string OrderManager::buildOrderRequestBody(const OrderModel& order) {
    std::stringstream ss;
    
    ss << "tradingsymbol=" << order.tradingSymbol;
    ss << "&exchange=" << order.exchange;
    ss << "&transaction_type=" << OrderModel::transactionTypeToString(order.transactionType);
    ss << "&order_type=" << OrderModel::orderTypeToString(order.orderType);
    ss << "&quantity=" << order.quantity;
    ss << "&product=" << OrderModel::productTypeToString(order.productType);
    ss << "&validity=" << OrderModel::validityToString(order.validity);
    
    if (order.orderType == OrderType::LIMIT || order.orderType == OrderType::STOP_LOSS) {
        ss << "&price=" << order.price;
    }
    
    if (order.orderType == OrderType::STOP_LOSS || order.orderType == OrderType::STOP_LOSS_MARKET) {
        ss << "&trigger_price=" << order.triggerPrice;
    }
    
    if (order.disclosedQuantity > 0) {
        ss << "&disclosed_quantity=" << order.disclosedQuantity;
    }
    
    if (!order.tag.empty()) {
        ss << "&tag=" << order.tag;
    }
    
    return ss.str();
}

void OrderManager::updateOrderCache(const OrderModel& order) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_orderCache[order.orderId] = order;
}

OrderModel OrderManager::getOrderFromCache(const std::string& orderId) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_orderCache.find(orderId);
    if (it != m_orderCache.end()) {
        return it->second;
    }
    return OrderModel();
}

}  // namespace BoxStrategy