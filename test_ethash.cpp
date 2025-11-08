// Test to verify Ethash calculation
// We'll compute Ethash for a known header + nonce and compare with expected result

#include <iostream>
#include <iomanip>
#include <cstring>
#include "ohmy/types.hpp"
#include "ohmy/keccak.hpp"

// FNV hash function (same as in kernel)
uint32_t fnv1a(uint32_t h, uint32_t d) {
    return (h ^ d) * 0x01000193;
}

// Simulate what the kernel does
void ethash_compute(const uint8_t* header32, uint64_t nonce, 
                    const uint64_t* dag, uint64_t dagSize,
                    uint8_t* mixHashOut, uint8_t* resultOut) {
    
    // 1. Compute seed = Keccak512(header || nonce)
    uint8_t headerNonce[40]; // 32 + 8
    std::memcpy(headerNonce, header32, 32);
    
    // Nonce in little-endian
    for (int i = 0; i < 8; i++) {
        headerNonce[32 + i] = (nonce >> (i * 8)) & 0xFF;
    }
    
    uint64_t seed[8]; // 64 bytes = 8 x uint64_t
    ohmy::hash64_t seed_bytes = ohmy::Keccak::keccak512(headerNonce, 40);
    std::memcpy(seed, seed_bytes.data(), 64);
    
    // 2. Initialize mix (replicate seed)
    uint32_t mix[32]; // 128 bytes = 32 x uint32_t
    for (int i = 0; i < 16; i++) {
        mix[i * 2] = seed[i] & 0xFFFFFFFF;
        mix[i * 2 + 1] = seed[i] >> 32;
    }
    
    // 3. Perform 64 DAG accesses
    uint32_t numItems = dagSize / 128; // Each DAG item is 128 bytes
    for (int i = 0; i < 64; i++) {
        // Calculate DAG index
        uint32_t p = fnv1a(i ^ seed[0], mix[i % 32]) % numItems;
        
        // Fetch DAG item (16 x uint64_t = 128 bytes)
        const uint64_t* dagItem = dag + (p * 16);
        
        // Mix it in using FNV
        for (int j = 0; j < 32; j++) {
            uint32_t dagWord = (j % 2 == 0) ? (dagItem[j/2] & 0xFFFFFFFF) : (dagItem[j/2] >> 32);
            mix[j] = fnv1a(mix[j], dagWord);
        }
    }
    
    // 4. Compress mix to 32 bytes
    uint32_t compressed[8];
    for (int i = 0; i < 8; i++) {
        compressed[i] = fnv1a(mix[i * 4], mix[i * 4 + 1]);
        compressed[i] = fnv1a(compressed[i], mix[i * 4 + 2]);
        compressed[i] = fnv1a(compressed[i], mix[i * 4 + 3]);
    }
    
    // Copy mixHash
    std::memcpy(mixHashOut, compressed, 32);
    
    // 5. Compute result = Keccak256(seed || compressed)
    uint8_t finalInput[96]; // 64 + 32
    std::memcpy(finalInput, seed, 64);
    std::memcpy(finalInput + 64, compressed, 32);
    
    ohmy::hash32_t result = ohmy::Keccak::keccak256(finalInput, 96);
    std::memcpy(resultOut, result.data(), 32);
}

int main() {
    std::cout << "Ethash CPU verification test\n";
    std::cout << "This will help us verify if the algorithm is correct\n\n";
    
    // We need a small DAG for testing
    // For now, just test the seed calculation which we know works
    
    // Test header (from our mining)
    const char* headerHex = "286be7228a2ba680290d477312ab63c1f76623d5702afd9a304a3cfaeb0aad75";
    ohmy::hash32_t header;
    for (int i = 0; i < 32; i++) {
        char buf[3] = {headerHex[i*2], headerHex[i*2+1], 0};
        header[i] = strtol(buf, nullptr, 16);
    }
    
    // Test nonce
    uint64_t nonce = 0x0000000000002c0a;
    
    std::cout << "Header: " << headerHex << "\n";
    std::cout << "Nonce:  0x" << std::hex << std::setw(16) << std::setfill('0') << nonce << "\n\n";
    
    // Compute seed
    uint8_t headerNonce[40];
    std::memcpy(headerNonce, header.data(), 32);
    for (int i = 0; i < 8; i++) {
        headerNonce[32 + i] = (nonce >> (i * 8)) & 0xFF;
    }
    
    ohmy::hash64_t seed = ohmy::Keccak::keccak512(headerNonce, 40);
    
    std::cout << "Seed (first 32 bytes): ";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)seed[i];
    }
    std::cout << "\n";
    
    std::cout << "\nTo validate: Send this header+nonce to the pool and see if they accept it\n";
    std::cout << "If they reject it, the problem is in our mixing/DAG access logic\n";
    
    return 0;
}
