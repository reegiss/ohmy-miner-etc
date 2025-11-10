#include "device_stats.hpp"
#include <sstream>
#include <iomanip>
#include "ohmy/logger.hpp"

namespace ohmy { namespace cuda {

uint64_t DeviceStatsUtil::totalHashRate(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates) {
    uint64_t total = 0;
    for (const auto& entry : deviceStates) {
        const auto& stPtr = entry.second;
        if (stPtr && stPtr->initialized && stPtr->totalTime > 0.0f) {
            float seconds = stPtr->totalTime / 1000.0f;
            if (seconds <= 0.0f) continue;
            total += static_cast<uint64_t>(stPtr->totalHashes / seconds);
        }
    }
    return total;
}

std::vector<uint64_t> DeviceStatsUtil::allHashRates(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates) {
    std::vector<uint64_t> rates;
    rates.reserve(deviceStates.size());
    for (const auto& entry : deviceStates) {
        const auto& stPtr = entry.second;
        if (stPtr && stPtr->initialized && stPtr->totalTime > 0.0f) {
            float seconds = stPtr->totalTime / 1000.0f;
            if (seconds <= 0.001f) {
                LOG_WARN("Elapsed time too small, defaulting to 1 ms to prevent division errors.");
                seconds = 0.001f;
            }
            rates.push_back(static_cast<uint64_t>(stPtr->totalHashes / seconds));
        } else {
            rates.push_back(0);
        }
    }
    return rates;
}

std::vector<std::string> DeviceStatsUtil::formatDeviceStatistics(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates,
                                                             int deviceId) {
    std::vector<std::string> stats;
    if (deviceId >= 0) {
        auto it = deviceStates.find(deviceId);
        if (it != deviceStates.end() && it->second && it->second->initialized) {
            auto* state = it->second.get();
            float seconds = state->totalTime > 0.0f ? state->totalTime / 1000.0f : 0.0f;
            double hashrate = seconds > 0.0 ? (double)state->totalHashes / seconds / 1e6 : 0.0;
            std::ostringstream oss;
            oss << "GPU#" << deviceId << ": "
                << "Hashes=" << state->totalHashes << " "
                << "Time=" << state->totalTime << "ms "
                << "Rate=" << std::fixed << std::setprecision(2) << hashrate << " MH/s";
            stats.push_back(oss.str());
        }
        return stats;
    }
    for (const auto& entry : deviceStates) {
        if (entry.second && entry.second->initialized) {
            auto* state = entry.second.get();
            float seconds = state->totalTime > 0.0f ? state->totalTime / 1000.0f : 0.0f;
            double hashrate = seconds > 0.0 ? (double)state->totalHashes / seconds / 1e6 : 0.0;
            std::ostringstream oss;
            oss << "GPU#" << entry.first << ": "
                << "Hashes=" << state->totalHashes << " "
                << "Time=" << state->totalTime << "ms "
                << "Rate=" << std::fixed << std::setprecision(2) << hashrate << " MH/s";
            stats.push_back(oss.str());
        }
    }
    return stats;
}

}} // namespace ohmy::cuda
