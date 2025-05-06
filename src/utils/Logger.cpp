/**
 * @file Logger.cpp
 * @brief Implementation of the Logger class
 */

#include "../utils/Logger.hpp"
#include <iomanip>

namespace BoxStrategy {
    Logger::Logger(const std::string& logFile, bool consoleOutput, LogLevel minLevel)
        : m_consoleOutput(consoleOutput), m_minLevel(minLevel) {
        m_logFile.open(logFile, std::ios::app);
        if (!m_logFile.is_open()) {
            std::cerr << "Failed to open log file: " << logFile << std::endl;
        }

        // Log start of session
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto tm = std::localtime(&timeT);

        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << " ";
        ss << "[INFO] Logger initialized. Session started.";

        m_logFile << ss.str() << std::endl;

        if (m_consoleOutput) {
            std::cout << ss.str() << std::endl;
        }
    }

    Logger::~Logger() {
        // Log end of session
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto tm = std::localtime(&timeT);

        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << " ";
        ss << "[INFO] Session ended.";

        m_logFile << ss.str() << std::endl;

        if (m_consoleOutput) {
            std::cout << ss.str() << std::endl;
        }

        m_logFile.close();
    }

    void Logger::setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_minLevel = level;
    }

    LogLevel Logger::getLevel() const {
        return m_minLevel;
    }

    void Logger::enableConsoleOutput(bool enable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_consoleOutput = enable;
    }

    void Logger::flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logFile.flush();
    }

    std::string Logger::levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default:              return "UNKNOWN";
        }
    }
}