#include "ohmy/device_manager.hpp"
#include <iostream>
#include <chrono>

using namespace ohmy::cuda;

void benchmark_device_detection() {
    std::cout << "\n=== Benchmarking CUDA Device Detection ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    DeviceManager manager;
    auto devices = manager.getDevices();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Devices found: " << devices.size() << std::endl;
    std::cout << "Detection time: " << duration.count() << " ms" << std::endl;
    
    for (const auto& device : devices) {
        std::cout << "\nDevice " << device.deviceId << ": " << device.name << std::endl;
        std::cout << "  Compute Capability: " << device.computeCapability << std::endl;
        std::cout << "  Memory: " << (device.totalMemory / (1024*1024)) << " MB" << std::endl;
        std::cout << "  SMs: " << device.multiProcessorCount << std::endl;
    }
}

void benchmark_memory_allocation() {
    std::cout << "\n=== Benchmarking CUDA Memory Operations ===" << std::endl;
    
    // TODO: Implement memory allocation benchmarks
    std::cout << "Memory allocation benchmarks - TODO" << std::endl;
}

int main() {
    std::cout << "\n=== Running CUDA Kernel Benchmarks ===" << std::endl;
    
    try {
        benchmark_device_detection();
        benchmark_memory_allocation();
        
        std::cout << "\n✓ CUDA benchmarks completed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}
