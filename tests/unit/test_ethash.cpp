#include "ohmy/ethash.hpp"
#include "ohmy/keccak.hpp"
#include <iostream>
#include <cassert>
#include <iomanip>
#include <sstream>

using namespace ohmy;

// Helper to convert hex string to bytes
std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// Helper to convert bytes to hex string
std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

void test_keccak256_empty() {
    // Test vector: Keccak-256("") 
    // Expected: c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
    const char* input = "";
    auto result = Keccak::keccak256(reinterpret_cast<const uint8_t*>(input), 0);
    
    std::string expected = "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
    std::string actual = bytes_to_hex(result.data(), 32);
    
    if (actual != expected) {
        std::cerr << "✗ Keccak-256 empty string test FAILED\n";
        std::cerr << "  Expected: " << expected << "\n";
        std::cerr << "  Got:      " << actual << "\n";
        assert(false);
    }
    std::cout << "✓ Keccak-256 empty string test passed\n";
}

void test_keccak256_short() {
    // Test vector: Keccak-256("abc")
    // Expected: 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
    const char* input = "abc";
    auto result = Keccak::keccak256(reinterpret_cast<const uint8_t*>(input), 3);
    
    std::string expected = "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45";
    std::string actual = bytes_to_hex(result.data(), 32);
    
    if (actual != expected) {
        std::cerr << "✗ Keccak-256 'abc' test FAILED\n";
        std::cerr << "  Expected: " << expected << "\n";
        std::cerr << "  Got:      " << actual << "\n";
        assert(false);
    }
    std::cout << "✓ Keccak-256 'abc' test passed\n";
}

void test_keccak256_ethereum() {
    // Test vector from Ethereum: Keccak-256("testing")
    // Expected: 5f16f4c7f149ac4f9510d9cf8cf384038ad348b3bcdc01915f95de12df9d1b02
    const char* input = "testing";
    auto result = Keccak::keccak256(reinterpret_cast<const uint8_t*>(input), 7);
    
    std::string expected = "5f16f4c7f149ac4f9510d9cf8cf384038ad348b3bcdc01915f95de12df9d1b02";
    std::string actual = bytes_to_hex(result.data(), 32);
    
    if (actual != expected) {
        std::cerr << "✗ Keccak-256 'testing' test FAILED\n";
        std::cerr << "  Expected: " << expected << "\n";
        std::cerr << "  Got:      " << actual << "\n";
        assert(false);
    }
    std::cout << "✓ Keccak-256 'testing' test passed\n";
}

void test_keccak512() {
    // Test vector: Keccak-512("abc")
    // Expected: 18587dc2ea106b9a1563e32b3312421ca164c7f1f07bc922a9c83d77cea3a1e5d0c69910739025372dc14ac9642629379540c17e2a65b19d77aa511a9d00bb96
    const char* input = "abc";
    auto result = Keccak::keccak512(reinterpret_cast<const uint8_t*>(input), 3);
    
    std::string expected = "18587dc2ea106b9a1563e32b3312421ca164c7f1f07bc922a9c83d77cea3a1e5"
                          "d0c69910739025372dc14ac9642629379540c17e2a65b19d77aa511a9d00bb96";
    std::string actual = bytes_to_hex(result.data(), 64);
    
    if (actual != expected) {
        std::cerr << "✗ Keccak-512 'abc' test FAILED\n";
        std::cerr << "  Expected: " << expected << "\n";
        std::cerr << "  Got:      " << actual << "\n";
        assert(false);
    }
    std::cout << "✓ Keccak-512 'abc' test passed\n";
}

void test_epoch_calculation() {
    assert(Ethash::getEpoch(0) == 0);
    assert(Ethash::getEpoch(29999) == 0);
    assert(Ethash::getEpoch(30000) == 1);
    assert(Ethash::getEpoch(60000) == 2);
    std::cout << "✓ Epoch calculation tests passed\n";
}

void test_fnv_hash() {
    // Test FNV-1a hash function
    // FNV prime: 0x01000193
    uint32_t a = 0x12345678;
    uint32_t b = 0xABCDEF00;
    uint32_t result = Ethash::fnv1a(a, b);
    
    // Expected: a * 0x01000193 ^ b
    uint32_t expected = (a * 0x01000193) ^ b;
    assert(result == expected);
    
    std::cout << "✓ FNV hash test passed\n";
}

void test_cache_generation() {
    std::cout << "Testing cache generation..." << std::endl;
    
    // Generate cache for epoch 0
    auto cache = Ethash::calculateCache(0);
    
    // Cache should not be empty
    assert(!cache.empty());
    
    // Cache size should match getCacheSize
    uint64_t expectedSize = Ethash::getCacheSize(0);
    size_t expectedItems = expectedSize / 64;
    assert(cache.size() == expectedItems);
    
    // First cache item should be deterministic
    // For epoch 0: seed = Keccak256(zeros)
    // cache[0] = Keccak512(seed)
    hash32_t seed{};
    seed.fill(0);
    hash32_t epoch0Seed = Keccak::keccak256(seed.data(), seed.size());
    hash64_t expected_first = Keccak::keccak512(epoch0Seed.data(), epoch0Seed.size());
    
    // Note: actual cache[0] will differ after RandMemoHash rounds
    // but we can check it's not all zeros
    bool notZero = false;
    for (size_t i = 0; i < cache[0].size(); ++i) {
        if (cache[0][i] != 0) {
            notZero = true;
            break;
        }
    }
    assert(notZero);
    
    std::cout << "  Cache items: " << cache.size() << std::endl;
    std::cout << "  Expected items: " << expectedItems << std::endl;
    std::cout << "✓ Cache generation test passed\n";
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
    std::cout << "\n=== Running Keccak Unit Tests ===" << std::endl;
    try {
        test_keccak256_empty();
        test_keccak256_short();
        test_keccak256_ethereum();
        test_keccak512();
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Keccak test failed: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Running Ethash Unit Tests ===" << std::endl;
    
    try {
        test_epoch_calculation();
        test_fnv_hash();
        test_dataset_size();
        test_cache_size();
        test_cache_generation();
        
        std::cout << "\n✅ All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
