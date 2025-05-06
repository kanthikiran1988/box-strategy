 /**
 * @file HttpClient.hpp
 * @brief HTTP client for making requests to the Zerodha Kite Connect API
 */

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <future>
#include <curl/curl.h>
#include "../utils/Logger.hpp"

namespace BoxStrategy {

/**
 * @enum HttpMethod
 * @brief HTTP request methods
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE
};

/**
 * @struct HttpResponse
 * @brief HTTP response data
 */
struct HttpResponse {
    int statusCode;                              ///< HTTP status code
    std::string body;                            ///< Response body
    std::unordered_map<std::string, std::string> headers;  ///< Response headers
};

/**
 * @class HttpClient
 * @brief Thread-safe HTTP client for making API requests
 */
class HttpClient {
public:
    /**
     * @brief Constructor
     * @param logger Logger instance
     */
    explicit HttpClient(std::shared_ptr<Logger> logger);
    
    /**
     * @brief Destructor
     */
    ~HttpClient();
    
    /**
     * @brief Perform a synchronous HTTP request
     * @param method HTTP method
     * @param url URL to request
     * @param headers HTTP headers
     * @param body Request body
     * @return HTTP response
     */
    HttpResponse request(HttpMethod method, const std::string& url,
                        const std::unordered_map<std::string, std::string>& headers = {},
                        const std::string& body = "");
    
    /**
     * @brief Perform an asynchronous HTTP request
     * @param method HTTP method
     * @param url URL to request
     * @param headers HTTP headers
     * @param body Request body
     * @return Future HTTP response
     */
    std::future<HttpResponse> requestAsync(HttpMethod method, const std::string& url,
                                          const std::unordered_map<std::string, std::string>& headers = {},
                                          const std::string& body = "");
    
    /**
     * @brief Set connection timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setConnectionTimeout(long timeoutMs);
    
    /**
     * @brief Set request timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setRequestTimeout(long timeoutMs);
    
private:
    /**
     * @brief Initialize libcurl
     */
    void init();
    
    /**
     * @brief Cleanup libcurl
     */
    void cleanup();
    
    /**
     * @brief Set common curl options
     * @param curl CURL handle
     * @param method HTTP method
     * @param url URL to request
     * @param headers HTTP headers
     * @param body Request body
     * @param responseHeaders Response headers
     * @param responseBody Response body
     */
    void setCurlOptions(CURL* curl, HttpMethod method, const std::string& url,
                        const std::unordered_map<std::string, std::string>& headers,
                        const std::string& body,
                        std::unordered_map<std::string, std::string>& responseHeaders,
                        std::string& responseBody);
    
    /**
     * @brief Convert HTTP method to string
     * @param method HTTP method
     * @return String representation of the HTTP method
     */
    static std::string methodToString(HttpMethod method);
    
    /**
     * @brief CURL write callback
     * @param data Data received
     * @param size Size of each data element
     * @param nmemb Number of data elements
     * @param userp User data pointer
     * @return Number of bytes handled
     */
    static size_t writeCallback(void* data, size_t size, size_t nmemb, void* userp);
    
    /**
     * @brief CURL header callback
     * @param data Header data received
     * @param size Size of each data element
     * @param nmemb Number of data elements
     * @param userp User data pointer
     * @return Number of bytes handled
     */
    static size_t headerCallback(void* data, size_t size, size_t nmemb, void* userp);
    
    std::shared_ptr<Logger> m_logger;  ///< Logger instance
    long m_connectionTimeout;          ///< Connection timeout in milliseconds
    long m_requestTimeout;             ///< Request timeout in milliseconds
    bool m_initialized;                ///< Whether libcurl is initialized
    std::mutex m_mutex;                ///< Mutex for thread safety
};

}  // namespace BoxStrategy