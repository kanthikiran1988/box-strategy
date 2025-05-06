/**
 * @file ConfigManager.cpp
 * @brief Implementation of the ConfigManager class
 */

#include "../config/ConfigManager.hpp"
#include <iostream>
#include <fstream>

namespace BoxStrategy {

ConfigManager::ConfigManager(const std::string& configFilePath, std::shared_ptr<Logger> logger)
    : m_configFilePath(configFilePath), m_logger(logger) {
    m_logger->info("ConfigManager initialized with config file: {}", configFilePath);
}

bool ConfigManager::loadConfig() {
    try {
        std::ifstream configFile(m_configFilePath);
        if (!configFile.is_open()) {
            m_logger->error("Failed to open configuration file: {}", m_configFilePath);
            return false;
        }
        
        configFile >> m_config;
        configFile.close();
        
        m_logger->info("Configuration loaded successfully from {}", m_configFilePath);
        return true;
    } catch (const std::exception& e) {
        m_logger->error("Exception while loading configuration: {}", e.what());
        return false;
    }
}

bool ConfigManager::saveConfig() {
    try {
        std::ofstream configFile(m_configFilePath);
        if (!configFile.is_open()) {
            m_logger->error("Failed to open configuration file for writing: {}", m_configFilePath);
            return false;
        }
        
        configFile << m_config.dump(4);  // Pretty print with 4 spaces indentation
        configFile.close();
        
        m_logger->info("Configuration saved successfully to {}", m_configFilePath);
        return true;
    } catch (const std::exception& e) {
        m_logger->error("Exception while saving configuration: {}", e.what());
        return false;
    }
}

std::string ConfigManager::getStringValue(const std::string& key, const std::string& defaultValue) const {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path)) {
            return m_config.at(path).get<std::string>();
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting string value for key {}: {}", key, e.what());
    }
    return defaultValue;
}

int ConfigManager::getIntValue(const std::string& key, int defaultValue) const {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path)) {
            return m_config.at(path).get<int>();
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting int value for key {}: {}", key, e.what());
    }
    return defaultValue;
}

double ConfigManager::getDoubleValue(const std::string& key, double defaultValue) const {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path)) {
            return m_config.at(path).get<double>();
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting double value for key {}: {}", key, e.what());
    }
    return defaultValue;
}

bool ConfigManager::getBoolValue(const std::string& key, bool defaultValue) const {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path)) {
            return m_config.at(path).get<bool>();
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting bool value for key {}: {}", key, e.what());
    }
    return defaultValue;
}

std::vector<std::string> ConfigManager::getStringArray(const std::string& key) const {
    std::vector<std::string> result;
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path) && m_config.at(path).is_array()) {
            for (const auto& item : m_config.at(path)) {
                result.push_back(item.get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting string array for key {}: {}", key, e.what());
    }
    return result;
}

std::vector<int> ConfigManager::getIntArray(const std::string& key) const {
    std::vector<int> result;
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path) && m_config.at(path).is_array()) {
            for (const auto& item : m_config.at(path)) {
                result.push_back(item.get<int>());
            }
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting int array for key {}: {}", key, e.what());
    }
    return result;
}

std::vector<double> ConfigManager::getDoubleArray(const std::string& key) const {
    std::vector<double> result;
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        if (m_config.contains(path) && m_config.at(path).is_array()) {
            for (const auto& item : m_config.at(path)) {
                result.push_back(item.get<double>());
            }
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting double array for key {}: {}", key, e.what());
    }
    return result;
}

void ConfigManager::setStringValue(const std::string& key, const std::string& value) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = value;
        m_logger->debug("Set string value for key {}: {}", key, value);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting string value for key {}: {}", key, e.what());
    }
}

void ConfigManager::setIntValue(const std::string& key, int value) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = value;
        m_logger->debug("Set int value for key {}: {}", key, value);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting int value for key {}: {}", key, e.what());
    }
}

void ConfigManager::setDoubleValue(const std::string& key, double value) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = value;
        m_logger->debug("Set double value for key {}: {}", key, value);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting double value for key {}: {}", key, e.what());
    }
}

void ConfigManager::setBoolValue(const std::string& key, bool value) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = value;
        m_logger->debug("Set bool value for key {}: {}", key, value);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting bool value for key {}: {}", key, e.what());
    }
}

void ConfigManager::setStringArray(const std::string& key, const std::vector<std::string>& values) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = values;
        m_logger->debug("Set string array for key {}", key);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting string array for key {}: {}", key, e.what());
    }
}

void ConfigManager::setIntArray(const std::string& key, const std::vector<int>& values) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = values;
        m_logger->debug("Set int array for key {}", key);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting int array for key {}: {}", key, e.what());
    }
}

void ConfigManager::setDoubleArray(const std::string& key, const std::vector<double>& values) {
    try {
        auto path = json::json_pointer(key.empty() ? "/" : "/" + key);
        m_config[path] = values;
        m_logger->debug("Set double array for key {}", key);
    } catch (const std::exception& e) {
        m_logger->error("Exception while setting double array for key {}: {}", key, e.what());
    }
}

json ConfigManager::getSection(const std::string& sectionKey) const {
    try {
        auto path = json::json_pointer(sectionKey.empty() ? "/" : "/" + sectionKey);
        if (m_config.contains(path)) {
            return m_config.at(path);
        }
    } catch (const std::exception& e) {
        m_logger->warn("Exception while getting section for key {}: {}", sectionKey, e.what());
    }
    return json::object();
}

}  // namespace BoxStrategy