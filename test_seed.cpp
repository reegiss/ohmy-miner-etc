#include <iostream>
#include <iomanip>
#include "ohmy/keccak.hpp"
#include "ohmy/types.hpp"

int main() {
    // Calculate seedHash for epoch 778 (ECIP-1099 seed epoch for DAG epoch 389)
    ohmy::hash32_t seedHash{};
    seedHash.fill(0);
    
    // Epoch 778 = 778 iterations of Keccak256
    for (uint32_t i = 0; i < 778; ++i) {
        seedHash = ohmy::Keccak::keccak256(seedHash);
    }
    
    // Print result
    std::cout << "Seed hash for epoch 778: ";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)seedHash[i];
    }
    std::cout << std::endl;
    
    // Expected from pool
    std::cout << "Pool seed hash:          89976f13aa62c077c2b43263bf551a5a61944a7f78e6ca1739bb85df7c6ad866" << std::endl;
    
    return 0;
}
