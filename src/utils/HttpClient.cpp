/**
 * @file HttpClient.cpp
 * @brief Implementation of the HttpClient class
 */

#include "../utils/HttpClient.hpp"
#include <curl/curl.h>
#include <thread>

namespace BoxStrategy {

HttpClient::HttpClient(std::shared_ptr<Logger> logger)
    : m_logger(logger), m_connectionTimeout(10000), m_requestTimeout(30000), m_initialized(false) {
    init();
}

HttpClient::~HttpClient() {
    cleanup();
}

void HttpClient::init() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        m_initialized = true;
        m_logger->info("HttpClient initialized");
    }
}

void HttpClient::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        curl_global_cleanup();
        m_initialized = false;
        m_logger->info("HttpClient cleaned up");
    }
}

HttpResponse HttpClient::request(HttpMethod method, const std::string& url,
                               const std::unordered_map<std::string, std::string>& headers,
                               const std::string& body) {
    m_logger->debug("Making {} request to {}", methodToString(method), url);
    
    HttpResponse response;
    response.statusCode = 0;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_logger->error("Failed to initialize CURL");
        return response;
    }
    
    setCurlOptions(curl, method, url, headers, body, response.headers, response.body);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        m_logger->error("CURL request failed: {} - {}", static_cast<int>(res), curl_easy_strerror(res));
    } else {
        long statusCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
        response.statusCode = static_cast<int>(statusCode);
        m_logger->debug("Request completed with status code {}", response.statusCode);
    }
    
    curl_easy_cleanup(curl);
    return response;
}

std::future<HttpResponse> HttpClient::requestAsync(HttpMethod method, const std::string& url,
                                                const std::unordered_map<std::string, std::string>& headers,
                                                const std::string& body) {
    return std::async(std::launch::async, [this, method, url, headers, body]() {
        return request(method, url, headers, body);
    });
}

void HttpClient::setConnectionTimeout(long timeoutMs) {
    m_connectionTimeout = timeoutMs;
}

void HttpClient::setRequestTimeout(long timeoutMs) {
    m_requestTimeout = timeoutMs;
}

void HttpClient::setCurlOptions(CURL* curl, HttpMethod method, const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers,
                              const std::string& body,
                              std::unordered_map<std::string, std::string>& responseHeaders,
                              std::string& responseBody) {
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set timeout options
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, m_connectionTimeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_requestTimeout);
    
    // Set up response callbacks
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HttpClient::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HttpClient::headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Prepare headers
    struct curl_slist* headerList = nullptr;
    for (const auto& header : headers) {
        std::string headerString = header.first + ": " + header.second;
        headerList = curl_slist_append(headerList, headerString.c_str());
    }
    
    // Set method and body
    switch (method) {
        case HttpMethod::GET:
            break;
        case HttpMethod::POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
        case HttpMethod::PUT:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
        case HttpMethod::DELETE:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
    }
    
    // Set headers
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Set verbose for debug logging
    #ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    #endif
}

std::string HttpClient::methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:    return "GET";
        case HttpMethod::POST:   return "POST";
        case HttpMethod::PUT:    return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        default:                 return "UNKNOWN";
    }
}

size_t HttpClient::writeCallback(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    std::string* responseBody = static_cast<std::string*>(userp);
    responseBody->append(static_cast<char*>(data), realSize);
    return realSize;
}

size_t HttpClient::headerCallback(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    std::string header(static_cast<char*>(data), realSize);
    
    // Find the end of header name (the colon)
    size_t colonPos = header.find(':');
    if (colonPos != std::string::npos) {
        // Extract header name and value
        std::string name = header.substr(0, colonPos);
        // Skip the colon and any leading whitespace
        size_t valueStart = header.find_first_not_of(" \t", colonPos + 1);
        if (valueStart != std::string::npos) {
            std::string value = header.substr(valueStart);
            // Remove trailing newlines
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
                value.pop_back();
            }
            
            // Store the header
            auto& headers = *static_cast<std::unordered_map<std::string, std::string>*>(userp);
            headers[name] = value;
        }
    }
    
    return realSize;
}

}  // namespace BoxStrategy