#include "ohmy/config.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace ohmy {
namespace utils {

std::map<std::string, std::string> Config::config_;

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        // TODO: Parse JSON configuration
        // Store values in config_ map
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config file: " << e.what() << std::endl;
        return false;
    }
}

Config::PoolConfig Config::getPoolConfig() {
    PoolConfig pool;
    // TODO: Read from config_ map
    pool.url = config_["pool_url"];
    pool.wallet = config_["wallet"];
    pool.worker = config_["worker"];
    pool.useTLS = config_["use_tls"] == "true";
    return pool;
}

Config::DeviceConfig Config::getDeviceConfig() {
    DeviceConfig device;
    // TODO: Read from config_ map
    return device;
}

Config::GeneralConfig Config::getGeneralConfig() {
    GeneralConfig general;
    // TODO: Read from config_ map
    return general;
}

bool Config::parseArgs(int argc, char** argv) {
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--pool" && i + 1 < argc) {
            config_["pool_url"] = argv[++i];
        } else if (arg == "--wallet" && i + 1 < argc) {
            config_["wallet"] = argv[++i];
        } else if (arg == "--worker" && i + 1 < argc) {
            config_["worker"] = argv[++i];
        } else if (arg == "--devices" && i + 1 < argc) {
            config_["devices"] = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            config_["threads"] = argv[++i];
        } else if (arg == "--log" && i + 1 < argc) {
            config_["log_file"] = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            return loadFromFile(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "OhMy Miner ETC - Ethereum Classic GPU Miner\n\n"
                      << "Usage: ohmy-miner-etc [options]\n\n"
                      << "Options:\n"
                      << "  --pool <url>        Pool URL (required)\n"
                      << "  --wallet <address>  Wallet address (required)\n"
                      << "  --worker <name>     Worker name (optional)\n"
                      << "  --devices <ids>     GPU device IDs (comma-separated)\n"
                      << "  --threads <num>     Number of mining threads\n"
                      << "  --config <file>     Load configuration from file\n"
                      << "  --log <file>        Log file path\n"
                      << "  --help, -h          Show this help message\n";
            return false;
        }
    }

    return true;
}

} // namespace utils
} // namespace ohmy
