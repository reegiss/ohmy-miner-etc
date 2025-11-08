#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ohmy {

// Common types
using hash32_t = std::array<uint8_t, 32>;
using hash64_t = std::array<uint8_t, 64>;

// Mining job structure
struct MiningJob {
    std::string jobId;
    hash32_t seedHash;
    hash32_t headerHash;
    uint64_t target;
    uint64_t blockNumber;
    uint32_t epoch;
};

// Mining solution structure
struct Solution {
    uint64_t nonce;
    hash32_t mixHash;
    hash32_t result;
    std::string jobId;  // Job ID this solution is for
};

// Mining statistics
struct MiningStats {
    uint64_t hashRate;          // Hashes per second
    uint64_t totalHashes;       // Total hashes computed
    uint32_t acceptedShares;    // Accepted solutions
    uint32_t rejectedShares;    // Rejected solutions
    double uptime;              // Uptime in seconds
};

} // namespace ohmy
