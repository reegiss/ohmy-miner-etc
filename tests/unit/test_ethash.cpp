#include "ohmy/ethash.hpp"
#include <cassert>
#include <iostream>

using namespace ohmy;

void test_epoch_calculation() {
    std::cout << "Testing epoch calculation..." << std::endl;
    
    // Block 0 should be epoch 0
    assert(Ethash::getEpoch(0) == 0);
    
    // Block 29999 should be epoch 0
    assert(Ethash::getEpoch(29999) == 0);
    
    // Block 30000 should be epoch 1
    assert(Ethash::getEpoch(30000) == 1);
    
    // Block 60000 should be epoch 2
    assert(Ethash::getEpoch(60000) == 2);
    
    std::cout << "✓ Epoch calculation tests passed" << std::endl;
}

void test_dataset_size() {
    std::cout << "Testing dataset size calculation..." << std::endl;
    
    // Epoch 0 should have base size
    uint64_t size0 = Ethash::getDatasetSize(0);
    assert(size0 > 0);
    
    // Epoch 1 should be larger
    uint64_t size1 = Ethash::getDatasetSize(1);
    assert(size1 > size0);
    
    std::cout << "  Epoch 0 dataset: " << size0 / (1024*1024) << " MB" << std::endl;
    std::cout << "  Epoch 1 dataset: " << size1 / (1024*1024) << " MB" << std::endl;
    std::cout << "✓ Dataset size tests passed" << std::endl;
}

void test_cache_size() {
    std::cout << "Testing cache size calculation..." << std::endl;
    
    // Epoch 0 should have base cache size
    uint64_t cache0 = Ethash::getCacheSize(0);
    assert(cache0 > 0);
    
    // Epoch 1 should be larger
    uint64_t cache1 = Ethash::getCacheSize(1);
    assert(cache1 > cache0);
    
    std::cout << "  Epoch 0 cache: " << cache0 / (1024*1024) << " MB" << std::endl;
    std::cout << "  Epoch 1 cache: " << cache1 / (1024*1024) << " MB" << std::endl;
    std::cout << "✓ Cache size tests passed" << std::endl;
}

int main() {
    std::cout << "\n=== Running Ethash Unit Tests ===" << std::endl;
    
    try {
        test_epoch_calculation();
        test_dataset_size();
        test_cache_size();
        
        std::cout << "\n✓ All Ethash tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
