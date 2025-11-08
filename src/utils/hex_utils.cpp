#include "ohmy/hex_utils.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace ohmy {
namespace utils {

int HexUtils::hexCharToValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char HexUtils::valueToHexChar(int value) {
    if (value >= 0 && value <= 9) return '0' + value;
    if (value >= 10 && value <= 15) return 'a' + (value - 10);
    return '0';
}

bool HexUtils::hexToHash32(const std::string& hexStr, hash32_t& hash) {
    std::string str = hexStr;
    
    // Remove 0x prefix if present
    if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str = str.substr(2);
    }
    
    // Pad with zeros if needed (hash32 = 32 bytes = 64 hex chars)
    if (str.size() < 64) {
        str = std::string(64 - str.size(), '0') + str;
    }
    
    if (str.size() != 64) {
        return false; // Invalid size
    }
    
    // Convert pairs of hex chars to bytes
    for (size_t i = 0; i < 32; ++i) {
        int high = hexCharToValue(str[i * 2]);
        int low = hexCharToValue(str[i * 2 + 1]);
        
        if (high < 0 || low < 0) {
            return false; // Invalid hex character
        }
        
        hash[i] = static_cast<uint8_t>((high << 4) | low);
    }
    
    return true;
}

bool HexUtils::hexToHash64(const std::string& hexStr, hash64_t& hash) {
    std::string str = hexStr;
    
    // Remove 0x prefix if present
    if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str = str.substr(2);
    }
    
    // Pad with zeros if needed (hash64 = 64 bytes = 128 hex chars)
    if (str.size() < 128) {
        str = std::string(128 - str.size(), '0') + str;
    }
    
    if (str.size() != 128) {
        return false; // Invalid size
    }
    
    // Convert pairs of hex chars to bytes
    for (size_t i = 0; i < 64; ++i) {
        int high = hexCharToValue(str[i * 2]);
        int low = hexCharToValue(str[i * 2 + 1]);
        
        if (high < 0 || low < 0) {
            return false; // Invalid hex character
        }
        
        hash[i] = static_cast<uint8_t>((high << 4) | low);
    }
    
    return true;
}

std::string HexUtils::hash32ToHex(const hash32_t& hash, bool withPrefix) {
    std::ostringstream oss;
    
    if (withPrefix) {
        oss << "0x";
    }
    
    for (size_t i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(hash[i]);
    }
    
    return oss.str();
}

std::string HexUtils::hash64ToHex(const hash64_t& hash, bool withPrefix) {
    std::ostringstream oss;
    
    if (withPrefix) {
        oss << "0x";
    }
    
    for (size_t i = 0; i < 64; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(hash[i]);
    }
    
    return oss.str();
}

bool HexUtils::hexToUint64(const std::string& hexStr, uint64_t& value) {
    std::string str = hexStr;
    
    // Remove 0x prefix if present
    if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str = str.substr(2);
    }
    
    if (str.empty() || str.size() > 16) {
        return false; // Invalid size for uint64
    }
    
    value = 0;
    for (char c : str) {
        int val = hexCharToValue(c);
        if (val < 0) {
            return false; // Invalid hex character
        }
        value = (value << 4) | val;
    }
    
    return true;
}

std::string HexUtils::uint64ToHex(uint64_t value, bool withPrefix) {
    std::ostringstream oss;
    
    if (withPrefix) {
        oss << "0x";
    }
    
    oss << std::hex << value;
    
    return oss.str();
}

} // namespace utils
} // namespace ohmy
