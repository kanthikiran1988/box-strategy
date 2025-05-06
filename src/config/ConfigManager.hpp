 /**
 * @file ConfigManager.hpp
 * @brief Handles all configuration parameters for the box strategy
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include "../utils/Logger.hpp"
#include "../external/json_wrapper.hpp"

// Use json from our namespace
using json = BoxStrategy::json;

namespace BoxStrategy {

/**
 * @class ConfigManager
 * @brief Manages all configuration parameters for the box strategy
 */
class ConfigManager {
public:
    /**
     * @brief Constructor
     * @param configFilePath Path to the configuration file
     * @param logger Logger instance
     */
    ConfigManager(const std::string& configFilePath, std::shared_ptr<Logger> logger);
    
    /**
     * @brief Destructor
     */
    ~ConfigManager() = default;
    
    /**
     * @brief Load configuration from file
     * @return true if successful, false otherwise
     */
    bool loadConfig();
    
    /**
     * @brief Save configuration to file
     * @return true if successful, false otherwise
     */
    bool saveConfig();
    
    /**
     * @brief Get a string value from the configuration
     * @param key Configuration key
     * @param defaultValue Default value if key is not found
     * @return The configuration value
     */
    std::string getStringValue(const std::string& key, const std::string& defaultValue = "") const;
    
    /**
     * @brief Get an integer value from the configuration
     * @param key Configuration key
     * @param defaultValue Default value if key is not found
     * @return The configuration value
     */
    int getIntValue(const std::string& key, int defaultValue = 0) const;
    
    /**
     * @brief Get a double value from the configuration
     * @param key Configuration key
     * @param defaultValue Default value if key is not found
     * @return The configuration value
     */
    double getDoubleValue(const std::string& key, double defaultValue = 0.0) const;
    
    /**
     * @brief Get a boolean value from the configuration
     * @param key Configuration key
     * @param defaultValue Default value if key is not found
     * @return The configuration value
     */
    bool getBoolValue(const std::string& key, bool defaultValue = false) const;
    
    /**
     * @brief Get a vector of strings from the configuration
     * @param key Configuration key
     * @return The configuration value
     */
    std::vector<std::string> getStringArray(const std::string& key) const;
    
    /**
     * @brief Get a vector of integers from the configuration
     * @param key Configuration key
     * @return The configuration value
     */
    std::vector<int> getIntArray(const std::string& key) const;
    
    /**
     * @brief Get a vector of doubles from the configuration
     * @param key Configuration key
     * @return The configuration value
     */
    std::vector<double> getDoubleArray(const std::string& key) const;
    
    /**
     * @brief Set a string value in the configuration
     * @param key Configuration key
     * @param value Value to set
     */
    void setStringValue(const std::string& key, const std::string& value);
    
    /**
     * @brief Set an integer value in the configuration
     * @param key Configuration key
     * @param value Value to set
     */
    void setIntValue(const std::string& key, int value);
    
    /**
     * @brief Set a double value in the configuration
     * @param key Configuration key
     * @param value Value to set
     */
    void setDoubleValue(const std::string& key, double value);
    
    /**
     * @brief Set a boolean value in the configuration
     * @param key Configuration key
     * @param value Value to set
     */
    void setBoolValue(const std::string& key, bool value);
    
    /**
     * @brief Set a vector of strings in the configuration
     * @param key Configuration key
     * @param values Values to set
     */
    void setStringArray(const std::string& key, const std::vector<std::string>& values);
    
    /**
     * @brief Set a vector of integers in the configuration
     * @param key Configuration key
     * @param values Values to set
     */
    void setIntArray(const std::string& key, const std::vector<int>& values);
    
    /**
     * @brief Set a vector of doubles in the configuration
     * @param key Configuration key
     * @param values Values to set
     */
    void setDoubleArray(const std::string& key, const std::vector<double>& values);
    
    /**
     * @brief Get config section as JSON
     * @param sectionKey Section key
     * @return JSON object for the section
     */
    json getSection(const std::string& sectionKey) const;

private:
    std::string m_configFilePath;    ///< Path to the configuration file
    json m_config;                   ///< Configuration data
    std::shared_ptr<Logger> m_logger; ///< Logger instance
};

}  // namespace BoxStrategy