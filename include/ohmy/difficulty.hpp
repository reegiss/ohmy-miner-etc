#pragma once

#include <cstdint>

namespace ohmy {
namespace utils {

/**
 * @brief Compute 256-bit target boundary from a 64-bit share difficulty.
 * Ethereum Stratum share target = floor((2^256 - 1) / difficulty).
 * Result is a big-endian 32-byte array. If difficulty is 0, returns all 0xFF.
 */
void targetFromDifficulty(uint64_t difficulty, uint8_t outTargetBE[32]);

} // namespace utils
} // namespace ohmy
