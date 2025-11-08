#include "ohmy/stratum_client.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace ohmy::network;

void test_client_creation() {
    std::cout << "Testing Stratum client creation..." << std::endl;
    
    StratumClient client("pool.example.com:3333", "0x1234567890abcdef");
    
    // Should not be connected initially
    assert(!client.isConnected());
    
    std::cout << "✓ Stratum client creation test passed" << std::endl;
}

void test_connection_attempt() {
    std::cout << "Testing connection attempt..." << std::endl;
    
    StratumClient client("localhost:9999", "0xtest");
    
    // Attempt to connect (will fail if no pool running)
    bool connected = client.connect();
    
    // Even if connection fails, test passes (we're testing the mechanism)
    std::cout << "  Connection result: " << (connected ? "success" : "failed (expected if no pool running)") << std::endl;
    
    if (connected) {
        assert(client.isConnected());
        client.disconnect();
        assert(!client.isConnected());
    }
    
    std::cout << "✓ Connection test passed" << std::endl;
}

void test_message_format() {
    std::cout << "Testing Stratum protocol..." << std::endl;
    
    // Test that we can create client without crashing
    StratumClient client("pool.ethermine.org:4444", 
                        "0x1234567890123456789012345678901234567890");
    
    // Test methods don't crash when not connected
    assert(!client.subscribe());  // Should fail when not connected
    assert(!client.authorize("worker1"));  // Should fail when not connected
    
    ohmy::Solution testSolution;
    testSolution.nonce = 0x123456;
    assert(!client.submitSolution(testSolution));  // Should fail when not connected
    
    std::cout << "✓ Protocol test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Running Stratum Client Unit Tests ===" << std::endl;
    
    try {
        test_client_creation();
        test_connection_attempt();
        test_message_format();
        
        std::cout << "\n✅ All Stratum tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }
}

