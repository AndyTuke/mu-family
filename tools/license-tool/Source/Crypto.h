#pragma once
#include <monocypher.h>
#include <cstdint>
#include <cstring>
#include <random>

// Thin Monocypher (Ed25519) wrappers for the license tool. The plugin verifies with the
// SAME library (mu_core::LicenseManager → crypto_eddsa_check), so a signature produced here
// validates there by construction — no cross-implementation drift (the OpenSSL signer this
// replaces was a separate Ed25519 implementation, which was the system's fragile seam).
namespace mulic
{

// Fill 32 bytes from the OS CSPRNG. std::random_device is non-deterministic on MSVC
// (RtlGenRandom-backed) — adequate entropy for a one-off signing-key seed.
inline void randomSeed(uint8_t seed[32])
{
    std::random_device rd;
    for (int i = 0; i < 32; )
    {
        const unsigned int r = rd();
        for (int b = 0; b < 4 && i < 32; ++b, ++i)
            seed[i] = (uint8_t) (r >> (8 * b));
    }
}

// Derive an Ed25519 keypair from a fresh random seed. We persist the 64-byte secret key
// (not the seed — crypto_eddsa_key_pair wipes the seed buffer) and embed the 32-byte public
// key in each product's LicenseKey.h.
inline void generateKeypair(uint8_t publicKey[32], uint8_t secretKey[64])
{
    uint8_t seed[32];
    randomSeed(seed);
    crypto_eddsa_key_pair(secretKey, publicKey, seed); // wipes `seed`
}

// Sign a message with the 64-byte secret key. Output is the 64-byte Ed25519 signature.
inline void sign(const uint8_t secretKey[64], const void* msg, size_t len, uint8_t signature[64])
{
    crypto_eddsa_sign(signature, secretKey, static_cast<const uint8_t*>(msg), len);
}

} // namespace mulic
