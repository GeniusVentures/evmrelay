// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/crypto/kdf.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/crypto/hmac.hpp>
#include <rlpx/crypto/aes.hpp>
#include <vector>
#include <cstring>

using namespace rlpx;
using namespace rlpx::crypto;

// Test KDF (Key Derivation Function)
TEST(CryptoTest, KdfDeriveBasic) {
    std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
    
    auto result = Kdf::derive(secret, 32);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 32);
}

TEST(CryptoTest, KdfDeriveAesKey) {
    std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
    
    auto result = Kdf::derive_aes_key(secret);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), kAesKeySize);
}

TEST(CryptoTest, KdfDeriveMacKey) {
    std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
    
    auto result = Kdf::derive_mac_key(secret);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), kMacKeySize);
}

TEST(CryptoTest, KdfDeriveKeys) {
    std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
    
    auto result = Kdf::derive_keys(secret);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().aes_key.size(), kAesKeySize);
    EXPECT_EQ(result.value().mac_key.size(), kMacKeySize);
}

TEST(CryptoTest, KdfEmptySecret) {
    std::vector<uint8_t> empty_secret;
    
    auto result = Kdf::derive(empty_secret, 32);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CryptoError::kKdfFailed);
}

// Test ECDH (Elliptic Curve Diffie-Hellman)
TEST(CryptoTest, EcdhGenerateKeypair) {
    auto result = Ecdh::generate_ephemeral_keypair();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().public_key.size(), kPublicKeySize);
    EXPECT_EQ(result.value().private_key.size(), kPrivateKeySize);
    
    // Verify public key is valid
    EXPECT_TRUE(Ecdh::verify_public_key(result.value().public_key));
}

TEST(CryptoTest, EcdhSharedSecret) {
    // Generate two keypairs
    auto alice_keypair = Ecdh::generate_ephemeral_keypair();
    auto bob_keypair = Ecdh::generate_ephemeral_keypair();
    
    ASSERT_TRUE(alice_keypair.has_value());
    ASSERT_TRUE(bob_keypair.has_value());
    
    // Compute shared secrets
    auto alice_shared = Ecdh::compute_shared_secret(
        bob_keypair.value().public_key,
        alice_keypair.value().private_key
    );
    
    auto bob_shared = Ecdh::compute_shared_secret(
        alice_keypair.value().public_key,
        bob_keypair.value().private_key
    );
    
    ASSERT_TRUE(alice_shared.has_value());
    ASSERT_TRUE(bob_shared.has_value());
    
    // Shared secrets should match
    EXPECT_EQ(alice_shared.value(), bob_shared.value());
}

TEST(CryptoTest, EcdhInvalidPublicKey) {
    PublicKey invalid_key{};  // All zeros
    PrivateKey private_key{};
    private_key.fill(0x01);
    
    auto result = Ecdh::compute_shared_secret(invalid_key, private_key);
    
    EXPECT_FALSE(result.has_value());
}

// Test HMAC
TEST(CryptoTest, HmacCompute) {
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> data = {0x05, 0x06, 0x07, 0x08};
    
    auto result = Hmac::compute(key, data);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 32);  // SHA-256 output
}

TEST(CryptoTest, HmacComputeMac) {
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> data = {0x05, 0x06, 0x07, 0x08};
    
    auto result = Hmac::compute_mac(key, data);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), kMacSize);
}

TEST(CryptoTest, HmacVerify) {
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> data = {0x05, 0x06, 0x07, 0x08};
    
    auto mac_result = Hmac::compute(key, data);
    ASSERT_TRUE(mac_result.has_value());
    
    bool valid = Hmac::verify(key, data, mac_result.value());
    EXPECT_TRUE(valid);
    
    // Test with wrong MAC
    std::vector<uint8_t> wrong_mac(32, 0xFF);
    bool invalid = Hmac::verify(key, data, wrong_mac);
    EXPECT_FALSE(invalid);
}

TEST(CryptoTest, HmacEmptyKey) {
    std::vector<uint8_t> empty_key;
    std::vector<uint8_t> data = {0x05, 0x06, 0x07, 0x08};
    
    auto result = Hmac::compute(empty_key, data);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CryptoError::kInvalidKeySize);
}

// Test AES
TEST(CryptoTest, AesEncryptDecrypt) {
    AesKey key;
    key.fill(0x42);
    
    std::array<uint8_t, kAesBlockSize> iv;
    iv.fill(0x00);
    
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    auto encrypted = Aes::encrypt_ctr(key, iv, plaintext);
    ASSERT_TRUE(encrypted.has_value());
    EXPECT_EQ(encrypted.value().size(), plaintext.size());
    
    auto decrypted = Aes::decrypt_ctr(key, iv, encrypted.value());
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), plaintext);
}

TEST(CryptoTest, AesInplaceEncryptDecrypt) {
    AesKey key;
    key.fill(0x42);
    
    std::array<uint8_t, kAesBlockSize> iv;
    iv.fill(0x00);
    
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    std::vector<uint8_t> original = data;
    
    auto encrypt_result = Aes::encrypt_ctr_inplace(key, iv, MutableByteView(data.data(), data.size()));
    ASSERT_TRUE(encrypt_result.has_value());
    
    // Reset IV for decryption
    iv.fill(0x00);
    
    auto decrypt_result = Aes::decrypt_ctr_inplace(key, iv, MutableByteView(data.data(), data.size()));
    ASSERT_TRUE(decrypt_result.has_value());
    
    EXPECT_EQ(data, original);
}

TEST(CryptoTest, AesEmptyData) {
    AesKey key;
    key.fill(0x42);
    
    std::array<uint8_t, kAesBlockSize> iv;
    iv.fill(0x00);
    
    std::vector<uint8_t> empty_data;
    
    auto result = Aes::encrypt_ctr(key, iv, empty_data);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

// Test crypto integration
TEST(CryptoTest, FullCryptoFlow) {
    // Generate keypairs
    auto alice_keypair = Ecdh::generate_ephemeral_keypair();
    auto bob_keypair = Ecdh::generate_ephemeral_keypair();
    
    ASSERT_TRUE(alice_keypair.has_value());
    ASSERT_TRUE(bob_keypair.has_value());
    
    // Compute shared secret
    auto shared_secret = Ecdh::compute_shared_secret(
        bob_keypair.value().public_key,
        alice_keypair.value().private_key
    );
    
    ASSERT_TRUE(shared_secret.has_value());
    
    // Derive keys
    auto keys = Kdf::derive_keys(shared_secret.value());
    ASSERT_TRUE(keys.has_value());
    
    // Encrypt data
    std::array<uint8_t, kAesBlockSize> iv{};
    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};  // "Hello"
    
    auto ciphertext = Aes::encrypt_ctr(keys.value().aes_key, iv, plaintext);
    ASSERT_TRUE(ciphertext.has_value());
    
    // Compute MAC
    auto mac = Hmac::compute_mac(keys.value().mac_key, ciphertext.value());
    ASSERT_TRUE(mac.has_value());
    
    // Verify MAC
    bool valid = Hmac::verify(keys.value().mac_key, ciphertext.value(), mac.value());
    EXPECT_TRUE(valid);
    
    // Decrypt
    iv.fill(0x00);
    auto decrypted = Aes::decrypt_ctr(keys.value().aes_key, iv, ciphertext.value());
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), plaintext);
}
