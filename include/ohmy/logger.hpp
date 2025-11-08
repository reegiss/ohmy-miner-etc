#pragma once

#include <string>
#include <memory>

namespace ohmy {
namespace utils {

/**
 * @brief Logging levels
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
 * @brief Logger class for application-wide logging
 */
class Logger {
public:
    /**
     * @brief Initialize logger
     */
    static void init(LogLevel level = LogLevel::INFO, const std::string& logFile = "");

    /**
     * @brief Set logging level
     */
    static void setLevel(LogLevel level);

    /**
     * @brief Log trace message
     */
    static void trace(const std::string& message);

    /**
     * @brief Log debug message
     */
    static void debug(const std::string& message);

    /**
     * @brief Log info message
     */
    static void info(const std::string& message);

    /**
     * @brief Log warning message
     */
    static void warn(const std::string& message);

    /**
     * @brief Log error message
     */
    static void error(const std::string& message);

    /**
     * @brief Log fatal message
     */
    static void fatal(const std::string& message);
};

// Convenience macros
#define LOG_TRACE(msg) ohmy::utils::Logger::trace(msg)
#define LOG_DEBUG(msg) ohmy::utils::Logger::debug(msg)
#define LOG_INFO(msg)  ohmy::utils::Logger::info(msg)
#define LOG_WARN(msg)  ohmy::utils::Logger::warn(msg)
#define LOG_ERROR(msg) ohmy::utils::Logger::error(msg)
#define LOG_FATAL(msg) ohmy::utils::Logger::fatal(msg)

} // namespace utils
} // namespace ohmy
