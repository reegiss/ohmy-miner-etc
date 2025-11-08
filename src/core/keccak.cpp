#include "ohmy/keccak.hpp"
#include <cstring>
#include <algorithm>

namespace ohmy {

// Round constants for Keccak-f[1600] iota step
const uint64_t Keccak::ROUND_CONSTANTS[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

// Rotation offsets for Keccak-f[1600] rho step
const int Keccak::ROTATION_OFFSETS[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
};

// Rotate left for 64-bit value
static inline uint64_t rotl64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

// Load 64-bit word from byte array (little-endian)
static inline uint64_t load64(const uint8_t* x) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= (uint64_t)x[i] << (8 * i);
    }
    return u;
}

// Store 64-bit word to byte array (little-endian)
static inline void store64(uint8_t* x, uint64_t u) {
    for (int i = 0; i < 8; ++i) {
        x[i] = u >> (8 * i);
    }
}

void Keccak::keccak_f1600(uint64_t state[STATE_SIZE]) {
    for (int round = 0; round < 24; ++round) {
        // Theta
        uint64_t C[5], D[5];
        for (int x = 0; x < 5; ++x) {
            C[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (int x = 0; x < 5; ++x) {
            D[x] = C[(x + 4) % 5] ^ rotl64(C[(x + 1) % 5], 1);
        }
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                state[x + 5 * y] ^= D[x];
            }
        }
        
        // Rho and Pi
        uint64_t B[25];
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                B[y + 5 * ((2 * x + 3 * y) % 5)] = rotl64(state[x + 5 * y], ROTATION_OFFSETS[x + 5 * y]);
            }
        }
        
        // Chi
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                state[x + 5 * y] = B[x + 5 * y] ^ ((~B[(x + 1) % 5 + 5 * y]) & B[(x + 2) % 5 + 5 * y]);
            }
        }
        
        // Iota
        state[0] ^= ROUND_CONSTANTS[round];
    }
}

void Keccak::keccak(const uint8_t* data, size_t len, uint8_t* output, size_t outputLen) {
    // Rate = 1600 - 2*outputLen*8 bits = (1600 - 2*outputLen*8) / 8 bytes
    size_t rate = 200 - 2 * outputLen;
    
    // Initialize state to zeros
    uint64_t state[STATE_SIZE] = {0};
    uint8_t temp[200];
    
    // Absorb phase
    while (len >= rate) {
        // XOR block into state
        for (size_t i = 0; i < rate; ++i) {
            temp[i] = ((uint8_t*)state)[i] ^ data[i];
        }
        std::memcpy(state, temp, rate);
        keccak_f1600(state);
        data += rate;
        len -= rate;
    }
    
    // Pad last block
    std::memset(temp, 0, 200);
    std::memcpy(temp, data, len);
    
    // Keccak padding: append 0x01, then zeros, then 0x80
    temp[len] ^= 0x01;
    temp[rate - 1] ^= 0x80;
    
    // XOR padded block into state
    for (size_t i = 0; i < rate; ++i) {
        ((uint8_t*)state)[i] ^= temp[i];
    }
    keccak_f1600(state);
    
    // Squeeze phase - extract output
    std::memcpy(output, state, outputLen);
}

hash32_t Keccak::keccak256(const uint8_t* data, size_t len) {
    hash32_t result;
    keccak(data, len, result.data(), 32);
    return result;
}

hash32_t Keccak::keccak256(const hash32_t& input) {
    return keccak256(input.data(), input.size());
}

hash64_t Keccak::keccak512(const uint8_t* data, size_t len) {
    hash64_t result;
    keccak(data, len, result.data(), 64);
    return result;
}

hash64_t Keccak::keccak512(const hash64_t& input) {
    return keccak512(input.data(), input.size());
}

} // namespace ohmy
