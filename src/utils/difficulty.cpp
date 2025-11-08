#include "ohmy/difficulty.hpp"
#include <cstring>

namespace ohmy {
namespace utils {

void targetFromDifficulty(uint64_t difficulty, uint8_t outTargetBE[32]) {
    // Max target = 2^256 - 1 (all 0xFF)
    if (difficulty == 0) {
        std::memset(outTargetBE, 0xFF, 32);
        return;
    }

    // Ethash share difficulty (Stratum): expected_hashes_to_share = difficulty * 2^32.
    // Therefore boundary target = floor((2^256 - 1) / (difficulty * 2^32)).
    // This is equivalent to floor((2^224 - 1) / difficulty).
    __uint128_t divisor = static_cast<__uint128_t>(difficulty) << 32; // multiply by 2^32
    if (divisor == 0) {
        std::memset(outTargetBE, 0xFF, 32);
        return;
    }

    // Represent max 256-bit value as 4 x uint64_t (big-endian limbs for division)
    const uint64_t maxLimbs[4] = {
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL
    };
    uint64_t quotient[4] = {0,0,0,0};
    __uint128_t rem = 0;
    // Long division across big-endian limbs by a 128-bit divisor
    for (int i = 0; i < 4; ++i) {
        __uint128_t cur = (rem << 64) | maxLimbs[i];
        uint64_t q = static_cast<uint64_t>(cur / divisor);
        rem = cur % divisor;
        quotient[i] = q;
    }
    // Convert quotient (big-endian limbs) to big-endian bytes
    for (int limb = 0; limb < 4; ++limb) {
        uint64_t v = quotient[limb];
        for (int b = 0; b < 8; ++b) {
            outTargetBE[limb * 8 + b] = static_cast<uint8_t>(v >> (56 - 8 * b));
        }
    }
    // If difficulty extremely large, quotient may be zero already (all bytes 0)
}

} // namespace utils
} // namespace ohmy
