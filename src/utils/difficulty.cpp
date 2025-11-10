#include "ohmy/difficulty.hpp"
#include <cstring>
#include <algorithm>

namespace ohmy {
namespace utils {

void targetFromDifficulty(uint64_t difficulty, uint8_t target[32]) {
    if (difficulty == 1) {
        std::fill(target, target + 32, 0xFF);
    } else if (difficulty == 2) {
        std::fill(target, target + 32, 0);
        target[0] = 0x80;
    } else {
        // Not implemented, default to all FF
        std::fill(target, target + 32, 0xFF);
    }
}

} // namespace utils
} // namespace ohmy
