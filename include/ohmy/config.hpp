#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace ohmy {
namespace utils {

/**
 * @brief Configuration manager for miner settings
 */
class Config {
public:
    struct PoolConfig {
        std::string url;
        std::string wallet;
        std::string worker;
        bool useTLS;
    };

    struct DeviceConfig {
        std::vector<int> devices;  // GPU device IDs to use
        uint32_t threads;          // Threads per device
        uint32_t intensity;        // Mining intensity
    };

    struct GeneralConfig {
        std::string logFile;
        std::string logLevel;
        std::string dagDir;
        bool benchmark;
    };

    /**
     * @brief Load configuration from file
     */
    static bool loadFromFile(const std::string& filename);

    /**
     * @brief Get pool configuration
     */
    static PoolConfig getPoolConfig();

    /**
     * @brief Get device configuration
     */
    static DeviceConfig getDeviceConfig();

    /**
     * @brief Get general configuration
     */
    static GeneralConfig getGeneralConfig();

    /**
     * @brief Parse command line arguments
     */
    static bool parseArgs(int argc, char** argv);

private:
    static std::map<std::string, std::string> config_;
};

} // namespace utils
} // namespace ohmy
