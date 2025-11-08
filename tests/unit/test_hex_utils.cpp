#include "ohmy/hex_utils.hpp"
#include <iostream>
#include <cassert>

using namespace ohmy;
using namespace ohmy::utils;

void testHexToHash32() {
    std::cout << "Testing hexToHash32..." << std::endl;
    
    // Test with 0x prefix
    hash32_t hash1;
    std::string hex1 = "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    assert(HexUtils::hexToHash32(hex1, hash1));
    assert(hash1[0] == 0x01);
    assert(hash1[1] == 0x23);
    assert(hash1[31] == 0xef);
    std::cout << "  ✓ Conversion with 0x prefix works" << std::endl;
    
    // Test without 0x prefix
    hash32_t hash2;
    std::string hex2 = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    assert(HexUtils::hexToHash32(hex2, hash2));
    assert(hash2[0] == 0xfe);
    assert(hash2[1] == 0xdc);
    assert(hash2[31] == 0x10);
    std::cout << "  ✓ Conversion without 0x prefix works" << std::endl;
    
    // Test with short string (should pad)
    hash32_t hash3;
    std::string hex3 = "0x1234";
    assert(HexUtils::hexToHash32(hex3, hash3));
    assert(hash3[30] == 0x12);
    assert(hash3[31] == 0x34);
    std::cout << "  ✓ Short string padding works" << std::endl;
}

void testHash32ToHex() {
    std::cout << "Testing hash32ToHex..." << std::endl;
    
    hash32_t hash;
    for (size_t i = 0; i < 32; ++i) {
        hash[i] = static_cast<uint8_t>(i);
    }
    
    std::string hex = HexUtils::hash32ToHex(hash, true);
    assert(hex.substr(0, 2) == "0x");
    assert(hex.length() == 66); // 0x + 64 hex chars
    std::cout << "  ✓ Hash32 to hex with prefix works" << std::endl;
    
    std::string hexNoPrefix = HexUtils::hash32ToHex(hash, false);
    assert(hexNoPrefix.length() == 64);
    assert(hexNoPrefix.substr(0, 2) == "00");
    std::cout << "  ✓ Hash32 to hex without prefix works" << std::endl;
}

void testHexToUint64() {
    std::cout << "Testing hexToUint64..." << std::endl;
    
    uint64_t value1;
    assert(HexUtils::hexToUint64("0x1234567890abcdef", value1));
    assert(value1 == 0x1234567890abcdefULL);
    std::cout << "  ✓ Hex to uint64 with 0x works" << std::endl;
    
    uint64_t value2;
    assert(HexUtils::hexToUint64("fedcba9876543210", value2));
    assert(value2 == 0xfedcba9876543210ULL);
    std::cout << "  ✓ Hex to uint64 without 0x works" << std::endl;
    
    uint64_t value3;
    assert(HexUtils::hexToUint64("0xff", value3));
    assert(value3 == 0xff);
    std::cout << "  ✓ Short hex to uint64 works" << std::endl;
}

void testUint64ToHex() {
    std::cout << "Testing uint64ToHex..." << std::endl;
    
    std::string hex1 = HexUtils::uint64ToHex(0x1234567890abcdefULL, true);
    assert(hex1 == "0x1234567890abcdef");
    std::cout << "  ✓ Uint64 to hex with prefix works" << std::endl;
    
    std::string hex2 = HexUtils::uint64ToHex(0xff, false);
    assert(hex2 == "ff");
    std::cout << "  ✓ Uint64 to hex without prefix works" << std::endl;
}

void testRoundTrip() {
    std::cout << "Testing round-trip conversions..." << std::endl;
    
    // hash32 round trip
    hash32_t original;
    for (size_t i = 0; i < 32; ++i) {
        original[i] = static_cast<uint8_t>(i * 7 + 3); // Some pattern
    }
    
    std::string hex = HexUtils::hash32ToHex(original, true);
    hash32_t recovered;
    assert(HexUtils::hexToHash32(hex, recovered));
    
    for (size_t i = 0; i < 32; ++i) {
        assert(original[i] == recovered[i]);
    }
    std::cout << "  ✓ Hash32 round-trip works" << std::endl;
    
    // uint64 round trip
    uint64_t originalValue = 0xdeadbeefcafebabeULL;
    std::string hexValue = HexUtils::uint64ToHex(originalValue, true);
    uint64_t recoveredValue;
    assert(HexUtils::hexToUint64(hexValue, recoveredValue));
    assert(originalValue == recoveredValue);
    std::cout << "  ✓ Uint64 round-trip works" << std::endl;
}

int main() {
    std::cout << "\n=== Running Hex Utils Unit Tests ===\n" << std::endl;
    
    try {
        testHexToHash32();
        testHash32ToHex();
        testHexToUint64();
        testUint64ToHex();
        testRoundTrip();
        
        std::cout << "\n✅ All hex utils tests passed!\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
