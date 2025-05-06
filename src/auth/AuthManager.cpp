 /**
 * @file AuthManager.cpp
 * @brief Implementation of the AuthManager class
 */

#include "../auth/AuthManager.hpp"
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>
#include "../external/json.hpp"

using json = nlohmann::json;

namespace BoxStrategy {

AuthManager::AuthManager(
    std::shared_ptr<ConfigManager> configManager,
    std::shared_ptr<HttpClient> httpClient,
    std::shared_ptr<Logger> logger
) : m_configManager(configManager),
    m_httpClient(httpClient),
    m_logger(logger) {
    
    m_logger->info("Initializing AuthManager");
    
    // Load API credentials from configuration
    m_apiKey = m_configManager->getStringValue("api/key");
    m_apiSecret = m_configManager->getStringValue("api/secret");
    
    if (m_apiKey.empty() || m_apiSecret.empty()) {
        m_logger->error("API key or secret not found in configuration");
    } else {
        m_logger->info("API credentials loaded from configuration");
        
        // Try to load existing authentication details
        loadAuthDetails();
    }
}

std::string AuthManager::generateLoginUrl() const {
    std::string baseUrl = "https://kite.zerodha.com/connect/login";
    std::string apiKeyParam = "?api_key=" + m_apiKey;
    std::string versionParam = "&v=3";
    
    std::string loginUrl = baseUrl + apiKeyParam + versionParam;
    m_logger->info("Generated login URL: {}", loginUrl);
    
    return loginUrl;
}

bool AuthManager::generateAccessToken(const std::string& requestToken) {
    if (m_apiKey.empty() || m_apiSecret.empty()) {
        m_logger->error("API key or secret not set");
        return false;
    }
    
    m_logger->info("Generating access token with request token: {}", requestToken);
    
    std::string checksum = generateChecksum(requestToken);
    m_logger->debug("Generated checksum: {}", checksum);
    
    std::unordered_map<std::string, std::string> headers = {
        {"X-Kite-Version", "3"},
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    
    std::string requestBody = "api_key=" + m_apiKey + "&request_token=" + requestToken + "&checksum=" + checksum;
    
    HttpResponse response = m_httpClient->request(
        HttpMethod::POST,
        "https://api.kite.trade/session/token",
        headers,
        requestBody
    );
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                json data = responseJson["data"];
                
                std::lock_guard<std::mutex> lock(m_mutex);
                m_accessToken = data["access_token"].get<std::string>();
                
                // Set expiry to 24 hours from now (Zerodha token is valid for 24 hours)
                m_accessTokenExpiry = std::chrono::system_clock::now() + std::chrono::hours(24);
                
                m_logger->info("Access token generated successfully");
                
                // Save authentication details to configuration
                saveAuthDetails();
                
                return true;
            } else {
                m_logger->error("Failed to generate access token: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to generate access token. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return false;
}

bool AuthManager::isAccessTokenValid() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_accessToken.empty()) {
        return false;
    }
    
    // Check if token has expired
    auto now = std::chrono::system_clock::now();
    if (now >= m_accessTokenExpiry) {
        return false;
    }
    
    // To be sure, we could make a simple API call to validate the token
    // but we'll skip that for now since it might be too heavy for frequent checks
    
    return true;
}

bool AuthManager::invalidateAccessToken() {
    if (m_accessToken.empty()) {
        m_logger->warn("No access token to invalidate");
        return true;
    }
    
    m_logger->info("Invalidating access token");
    
    std::unordered_map<std::string, std::string> headers = {
        {"X-Kite-Version", "3"},
        {"Authorization", "token " + m_apiKey + ":" + m_accessToken}
    };
    
    HttpResponse response = m_httpClient->request(
        HttpMethod::DELETE,
        "https://api.kite.trade/session/token",
        headers
    );
    
    if (response.statusCode == 200) {
        try {
            json responseJson = json::parse(response.body);
            
            if (responseJson["status"] == "success") {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_accessToken.clear();
                m_accessTokenExpiry = std::chrono::system_clock::time_point();
                
                m_logger->info("Access token invalidated successfully");
                
                // Save authentication details to configuration
                saveAuthDetails();
                
                return true;
            } else {
                m_logger->error("Failed to invalidate access token: {}", responseJson["message"].get<std::string>());
            }
        } catch (const std::exception& e) {
            m_logger->error("Exception while parsing response: {}", e.what());
        }
    } else {
        m_logger->error("Failed to invalidate access token. Status code: {}, Response: {}", 
                      response.statusCode, response.body);
    }
    
    return false;
}

std::string AuthManager::getAccessToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_accessToken;
}

void AuthManager::setAccessToken(const std::string& accessToken, 
                            const std::chrono::system_clock::time_point& expiryTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_accessToken = accessToken;
    m_accessTokenExpiry = expiryTime;
    
    m_logger->info("Access token set manually");
    
    // Save authentication details to configuration
    saveAuthDetails();
}

std::string AuthManager::getApiKey() const {
    return m_apiKey;
}

std::string AuthManager::getApiSecret() const {
    return m_apiSecret;
}

std::string AuthManager::generateChecksum(const std::string& requestToken) const {
    // Generate checksum as SHA256(api_key + request_token + api_secret)
    std::string input = m_apiKey + requestToken + m_apiSecret;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, input.c_str(), input.length());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return ss.str();
}

bool AuthManager::saveAuthDetails() const {
    try {
        m_configManager->setStringValue("auth/access_token", m_accessToken);
        
        auto timeT = std::chrono::system_clock::to_time_t(m_accessTokenExpiry);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
        m_configManager->setStringValue("auth/expiry", ss.str());
        
        m_configManager->saveConfig();
        
        m_logger->debug("Authentication details saved to configuration");
        return true;
    } catch (const std::exception& e) {
        m_logger->error("Exception while saving authentication details: {}", e.what());
        return false;
    }
}

bool AuthManager::loadAuthDetails() {
    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_accessToken = m_configManager->getStringValue("auth/access_token");
        std::string expiryStr = m_configManager->getStringValue("auth/expiry");
        
        if (!m_accessToken.empty() && !expiryStr.empty()) {
            std::tm tm = {};
            std::istringstream ss(expiryStr);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            
            if (ss.fail()) {
                m_logger->error("Failed to parse expiry time: {}", expiryStr);
                return false;
            }
            
            std::time_t time = std::mktime(&tm);
            m_accessTokenExpiry = std::chrono::system_clock::from_time_t(time);
            
            m_logger->info("Authentication details loaded from configuration");
            return true;
        }
    } catch (const std::exception& e) {
        m_logger->error("Exception while loading authentication details: {}", e.what());
    }
    
    return false;
}

}  // namespace BoxStrategy