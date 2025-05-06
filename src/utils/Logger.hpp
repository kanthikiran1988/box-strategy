 /**
 * @file Logger.hpp
 * @brief A thread-safe logging system for the application
 */

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <iostream>
#include <fmt/format.h>

namespace BoxStrategy {

/**
 * @enum LogLevel
 * @brief Defines the severity levels for logging
 */
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

/**
 * @class Logger
 * @brief Thread-safe logging utility for the application
 */
class Logger {
public:
    /**
     * @brief Constructor
     * @param logFile Path to the log file
     * @param consoleOutput Whether to output to console as well
     * @param minLevel Minimum log level to record
     */
    Logger(const std::string& logFile, bool consoleOutput = true, LogLevel minLevel = LogLevel::INFO);
    
    /**
     * @brief Destructor
     */
    ~Logger();
    
    /**
     * @brief Log a message with trace level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void trace(const std::string& fmt, Args&&... args) {
        log(LogLevel::TRACE, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a message with debug level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void debug(const std::string& fmt, Args&&... args) {
        log(LogLevel::DEBUG, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a message with info level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        log(LogLevel::INFO, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a message with warning level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void warn(const std::string& fmt, Args&&... args) {
        log(LogLevel::WARN, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a message with error level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        log(LogLevel::ERROR, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Log a message with fatal level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void fatal(const std::string& fmt, Args&&... args) {
        log(LogLevel::FATAL, fmt, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Set the minimum log level
     * @param level New minimum log level
     */
    void setLevel(LogLevel level);
    
    /**
     * @brief Get the current minimum log level
     * @return Current minimum log level
     */
    LogLevel getLevel() const;
    
    /**
     * @brief Enable or disable console output
     * @param enable Whether to enable console output
     */
    void enableConsoleOutput(bool enable);
    
    /**
     * @brief Flush the log file
     */
    void flush();

private:
    /**
     * @brief Log a message with the specified level
     * @param level Log level
     * @param fmt Format string
     * @param args Format arguments
     */
    template<typename... Args>
    void log(LogLevel level, const std::string& fmt, Args&&... args) {
        if (level < m_minLevel) {
            return;
        }
        
        std::string message;
        try {
            message = fmt::format(fmt, std::forward<Args>(args)...);
        } catch (const std::exception& e) {
            message = fmt + " (Error formatting message: " + e.what() + ")";
        }
        
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto tm = std::localtime(&timeT);
        
        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << " ";
        ss << "[" << levelToString(level) << "] ";
        ss << message;
        
        std::string logLine = ss.str();
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logFile << logLine << std::endl;
            
            if (m_consoleOutput) {
                if (level >= LogLevel::ERROR) {
                    std::cerr << logLine << std::endl;
                } else {
                    std::cout << logLine << std::endl;
                }
            }
        }
    }
    
    /**
     * @brief Convert log level to string
     * @param level Log level
     * @return String representation of the log level
     */
    static std::string levelToString(LogLevel level);
    
    std::ofstream m_logFile;    ///< Log file stream
    bool m_consoleOutput;       ///< Whether to output to console
    LogLevel m_minLevel;        ///< Minimum log level to record
    std::mutex m_mutex;         ///< Mutex for thread safety
};

}  // namespace BoxStrategy