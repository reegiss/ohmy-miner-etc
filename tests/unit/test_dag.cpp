#include "ohmy/dag_generator.hpp"
#include <iostream>
#include <cassert>

using namespace ohmy::dag;

void test_dag_creation() {
    std::cout << "Testing DAG generator creation..." << std::endl;
    
    DagGenerator generator;
    
    std::cout << "✓ DAG generator created successfully" << std::endl;
}

void test_epoch_check() {
    std::cout << "Testing epoch availability check..." << std::endl;
    
    DagGenerator generator;
    
    // Should not have any epoch initially
    assert(!generator.hasEpoch(0));
    assert(!generator.hasEpoch(1));
    
    std::cout << "✓ Epoch check tests passed" << std::endl;
}

void test_cache_directory() {
    std::cout << "Testing cache directory setup..." << std::endl;
    
    DagGenerator generator;
    generator.setCacheDir("/tmp/dag-test-cache");
    
    std::cout << "✓ Cache directory set successfully" << std::endl;
}

int main() {
    std::cout << "\n=== Running DAG Generator Unit Tests ===" << std::endl;
    
    try {
        test_dag_creation();
        test_epoch_check();
        test_cache_directory();
        
        std::cout << "\n✓ All DAG tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
