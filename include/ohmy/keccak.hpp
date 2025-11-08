#pragma once

#include "types.hpp"
#include <cstdint>
#include <cstddef>

namespace ohmy {

/**
 * @brief Keccak-256 (SHA3) implementation for Ethash
 * 
 * This is the core cryptographic primitive used throughout Ethash:
 * - Seed hash generation
 * - Cache generation
 * - DAG item calculation
 * - Solution verification
 */
class Keccak {
public:
    /**
     * @brief Compute Keccak-256 hash
     * @param data Input data pointer
     * @param len Input data length in bytes
     * @return 32-byte hash
     */
    static hash32_t keccak256(const uint8_t* data, size_t len);
    
    /**
     * @brief Compute Keccak-256 hash from hash32_t
     * @param input Input hash
     * @return 32-byte hash
     */
    static hash32_t keccak256(const hash32_t& input);
    
    /**
     * @brief Compute Keccak-512 hash (used for cache generation)
     * @param data Input data pointer
     * @param len Input data length in bytes
     * @return 64-byte hash
     */
    static hash64_t keccak512(const uint8_t* data, size_t len);
    
    /**
     * @brief Compute Keccak-512 hash from hash64_t
     * @param input Input hash
     * @return 64-byte hash
     */
    static hash64_t keccak512(const hash64_t& input);

private:
    // Keccak state is 1600 bits = 200 bytes = 25 uint64_t words
    static constexpr size_t STATE_SIZE = 25;
    
    /**
     * @brief Keccak-f[1600] permutation function
     * @param state 25-word state array (modified in-place)
     */
    static void keccak_f1600(uint64_t state[STATE_SIZE]);
    
    /**
     * @brief Generic Keccak hash function
     * @param data Input data
     * @param len Input length
     * @param output Output buffer
     * @param outputLen Output length (32 for SHA3-256, 64 for SHA3-512)
     */
    static void keccak(const uint8_t* data, size_t len, uint8_t* output, size_t outputLen);
    
    // Round constants for iota step
    static const uint64_t ROUND_CONSTANTS[24];
    
    // Rotation offsets for rho step
    static const int ROTATION_OFFSETS[25];
};

} // namespace ohmy
