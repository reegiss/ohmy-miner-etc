// Debug Ethash calculation to find where the algorithm differs
#include <iostream>
#include <iomanip>
#include <cstring>
#include "ohmy/types.hpp"
#include "ohmy/keccak.hpp"
#include "ohmy/ethash.hpp"

using namespace ohmy;

// FNV hash
uint32_t fnv1a(uint32_t h, uint32_t d) {
    return (h ^ d) * 0x01000193;
}

void print_hash(const char* label, const uint8_t* data, size_t len) {
    std::cout << label << ": ";
    for (size_t i = 0; i < len; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    std::cout << std::dec << "\n";
}

int main() {
    // Test with a known header and nonce
    const char* headerHex = "f4313fbb48d0cb02a84b6af810ef2f60c1a251bd505f4a15daa7b82ef23cafc4";
    hash32_t header;
    for (int i = 0; i < 32; i++) {
        char buf[3] = {headerHex[i*2], headerHex[i*2+1], 0};
        header[i] = strtol(buf, nullptr, 16);
    }
    
    uint64_t nonce = 0x0000000000000001ULL; // Simple nonce for testing
    
    std::cout << "=== Ethash Algorithm Debug ===\n\n";
    std::cout << "Input header: " << headerHex << "\n";
    std::cout << "Input nonce: 0x" << std::hex << std::setw(16) << std::setfill('0') << nonce << std::dec << "\n\n";
    
    // Step 1: Compute seed = Keccak512(header || nonce)
    uint8_t headerNonce[40]; // 32 + 8
    std::memcpy(headerNonce, header.data(), 32);
    
    // Nonce in little-endian
    for (int i = 0; i < 8; i++) {
        headerNonce[32 + i] = (nonce >> (i * 8)) & 0xFF;
    }
    
    std::cout << "Step 1: header || nonce (40 bytes)\n";
    print_hash("  Combined", headerNonce, 40);
    
    hash64_t seed = Keccak::keccak512(headerNonce, 40);
    std::cout << "\nStep 2: seed = Keccak512(header || nonce)\n";
    print_hash("  Seed", seed.data(), 64);
    
    // Step 3: Initialize mix (replicate seed)
    uint32_t mix[32]; // 128 bytes
    const uint32_t* seedWords = reinterpret_cast<const uint32_t*>(seed.data());
    for (int i = 0; i < 16; i++) {
        mix[i] = seedWords[i];
        mix[i + 16] = seedWords[i];
    }
    
    std::cout << "\nStep 3: Initialize mix (seed replicated)\n";
    std::cout << "  mix[0] = 0x" << std::hex << std::setw(8) << std::setfill('0') << mix[0] << std::dec << "\n";
    std::cout << "  mix[16] = 0x" << std::hex << std::setw(8) << std::setfill('0') << mix[16] << std::dec << "\n";
    
    // Step 4: DAG mixing (we'll simulate with dummy DAG for now)
    std::cout << "\nStep 4: DAG mixing (64 accesses)\n";
    std::cout << "  s[0] = 0x" << std::hex << std::setw(8) << std::setfill('0') << seedWords[0] << std::dec << "\n";
    
    uint32_t s0 = seedWords[0];
    
    // Show first few DAG accesses
    for (int i = 0; i < 3; i++) {
        uint32_t dagIndex = fnv1a(i ^ s0, mix[i % 32]);
        std::cout << "  Access " << i << ": fnv1a(" << i << " ^ s0, mix[" << (i % 32) << "]) = 0x" 
                  << std::hex << std::setw(8) << std::setfill('0') << dagIndex << std::dec << "\n";
    }
    
    // Step 5: Compression (without actual DAG, just show the algorithm)
    std::cout << "\nStep 5: Compression (FNV of 4 consecutive words)\n";
    std::cout << "  Example: compressed[0] = FNV(FNV(FNV(mix[0], mix[1]), mix[2]), mix[3])\n";
    uint32_t c0 = fnv1a(mix[0], mix[1]);
    c0 = fnv1a(c0, mix[2]);
    c0 = fnv1a(c0, mix[3]);
    std::cout << "  compressed[0] = 0x" << std::hex << std::setw(8) << std::setfill('0') << c0 << std::dec << "\n";
    
    std::cout << "\n=== Algorithm Steps Verified ===\n";
    std::cout << "Compare these values with a reference implementation (ethminer, geth, etc.)\n";
    std::cout << "to find where our calculation diverges.\n";
    
    return 0;
}
