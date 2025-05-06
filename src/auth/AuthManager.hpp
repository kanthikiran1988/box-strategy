 /**
 * @file AuthManager.hpp
 * @brief Manages authentication with the Zerodha Kite Connect API
 */

#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include "../utils/Logger.hpp"
#include "../utils/HttpClient.hpp"
#include "../config/ConfigManager.hpp"

namespace BoxStrategy {

/**
 * @class AuthManager
 * @brief Manages authentication with the Zerodha Kite Connect API
 */
class AuthManager {
public:
    /**
     * @brief Constructor
     * @param configManager Configuration manager
     * @param httpClient HTTP client
     * @param logger Logger instance
     */
    AuthManager(
        std::shared_ptr<ConfigManager> configManager,
        std::shared_ptr<HttpClient> httpClient,
        std::shared_ptr<Logger> logger
    );
    
    /**
     * @brief Destructor
     */
    ~AuthManager() = default;
    
    /**
     * @brief Generate login URL for the user to authenticate
     * @return Login URL
     */
    std::string generateLoginUrl() const;
    
    /**
     * @brief Generate access token using request token
     * @param requestToken Request token received after user authentication
     * @return true if successful, false otherwise
     */
    bool generateAccessToken(const std::string& requestToken);
    
    /**
     * @brief Check if current access token is valid
     * @return true if valid, false otherwise
     */
    bool isAccessTokenValid() const;
    
    /**
     * @brief Invalidate current access token
     * @return true if successful, false otherwise
     */
    bool invalidateAccessToken();
    
    /**
     * @brief Get the current access token
     * @return Current access token
     */
    std::string getAccessToken() const;
    
    /**
     * @brief Set the access token manually
     * @param accessToken Access token
     * @param expiryTime Expiry time of the access token
     */
    void setAccessToken(const std::string& accessToken, 
                       const std::chrono::system_clock::time_point& expiryTime);
    
    /**
     * @brief Get the API key
     * @return API key
     */
    std::string getApiKey() const;
    
    /**
     * @brief Get the API secret
     * @return API secret
     */
    std::string getApiSecret() const;

private:
    /**
     * @brief Generate checksum for API requests
     * @param requestToken Request token
     * @return Checksum
     */
    std::string generateChecksum(const std::string& requestToken) const;
    
    /**
     * @brief Save authentication details to configuration
     * @return true if successful, false otherwise
     */
    bool saveAuthDetails() const;
    
    /**
     * @brief Load authentication details from configuration
     * @return true if successful, false otherwise
     */
    bool loadAuthDetails();
    
    std::shared_ptr<ConfigManager> m_configManager;  ///< Configuration manager
    std::shared_ptr<HttpClient> m_httpClient;        ///< HTTP client
    std::shared_ptr<Logger> m_logger;                ///< Logger instance
    
    std::string m_apiKey;                            ///< API key
    std::string m_apiSecret;                         ///< API secret
    std::string m_accessToken;                       ///< Access token
    std::chrono::system_clock::time_point m_accessTokenExpiry;  ///< Access token expiry time
    
    mutable std::mutex m_mutex;                      ///< Mutex for thread safety
};

}  // namespace BoxStrategy