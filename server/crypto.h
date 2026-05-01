#pragma once
// Crypto utilities for protocol v2.1 (demonstration-grade, not production-grade).
// Provides XOR encrypt/decrypt, hex encode/decode, and SHA-256 via Windows CNG.

#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <memory>

#pragma comment(lib, "bcrypt.lib")

// XOR-encrypts plaintext with a repeating key.
inline std::string xorEncrypt(const std::string& plaintext, const std::string& key) {
    if (key.empty()) return plaintext;
    std::string out(plaintext.size(), '\0');
    for (size_t i = 0; i < plaintext.size(); ++i)
        out[i] = static_cast<char>(
            static_cast<unsigned char>(plaintext[i]) ^
            static_cast<unsigned char>(key[i % key.size()])
        );
    return out;
}

// XOR-decrypts ciphertext with a repeating key (identical to xorEncrypt).
inline std::string xorDecrypt(const std::string& ciphertext, const std::string& key) {
    return xorEncrypt(ciphertext, key);
}

// Encodes a byte string as a lowercase hex string (2 chars per byte).
inline std::string hexEncode(const std::string& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : bytes)
        oss << std::setw(2) << static_cast<unsigned>(c);
    return oss.str();
}

// Decodes a lowercase (or uppercase) hex string to bytes.
// Returns "" on invalid input (odd length or non-hex characters).
inline std::string hexDecode(const std::string& hex) {
    if (hex.size() % 2 != 0) return "";
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

// RAII wrapper for BCRYPT_ALG_HANDLE.
struct BcryptAlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~BcryptAlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};

// RAII wrapper for BCRYPT_HASH_HANDLE.
struct BcryptHashHandle {
    BCRYPT_HASH_HANDLE h = nullptr;
    ~BcryptHashHandle() { if (h) BCryptDestroyHash(h); }
};

// Computes the SHA-256 hash of input and returns a 64-character lowercase hex string.
// Returns "" on failure.
inline std::string sha256Hex(const std::string& input) {
    BcryptAlgHandle alg;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return "";

    BcryptHashHandle hash;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg.h, &hash.h, nullptr, 0, nullptr, 0, 0)))
        return "";

    if (!BCRYPT_SUCCESS(BCryptHashData(
            hash.h,
            reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
            static_cast<ULONG>(input.size()),
            0)))
        return "";

    unsigned char digest[32];
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.h, digest, sizeof(digest), 0)))
        return "";

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : digest)
        oss << std::setw(2) << static_cast<unsigned>(b);
    return oss.str();
}
