#include "ohmy/stratum_client.hpp"
#include <iostream>
#include <cassert>

using namespace ohmy::network;

void test_client_creation() {
    std::cout << "Testing Stratum client creation..." << std::endl;
    
    StratumClient client("stratum+tcp://pool.example.com:4444", "0x0000000000000000000000000000000000000000");
    
    std::cout << "✓ Stratum client created successfully" << std::endl;
}

void test_connection_status() {
    std::cout << "Testing connection status..." << std::endl;
    
    StratumClient client("stratum+tcp://pool.example.com:4444", "0x0000000000000000000000000000000000000000");
    
    // Should not be connected initially
    assert(!client.isConnected());
    
    std::cout << "✓ Connection status tests passed" << std::endl;
}

int main() {
    std::cout << "\n=== Running Stratum Client Unit Tests ===" << std::endl;
    
    try {
        test_client_creation();
        test_connection_status();
        
        std::cout << "\n✓ All Stratum tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
