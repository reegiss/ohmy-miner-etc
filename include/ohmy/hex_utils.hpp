#pragma once

#include "types.hpp"
#include <string>
#include <cstdint>

namespace ohmy {
namespace utils {

/**
 * @brief Utilities for hexadecimal string conversion
 */
class HexUtils {
public:
    /**
     * @brief Convert hex string to hash32_t
     * @param hexStr Hex string (with or without 0x prefix)
     * @param hash Output hash32_t array
     * @return true if conversion successful
     */
    static bool hexToHash32(const std::string& hexStr, hash32_t& hash);

    /**
     * @brief Convert hex string to hash64_t
     * @param hexStr Hex string (with or without 0x prefix)
     * @param hash Output hash64_t array
     * @return true if conversion successful
     */
    static bool hexToHash64(const std::string& hexStr, hash64_t& hash);

    /**
     * @brief Convert hash32_t to hex string
     * @param hash Input hash32_t array
     * @param withPrefix Add "0x" prefix if true
     * @return Hex string representation
     */
    static std::string hash32ToHex(const hash32_t& hash, bool withPrefix = true);

    /**
     * @brief Convert hash64_t to hex string
     * @param hash Input hash64_t array
     * @param withPrefix Add "0x" prefix if true
     * @return Hex string representation
     */
    static std::string hash64ToHex(const hash64_t& hash, bool withPrefix = true);

    /**
     * @brief Convert hex string to uint64_t
     * @param hexStr Hex string (with or without 0x prefix)
     * @param value Output value
     * @return true if conversion successful
     */
    static bool hexToUint64(const std::string& hexStr, uint64_t& value);

    /**
     * @brief Convert uint64_t to hex string
     * @param value Input value
     * @param withPrefix Add "0x" prefix if true
     * @return Hex string representation
     */
    static std::string uint64ToHex(uint64_t value, bool withPrefix = true);

private:
    /**
     * @brief Convert single hex character to value
     */
    static int hexCharToValue(char c);

    /**
     * @brief Convert value to hex character
     */
    static char valueToHexChar(int value);
};

} // namespace utils
} // namespace ohmy
