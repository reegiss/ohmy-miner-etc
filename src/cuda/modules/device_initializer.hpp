#ifndef DEVICE_INITIALIZER_H
#define DEVICE_INITIALIZER_H

#include <cuda_runtime.h>
#include <string>
#include <vector>
#include <map>
#include "ohmy/logger.hpp"

namespace ohmy {
namespace cuda {

struct DeviceState;

class DeviceInitializer {
public:
    static bool initDevice(int deviceId, const void* dag, size_t dagSize, DeviceState& state);
    static int initializeAllDevices(const void* dag, size_t dagSize, std::map<int, std::unique_ptr<DeviceState>>& deviceStates);
};

} // namespace cuda
} // namespace ohmy

#endif // DEVICE_INITIALIZER_H