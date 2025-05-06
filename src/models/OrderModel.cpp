/**
 * @file OrderModel.cpp
 * @brief Implementation of the OrderModel
 */

#include "../models/OrderModel.hpp"
#include <sstream>
#include <iomanip>

namespace BoxStrategy {

OrderModel::OrderModel()
    : instrumentToken(0),
      transactionType(TransactionType::UNKNOWN),
      orderType(OrderType::UNKNOWN),
      productType(ProductType::UNKNOWN),
      variety(Variety::UNKNOWN),
      validity(Validity::UNKNOWN),
      quantity(0),
      disclosedQuantity(0),
      filledQuantity(0),
      pendingQuantity(0),
      cancelledQuantity(0),
      price(0.0),
      triggerPrice(0.0),
      averagePrice(0.0),
      status(OrderStatus::UNKNOWN) {
}

std::string OrderModel::orderTypeToString(OrderType type) {
    switch (type) {
        case OrderType::MARKET:            return "MARKET";
        case OrderType::LIMIT:             return "LIMIT";
        case OrderType::STOP_LOSS:         return "SL";
        case OrderType::STOP_LOSS_MARKET:  return "SL-M";
        default:                           return "UNKNOWN";
    }
}

OrderType OrderModel::stringToOrderType(const std::string& typeStr) {
    if (typeStr == "MARKET")   return OrderType::MARKET;
    if (typeStr == "LIMIT")    return OrderType::LIMIT;
    if (typeStr == "SL")       return OrderType::STOP_LOSS;
    if (typeStr == "SL-M")     return OrderType::STOP_LOSS_MARKET;
    return OrderType::UNKNOWN;
}

std::string OrderModel::transactionTypeToString(TransactionType type) {
    switch (type) {
        case TransactionType::BUY:  return "BUY";
        case TransactionType::SELL: return "SELL";
        default:                    return "UNKNOWN";
    }
}

TransactionType OrderModel::stringToTransactionType(const std::string& typeStr) {
    if (typeStr == "BUY")  return TransactionType::BUY;
    if (typeStr == "SELL") return TransactionType::SELL;
    return TransactionType::UNKNOWN;
}

std::string OrderModel::orderStatusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::OPEN:            return "OPEN";
        case OrderStatus::PENDING:         return "PENDING";
        case OrderStatus::COMPLETE:        return "COMPLETE";
        case OrderStatus::REJECTED:        return "REJECTED";
        case OrderStatus::CANCELLED:       return "CANCELLED";
        case OrderStatus::TRIGGER_PENDING: return "TRIGGER PENDING";
        default:                           return "UNKNOWN";
    }
}

OrderStatus OrderModel::stringToOrderStatus(const std::string& statusStr) {
    if (statusStr == "OPEN")             return OrderStatus::OPEN;
    if (statusStr == "PENDING")          return OrderStatus::PENDING;
    if (statusStr == "COMPLETE")         return OrderStatus::COMPLETE;
    if (statusStr == "REJECTED")         return OrderStatus::REJECTED;
    if (statusStr == "CANCELLED")        return OrderStatus::CANCELLED;
    if (statusStr == "TRIGGER PENDING")  return OrderStatus::TRIGGER_PENDING;
    return OrderStatus::UNKNOWN;
}

std::string OrderModel::productTypeToString(ProductType type) {
    switch (type) {
        case ProductType::CNC:  return "CNC";
        case ProductType::NRML: return "NRML";
        case ProductType::MIS:  return "MIS";
        case ProductType::CO:   return "CO";
        case ProductType::BO:   return "BO";
        default:                return "UNKNOWN";
    }
}

ProductType OrderModel::stringToProductType(const std::string& typeStr) {
    if (typeStr == "CNC")  return ProductType::CNC;
    if (typeStr == "NRML") return ProductType::NRML;
    if (typeStr == "MIS")  return ProductType::MIS;
    if (typeStr == "CO")   return ProductType::CO;
    if (typeStr == "BO")   return ProductType::BO;
    return ProductType::UNKNOWN;
}

std::string OrderModel::varietyToString(Variety variety) {
    switch (variety) {
        case Variety::REGULAR: return "regular";
        case Variety::AMO:     return "amo";
        case Variety::CO:      return "co";
        case Variety::BO:      return "bo";
        default:               return "unknown";
    }
}

Variety OrderModel::stringToVariety(const std::string& varietyStr) {
    if (varietyStr == "regular") return Variety::REGULAR;
    if (varietyStr == "amo")     return Variety::AMO;
    if (varietyStr == "co")      return Variety::CO;
    if (varietyStr == "bo")      return Variety::BO;
    return Variety::UNKNOWN;
}

std::string OrderModel::validityToString(Validity validity) {
    switch (validity) {
        case Validity::DAY: return "DAY";
        case Validity::IOC: return "IOC";
        default:            return "UNKNOWN";
    }
}

Validity OrderModel::stringToValidity(const std::string& validityStr) {
    if (validityStr == "DAY") return Validity::DAY;
    if (validityStr == "IOC") return Validity::IOC;
    return Validity::UNKNOWN;
}

std::chrono::system_clock::time_point OrderModel::parseDateTime(const std::string& dateTimeStr) {
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    std::time_t time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
}

std::string OrderModel::formatDateTime(const std::chrono::system_clock::time_point& tp) {
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm* tm = std::localtime(&time);
    
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

}  // namespace BoxStrategy