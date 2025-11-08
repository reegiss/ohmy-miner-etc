#pragma once

#include <cstdint>

namespace ohmy {
namespace utils {

/**
 * @brief Compute 256-bit target boundary from a 64-bit share difficulty.
 * Pools using EthereumStratum/1.0 define share target as:
 *   target = floor((2^256 - 1) / (difficulty * 2^32)).
 * Result is a big-endian 32-byte array. If difficulty is 0, returns all 0xFF.
 */
void targetFromDifficulty(uint64_t difficulty, uint8_t outTargetBE[32]);

} // namespace utils
} // namespace ohmy
