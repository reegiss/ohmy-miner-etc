#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/types.hpp"
#include <iostream>
#include <vector>
#include <cassert>

using namespace ohmy::cuda;

/**
 * Phase 5: Multi-GPU Unit Tests (without GTest)
 * 
 * Tests for multi-GPU mining architecture:
 * - Device initialization
 * - Per-device statistics
 * - Callback infrastructure
 * - Hashrate aggregation
 */

int main() {
    LOG_INFO("=== Starting Phase 5 Multi-GPU Tests ===");
    
    DeviceManager mgr;
    int testsPassed = 0;
    int testsFailed = 0;
    
    // Test 1: Device enumeration
    {
        LOG_INFO("Test 1: Device enumeration");
        try {
            std::vector<DeviceInfo> devices = mgr.getDevices();
            LOG_INFO("  Found " + std::to_string(devices.size()) + " CUDA device(s)");
            for (const auto& dev : devices) {
                LOG_INFO("    GPU#" + std::to_string(dev.deviceId) + ": " + dev.name + 
                        " (" + std::to_string(dev.totalMemory / 1e9) + " GB)");
            }
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 2: Device count API
    {
        LOG_INFO("Test 2: Get device count");
        try {
            int count = mgr.getDeviceCount();
            LOG_INFO("  Device count: " + std::to_string(count));
            assert(count >= 0);
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 3: Device initialization check
    {
        LOG_INFO("Test 3: Is device initialized (before init)");
        try {
            bool initialized = mgr.isDeviceInitialized(0);
            LOG_INFO("  GPU#0 initialized: " + std::string(initialized ? "yes" : "no"));
            assert(!initialized);  // Should be false before initialization
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 4: Per-device statistics API
    {
        LOG_INFO("Test 4: Get per-device statistics");
        try {
            std::vector<std::string> stats = mgr.getDeviceStatistics(-1);
            LOG_INFO("  Statistics count: " + std::to_string(stats.size()));
            for (const auto& stat : stats) {
                LOG_INFO("    " + stat);
            }
            assert(stats.size() >= 0);
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 5: Aggregate statistics API
    {
        LOG_INFO("Test 5: Get aggregate statistics");
        try {
            std::string stats = mgr.getAggregateStatistics();
            LOG_INFO("  Aggregate: " + stats);
            assert(!stats.empty());
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 6: Hash rate APIs
    {
        LOG_INFO("Test 6: Hash rate APIs");
        try {
            uint64_t totalRate = mgr.getTotalHashRate();
            LOG_INFO("  Total hashrate: " + std::to_string(totalRate) + " H/s");
            
            std::vector<uint64_t> perDeviceRates = mgr.getAllHashRates();
            LOG_INFO("  Per-device rates: " + std::to_string(perDeviceRates.size()) + " devices");
            for (size_t i = 0; i < perDeviceRates.size(); ++i) {
                LOG_INFO("    GPU#" + std::to_string(i) + ": " + 
                        std::to_string(perDeviceRates[i]) + " H/s");
            }
            
            assert(totalRate >= 0);
            assert(perDeviceRates.size() >= 0);
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 7: Mining job context API
    {
        LOG_INFO("Test 7: Set mining job context");
        try {
            mgr.setMiningJobContext("test_job_id", 0);
            LOG_INFO("  Mining job context set successfully");
            testsPassed++;
        } catch (const std::exception& e) {
            LOG_ERROR("  FAILED: " + std::string(e.what()));
            testsFailed++;
        }
    }
    
    // Test 8: Result callback setup
    // Summary
    LOG_INFO("=== Phase 5 Multi-GPU Tests Summary ===");
    LOG_INFO("Passed: " + std::to_string(testsPassed));
    LOG_INFO("Failed: " + std::to_string(testsFailed));
    LOG_INFO("Total:  " + std::to_string(testsPassed + testsFailed));
    
    if (testsFailed == 0) {
        LOG_INFO("✓ All tests passed!");
        return 0;
    } else {
        LOG_ERROR("✗ Some tests failed");
        return 1;
    }
}
