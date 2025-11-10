#pragma once
#ifndef KECCAK_DEV_CUH
#define KECCAK_DEV_CUH

#include <cuda_runtime.h>
#include <cstdint>
#include <cstring>

namespace ohmy {
namespace cuda {

__device__ __forceinline__ uint64_t rotl64_dev(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

// Some NVCC versions emit warning 20044 for extern __constant__ declarations.
// Suppress it locally; definitions live in keccak_constants.cu.
#ifdef __CUDACC__
#pragma diag_suppress 20044
#endif
// FNV prime constant - defined in keccak_constants.cu
extern __device__ __constant__ uint32_t c_fnv_prime;

// Keccak constants - defined in keccak_constants.cu
extern __device__ __constant__ uint64_t c_keccak_round_constants[24];

extern __device__ __constant__ int c_keccak_rotation_offsets[25];
#ifdef __CUDACC__
#pragma diag_default 20044
#endif

__device__ inline void keccak_f1600_dev(uint64_t state[25]) {
    for (int round = 0; round < 24; ++round) {
        uint64_t C[5], D[5];
        #pragma unroll
        for (int x = 0; x < 5; ++x) {
            C[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        #pragma unroll
        for (int x = 0; x < 5; ++x) {
            D[x] = C[(x + 4) % 5] ^ rotl64_dev(C[(x + 1) % 5], 1);
        }
        #pragma unroll
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                state[x + 5 * y] ^= D[x];
            }
        }
        uint64_t B[25];
        #pragma unroll
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                B[y + 5 * ((2 * x + 3 * y) % 5)] = rotl64_dev(state[x + 5 * y], c_keccak_rotation_offsets[x + 5 * y]);
            }
        }
        #pragma unroll
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < 5; ++y) {
                state[x + 5 * y] = B[x + 5 * y] ^ ((~B[(x + 1) % 5 + 5 * y]) & B[(x + 2) % 5 + 5 * y]);
            }
        }
        state[0] ^= c_keccak_round_constants[round];
    }
}

__device__ inline void keccak_absorb_squeeze(const uint8_t* data, size_t len, uint8_t* out, size_t outLen) {
    const size_t rate = 200 - 2 * outLen;
    uint64_t state[25];
    #pragma unroll
    for (int i = 0; i < 25; ++i) state[i] = 0;
    uint8_t block[200];
    while (len >= rate) {
        #pragma unroll
        for (size_t i = 0; i < rate; ++i) {
            block[i] = ((uint8_t*)state)[i] ^ data[i];
        }
        memcpy(state, block, rate);
        keccak_f1600_dev(state);
        data += rate;
        len -= rate;
    }
    memset(block, 0, 200);
    if (len > 0) memcpy(block, data, len);
    block[len] ^= 0x01;
    block[rate - 1] ^= 0x80;
    #pragma unroll
    for (size_t i = 0; i < rate; ++i) ((uint8_t*)state)[i] ^= block[i];
    keccak_f1600_dev(state);
    memcpy(out, state, outLen);
}

__device__ inline void keccak256_dev(const uint8_t* data, size_t len, uint8_t out[32]) {
    keccak_absorb_squeeze(data, len, out, 32);
}

__device__ inline void keccak512_dev(const uint8_t* data, size_t len, uint8_t out[64]) {
    keccak_absorb_squeeze(data, len, out, 64);
}

__device__ inline uint32_t fnv1a(uint32_t a, uint32_t b) {
    return a * 0x01000193u ^ b;
}

} // namespace cuda
} // namespace ohmy

#endif // KECCAK_DEV_CUH
