#include <iostream>
#include <iomanip>
#include <cstring>
#include <array>
#include <vector>
#include <string>
#include <cstdint>

// Include Keccak header
#include "ohmy/keccak.hpp"

using hash32_t = std::array<uint8_t, 32>;

void print_hex(const char* label, const uint8_t* data, size_t len) {
    std::cout << label << ": ";
    for (size_t i = 0; i < len; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    std::cout << std::dec << std::endl;
}

bool hex_to_bytes(const std::string& hex, uint8_t* bytes, size_t max_bytes) {
    std::string str = hex;
    if (str.size() >= 2 && str[0] == '0' && str[1] == 'x') {
        str = str.substr(2);
    }
    
    size_t expected_len = max_bytes * 2;
    if (str.size() < expected_len) {
        str = std::string(expected_len - str.size(), '0') + str;
    }
    
    for (size_t i = 0; i < max_bytes; i++) {
        if (i * 2 + 1 >= str.size()) return false;
        
        char high = str[i * 2];
        char low = str[i * 2 + 1];
        
        int h = (high >= '0' && high <= '9') ? high - '0' : 
                (high >= 'a' && high <= 'f') ? high - 'a' + 10 :
                (high >= 'A' && high <= 'F') ? high - 'A' + 10 : -1;
        int l = (low >= '0' && low <= '9') ? low - '0' : 
                (low >= 'a' && low <= 'f') ? low - 'a' + 10 :
                (low >= 'A' && low <= 'F') ? low - 'A' + 10 : -1;
        
        if (h < 0 || l < 0) return false;
        bytes[i] = (h << 4) | l;
    }
    return true;
}

// FNV-1a 32-bit hash (used in Ethash)
uint32_t fnv1a_32(uint32_t a, uint32_t b) {
    return (a * 0x01000193) ^ b;
}

int main() {
    std::cout << "=== Verificação manual do Ethash ===" << std::endl;
    
    // Dados reais do log de mineração
    std::string seedHex = "0x030680a900c7adb0e5b48c35c26b5ef914b7b31f25d4f7f9b63b4de9b17ff2bb81";
    std::string headerHex = "0xa8b6978b3d2ad3a9bd1f6b74bc7a6b39d4ab3c7ff2b8a14b8d5c89f6b0b8b9be";
    std::string nonce_str = "0x1234567890abcdef"; // Nonce de teste
    
    hash32_t seed, header;
    uint64_t nonce = 0x1234567890abcdefULL;
    
    if (!hex_to_bytes(seedHex, seed.data(), 32)) {
        std::cout << "Erro: Não conseguiu parsear seed" << std::endl;
        return 1;
    }
    
    if (!hex_to_bytes(headerHex, header.data(), 32)) {
        std::cout << "Erro: Não conseguiu parsear header" << std::endl;
        return 1;
    }
    
    print_hex("Seed", seed.data(), 32);
    print_hex("Header", header.data(), 32);
    std::cout << "Nonce: 0x" << std::hex << nonce << std::dec << std::endl;
    
    // Passo 1: Construir headerNonce (header + nonce little-endian)
    uint8_t headerNonce[40];
    std::memcpy(headerNonce, header.data(), 32);
    for (int i = 0; i < 8; i++) {
        headerNonce[32 + i] = (nonce >> (i * 8)) & 0xFF;
    }
    
    print_hex("HeaderNonce", headerNonce, 40);
    
    // Passo 2: Calcular seed inicial = Keccak256(headerNonce)
    hash32_t initialSeed = ohmy::Keccak::keccak256(headerNonce, 40);
    print_hex("InitialSeed", initialSeed.data(), 32);
    
    // Passo 3: Criar mix inicial (128 bytes = seed replicado 4x)
    uint32_t mix[32]; // 128 bytes = 32 x uint32_t
    const uint32_t* seedWords = reinterpret_cast<const uint32_t*>(initialSeed.data());
    for (int i = 0; i < 32; i++) {
        mix[i] = seedWords[i % 8]; // Replica o seed de 32 bytes
    }
    
    std::cout << "Mix inicial (primeiros 8 words): ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << mix[i] << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Para um teste completo precisaríamos do DAG, mas vamos simular
    std::cout << "\nNOTA: Para verificação completa, precisamos do DAG da época 778" << std::endl;
    std::cout << "Este teste verificou apenas os primeiros passos do algoritmo Ethash" << std::endl;
    
    // Vamos verificar se o header está sendo interpretado corretamente
    std::cout << "\n=== Análise do Header ===" << std::endl;
    std::cout << "Header como string: " << headerHex << std::endl;
    std::cout << "Tamanho da string: " << headerHex.size() << " chars" << std::endl;
    std::cout << "Bytes esperados: 32 (64 hex chars após remoção do 0x)" << std::endl;
    
    if (headerHex.size() == 66) { // 0x + 64 chars
        std::cout << "✓ Header tem tamanho correto" << std::endl;
    } else {
        std::cout << "✗ Header tem tamanho incorreto!" << std::endl;
    }
    
    return 0;
}