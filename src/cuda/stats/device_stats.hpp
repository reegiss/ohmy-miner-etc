#ifndef OHMY_CUDA_DEVICE_STATS_HPP
#define OHMY_CUDA_DEVICE_STATS_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "ohmy/cuda/core/device_state.hpp"

namespace ohmy { namespace cuda {

struct DeviceStatsUtil {
    static uint64_t totalHashRate(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates);
    static std::vector<uint64_t> allHashRates(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates);
    static std::vector<std::string> formatDeviceStatistics(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates,
                                                           int deviceId = -1);
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_DEVICE_STATS_HPP
