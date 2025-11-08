#include "ohmy/device_manager.hpp"
#include "ohmy/ethash.hpp"
#include <iostream>
#include <chrono>
#include <vector>

using namespace ohmy;
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

void benchmark_hashrate() {
    std::cout << "\n=== Benchmarking GPU Hashrate ===" << std::endl;
    
    DeviceManager manager;
    auto devices = manager.getDevices();
    
    if (devices.empty()) {
        std::cout << "No CUDA devices available" << std::endl;
        return;
    }
    
    // Generate small test DAG (just a few MB for benchmark)
    std::cout << "Generating test cache..." << std::endl;
    auto cache = Ethash::calculateCache(0);
    
    // Generate a small portion of DAG for testing
    std::cout << "Generating test DAG items..." << std::endl;
    const size_t testDagItems = 1024;  // 64KB for quick test
    std::vector<hash64_t> testDag(testDagItems);
    
    for (size_t i = 0; i < testDagItems; ++i) {
        testDag[i] = Ethash::calculateDatasetItem(cache, i);
    }
    
    size_t dagSize = testDagItems * 64;  // 64 bytes per item
    
    std::cout << "Initializing device with " << (dagSize / 1024) << " KB DAG..." << std::endl;
    if (!manager.initDevice(0, testDag.data(), dagSize)) {
        std::cout << "Failed to initialize device" << std::endl;
        return;
    }
    
    // Prepare test work
    hash32_t headerHash;
    headerHash.fill(0xAB);
    
    uint64_t target = 0xFFFFFFFFFFFFFFFFULL;  // Very easy target
    uint64_t startNonce = 0;
    
    // Test different batch sizes
    std::vector<uint64_t> batchSizes = {1024, 4096, 16384, 65536, 262144, 1048576};
    
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "| Batch Size | Time (ms) | Hashrate (MH/s) | Throughput |" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    
    for (uint64_t batchSize : batchSizes) {
        std::vector<Solution> solutions;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        manager.search(headerHash, target, startNonce, batchSize, solutions);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double milliseconds = duration.count() / 1000.0;
        double seconds = duration.count() / 1000000.0;
        double hashrate = (batchSize / 1000000.0) / seconds;  // MH/s
        
        printf("| %10llu | %9.2f | %15.2f | %10.0f |\n", 
               batchSize, milliseconds, hashrate, batchSize / seconds);
    }
    
    std::cout << std::string(70, '=') << std::endl;
    
    // Get average hashrate
    uint64_t avgHashrate = manager.getHashRate(0);
    double avgMH = avgHashrate / 1000000.0;
    
    std::cout << "\n✓ Average Hashrate: " << avgMH << " MH/s" << std::endl;
    std::cout << "  (" << avgHashrate << " H/s)" << std::endl;
}

void benchmark_memory_allocation() {
    std::cout << "\n=== Benchmarking CUDA Memory Operations ===" << std::endl;
    std::cout << "Memory allocation benchmarks - TODO" << std::endl;
}

int main() {
    std::cout << "\n=== Running CUDA Kernel Benchmarks ===" << std::endl;
    
    try {
        benchmark_device_detection();
        benchmark_hashrate();
        benchmark_memory_allocation();
        
        std::cout << "\n✓ CUDA benchmarks completed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}
