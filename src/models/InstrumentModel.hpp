 /**
 * @file InstrumentModel.hpp
 * @brief Model for financial instruments
 */

#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <vector>

namespace BoxStrategy {

/**
 * @enum InstrumentType
 * @brief Types of financial instruments
 */
enum class InstrumentType {
    UNKNOWN,
    INDEX,
    EQUITY,
    FUTURE,
    OPTION,
    CURRENCY,
    COMMODITY
};

/**
 * @enum OptionType
 * @brief Types of options
 */
enum class OptionType {
    UNKNOWN,
    CALL,
    PUT
};

/**
 * @struct InstrumentModel
 * @brief Model for a financial instrument
 */
struct InstrumentModel {
    uint64_t instrumentToken;            ///< Unique identifier for the instrument
    std::string tradingSymbol;           ///< Trading symbol of the instrument
    std::string exchange;                ///< Exchange where the instrument is traded
    std::string exchangeToken;           ///< Exchange token of the instrument
    std::string name;                    ///< Name of the instrument
    InstrumentType type;                 ///< Type of the instrument
    std::string segment;                 ///< Segment of the instrument
    
    // Option-specific fields
    std::string underlying;              ///< Underlying instrument for options
    double strikePrice;                  ///< Strike price for options
    OptionType optionType;               ///< Type of option (call/put)
    std::chrono::system_clock::time_point expiry;  ///< Expiry date for options/futures
    
    // Market data
    double lastPrice;                    ///< Last traded price
    double openPrice;                    ///< Opening price
    double highPrice;                    ///< High price
    double lowPrice;                     ///< Low price
    double closePrice;                   ///< Closing price
    double averagePrice;                 ///< Average price
    uint64_t volume;                     ///< Traded volume
    uint64_t buyQuantity;                ///< Buy quantity
    uint64_t sellQuantity;               ///< Sell quantity
    double openInterest;                 ///< Open interest
    
    // Depth data
    struct DepthItem {
        double price;                    ///< Price level
        uint64_t quantity;               ///< Quantity at this price level
        uint32_t orders;                 ///< Number of orders at this price level
    };
    
    std::vector<DepthItem> buyDepth;     ///< Market depth for buy side
    std::vector<DepthItem> sellDepth;    ///< Market depth for sell side
    
    /**
     * @brief Default constructor
     */
    InstrumentModel();
    
    /**
     * @brief Convert instrument type to string
     * @param type Instrument type
     * @return String representation of the instrument type
     */
    static std::string instrumentTypeToString(InstrumentType type);
    
    /**
     * @brief Convert string to instrument type
     * @param typeStr String representation of the instrument type
     * @return Instrument type
     */
    static InstrumentType stringToInstrumentType(const std::string& typeStr);
    
    /**
     * @brief Convert option type to string
     * @param type Option type
     * @return String representation of the option type
     */
    static std::string optionTypeToString(OptionType type);
    
    /**
     * @brief Convert string to option type
     * @param typeStr String representation of the option type
     * @return Option type
     */
    static OptionType stringToOptionType(const std::string& typeStr);
    
    /**
     * @brief Parse date string to system_clock time point
     * @param dateStr Date string in format YYYY-MM-DD
     * @return Parsed time point
     */
    static std::chrono::system_clock::time_point parseDate(const std::string& dateStr);
    
    /**
     * @brief Format time point to date string
     * @param tp Time point to format
     * @return Formatted date string in format YYYY-MM-DD
     */
    static std::string formatDate(const std::chrono::system_clock::time_point& tp);
};

}  // namespace BoxStrategy