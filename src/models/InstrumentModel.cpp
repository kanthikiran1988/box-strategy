/**
 * @file InstrumentModel.cpp
 * @brief Implementation of the InstrumentModel
 */

#include "../models/InstrumentModel.hpp"
#include <sstream>
#include <iomanip>

namespace BoxStrategy {

InstrumentModel::InstrumentModel()
    : instrumentToken(0),
      strikePrice(0.0),
      optionType(OptionType::UNKNOWN),
      type(InstrumentType::UNKNOWN),
      lastPrice(0.0),
      openPrice(0.0),
      highPrice(0.0),
      lowPrice(0.0),
      closePrice(0.0),
      averagePrice(0.0),
      volume(0),
      buyQuantity(0),
      sellQuantity(0),
      openInterest(0.0) {
}

std::string InstrumentModel::instrumentTypeToString(InstrumentType type) {
    switch (type) {
        case InstrumentType::INDEX:     return "INDEX";
        case InstrumentType::EQUITY:    return "EQUITY";
        case InstrumentType::FUTURE:    return "FUTURE";
        case InstrumentType::OPTION:    return "OPTION";
        case InstrumentType::CURRENCY:  return "CURRENCY";
        case InstrumentType::COMMODITY: return "COMMODITY";
        default:                        return "UNKNOWN";
    }
}

InstrumentType InstrumentModel::stringToInstrumentType(const std::string& typeStr) {
    // Handle Zerodha Kite API instrument type formats
    if (typeStr == "INDEX" || typeStr == "INDICES")                 return InstrumentType::INDEX;
    if (typeStr == "EQUITY" || typeStr == "EQ")                     return InstrumentType::EQUITY;
    if (typeStr == "FUTURE" || typeStr == "FUT")                    return InstrumentType::FUTURE;
    if (typeStr == "OPTION" || typeStr == "OPT" || 
        typeStr == "CE" || typeStr == "PE")                         return InstrumentType::OPTION;
    if (typeStr == "CURRENCY")                                      return InstrumentType::CURRENCY;
    if (typeStr == "COMMODITY")                                     return InstrumentType::COMMODITY;
    
    // Properly handle the case when options are classified as "OTHER" or empty
    if (typeStr == "OTHER" || typeStr.empty()) {
        // This is a fallback - we'll need to check trading symbol in MarketDataManager
        return InstrumentType::UNKNOWN;
    }
    
    return InstrumentType::UNKNOWN;
}

std::string InstrumentModel::optionTypeToString(OptionType type) {
    switch (type) {
        case OptionType::CALL: return "CE";
        case OptionType::PUT:  return "PE";
        default:               return "XX";
    }
}

OptionType InstrumentModel::stringToOptionType(const std::string& typeStr) {
    if (typeStr == "CE" || typeStr == "CALL") return OptionType::CALL;
    if (typeStr == "PE" || typeStr == "PUT")  return OptionType::PUT;
    return OptionType::UNKNOWN;
}

std::chrono::system_clock::time_point InstrumentModel::parseDate(const std::string& dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    
    std::time_t time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
}

std::string InstrumentModel::formatDate(const std::chrono::system_clock::time_point& tp) {
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm* tm = std::localtime(&time);
    
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d");
    return ss.str();
}

}  // namespace BoxStrategy