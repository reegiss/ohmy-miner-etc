#ifndef OHMY_CUDA_DEVICE_STATS_HPP
#define OHMY_CUDA_DEVICE_STATS_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "ohmy/cuda/core/device_state.hpp"

namespace ohmy { namespace cuda {

/**
 * DeviceStatsUtil
 * ----------------
 * Pure static utility collection for aggregating and formatting per-device
 * hash rate statistics. Separates presentation / aggregation logic from
 * DeviceState to keep state POD-like and minimize dependencies.
 *
 * Functions:
 *  - totalHashRate: Sum of all device hash counters
 *  - allHashRates: Vector of individual device hash counts (ordered by map iteration)
 *  - formatDeviceStatistics: Returns human-readable strings (single device or all)
 *
 * Input Container:
 *  - std::map<int, unique_ptr<DeviceState>> to preserve stable iteration order by device id
 *
 * Thread-safety:
 *  - Caller must ensure consistent snapshot (e.g., gather under external lock
 *    if concurrent updates occur), as we intentionally avoid locking here.
 */
struct DeviceStatsUtil {
    static uint64_t totalHashRate(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates);
    static std::vector<uint64_t> allHashRates(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates);
    static std::vector<std::string> formatDeviceStatistics(const std::map<int, std::unique_ptr<DeviceState>>& deviceStates,
                                                           int deviceId = -1);
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_DEVICE_STATS_HPP
