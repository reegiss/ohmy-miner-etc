#include "ohmy/logger.hpp"
#include <iostream>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ohmy {
namespace utils {

static LogLevel currentLevel = LogLevel::INFO;
static std::ofstream logFile;

void Logger::init(LogLevel level, const std::string& logFileName) {
    currentLevel = level;
    
    if (!logFileName.empty()) {
        logFile.open(logFileName, std::ios::app);
    }
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
}

static std::string getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

static std::string getTimestamp() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static void log(LogLevel level, const std::string& message) {
    if (level < currentLevel) {
        return;
    }

    std::string timestamp = getTimestamp();
    std::string levelStr = getLevelString(level);
    std::string logMessage = "[" + timestamp + "] [" + levelStr + "] " + message;

    // Print to console
    if (level >= LogLevel::ERROR) {
        std::cerr << logMessage << std::endl;
    } else {
        std::cout << logMessage << std::endl;
    }

    // Write to file if open
    if (logFile.is_open()) {
        logFile << logMessage << std::endl;
        logFile.flush();
    }
}

void Logger::trace(const std::string& message) {
    log(LogLevel::TRACE, message);
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::fatal(const std::string& message) {
    log(LogLevel::FATAL, message);
}

} // namespace utils
} // namespace ohmy
