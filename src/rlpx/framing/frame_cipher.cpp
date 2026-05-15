// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// go-ethereum rlpx frame cipher — exact port of sessionState / hashMAC.
// Reference: go-ethereum/p2p/rlpx/rlpx.go

#include <rlpx/framing/frame_cipher.hpp>
#include <base/rlp-logger.hpp>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>
#include <nil/crypto3/hash/accumulators/hash.hpp>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <cstring>

namespace rlpx::framing {

namespace {

    rlp::base::Logger& fc_log()
    {
        static auto log = rlp::base::createLogger("rlpx.frame");
        return log;
    }

    /// Legacy Keccak-256 (not SHA3-256) over a contiguous byte range.
    std::array<uint8_t, 32> keccak256(const uint8_t* data, size_t len) noexcept
    {
        using Hasher = nil::crypto3::hashes::keccak_1600<256>;
        nil::crypto3::accumulator_set<Hasher> acc;
        nil::crypto3::hash<Hasher>(data, data + len, acc);
        auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
        std::array<uint8_t, 32> result{};
        std::copy(digest.begin(), digest.end(), result.begin());
        return result;
    }

    /// AES-256-ECB single-block encrypt — used by hashMAC.compute().
    void aes_ecb_encrypt_block(const uint8_t* key32, const uint8_t* in16,
                                uint8_t* out16) noexcept
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key32, nullptr);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int outl = 0;
        EVP_EncryptUpdate(ctx, out16, &outl, in16, 16);
        EVP_CIPHER_CTX_free(ctx);
    }

    /// go-ethereum hashMAC — exact port.
    ///
    /// go-ethereum keeps a live keccak.NewLegacyKeccak256() accumulator.
    /// We replicate this by keeping ALL bytes written to the hash in a buffer
    /// and recomputing keccak256(buffer) on demand as hash.Sum().
    /// This is equivalent because Keccak is deterministic and we always
    /// hash from the beginning.
    struct HashMAC
    {
        std::array<uint8_t, 32> mac_key{};  ///< = sec.MAC (32-byte mac_secret)
        ByteBuffer              written{};  ///< Accumulates all bytes written to hash

        /// Seed the MAC: go-ethereum does
        ///   mac.Write(xor(MAC, nonce)); mac.Write(auth_or_ack_wire)
        /// We receive the pre-computed digest of that sequence as seed_digest,
        /// AND the original bytes that produced it, so we can replay.
        /// Simpler: we store the seed bytes directly.
        void write(const uint8_t* data, size_t len)
        {
            written.insert(written.end(), data, data + len);
        }

        /// hash.Sum() — keccak256 of everything written so far.
        std::array<uint8_t, 32> sum() const noexcept
        {
            return keccak256(written.data(), written.size());
        }

        /// go-ethereum hashMAC.computeHeader:
        ///   sum1 = hash.Sum(nil)
        ///   return compute(sum1, header[:16])
        std::array<uint8_t, 16> compute_header(const uint8_t* header_ct16) noexcept
        {
            auto sum1 = sum();
            return compute(sum1, header_ct16);
        }

        /// go-ethereum hashMAC.computeFrame:
        ///   hash.Write(framedata)
        ///   seed = hash.Sum(nil)
        ///   return compute(seed, seed[:16])
        std::array<uint8_t, 16> compute_frame(const uint8_t* framedata,
                                               size_t len) noexcept
        {
            write(framedata, len);
            auto seed = sum();
            return compute(seed, seed.data());
        }

        /// go-ethereum hashMAC.compute(sum1, seed16):
        ///   aesBuffer = AES(mac_key, sum1[:16])
        ///   aesBuffer ^= seed16
        ///   hash.Write(aesBuffer)
        ///   sum2 = hash.Sum(nil)
        ///   return sum2[:16]
        std::array<uint8_t, 16> compute(const std::array<uint8_t, 32>& sum1,
                                         const uint8_t* seed16) noexcept
        {
            std::array<uint8_t, 16> aes_buf{};
            aes_ecb_encrypt_block(mac_key.data(), sum1.data(), aes_buf.data());
            for (size_t i = 0; i < 16; ++i)
            {
                aes_buf[i] ^= seed16[i];
            }
            write(aes_buf.data(), 16);
            auto sum2 = sum();
            std::array<uint8_t, 16> result{};
            std::memcpy(result.data(), sum2.data(), 16);
            return result;
        }
    };

    /// AES-256-CTR streaming cipher — go-ethereum cipher.NewCTR with all-zero IV.
    struct AesCtrState
    {
        EVP_CIPHER_CTX* ctx = nullptr;

        void init(const uint8_t* key32) noexcept
        {
            ctx = EVP_CIPHER_CTX_new();
            uint8_t iv[16]{};
            EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key32, iv);
        }

        ~AesCtrState()
        {
            if (ctx) { EVP_CIPHER_CTX_free(ctx); }
        }

        /// CTR is its own inverse — same call for encrypt and decrypt.
        void process(const uint8_t* in, uint8_t* out, size_t len) noexcept
        {
            int outl = 0;
            EVP_EncryptUpdate(ctx, out, &outl, in,
                              static_cast<int>(len));
        }
    };

} // anonymous namespace

// ── FrameCipher pimpl definition ─────────────────────────────────────────────

struct FrameCipher::FrameCipherImpl
{
    AesCtrState enc;
    AesCtrState dec;
    HashMAC     egress_mac;
    HashMAC     ingress_mac;
};

// ── FrameCipher constructor / destructor ──────────────────────────────────────

FrameCipher::FrameCipher(const auth::FrameSecrets& secrets) noexcept
    : secrets_(secrets)
    , impl_(std::make_unique<FrameCipherImpl>())
{
    impl_->enc.init(secrets.aes_secret.data());
    impl_->dec.init(secrets.aes_secret.data());

    impl_->egress_mac.mac_key  = secrets.mac_secret;
    impl_->ingress_mac.mac_key = secrets.mac_secret;
    // Seed written buffers: go-ethereum writes xor(MAC,nonce) then auth/ack wire.
    // derive_frame_secrets stores those exact raw bytes in egress/ingress_mac_seed.
    impl_->egress_mac.write(secrets.egress_mac_seed.data(),
                             secrets.egress_mac_seed.size());
    impl_->ingress_mac.write(secrets.ingress_mac_seed.data(),
                              secrets.ingress_mac_seed.size());
    fc_log()->debug("FrameCipher: egress_seed_len={} ingress_seed_len={}",
                    secrets.egress_mac_seed.size(), secrets.ingress_mac_seed.size());
}

FrameCipher::~FrameCipher() = default;

// ── encrypt_frame ─────────────────────────────────────────────────────────────

FramingResult<ByteBuffer> FrameCipher::encrypt_frame(
    const FrameEncryptParams& params) noexcept
{
    const size_t frame_data_size = static_cast<size_t>(params.frame_data.size());
    if (params.frame_data.empty() || frame_data_size > kMaxFrameSize)
    {
        return FramingError::kInvalidFrameSize;
    }

    const size_t fsize   = frame_data_size;
    const size_t padding = (fsize % kFramePaddingAlignment != 0)
                         ? (kFramePaddingAlignment - (fsize % kFramePaddingAlignment))
                         : 0;
    const size_t rsize   = fsize + padding;

    // Header: 3-byte frame length + fixed RLP header bytes + trailing zeros.
    std::array<uint8_t, kFrameHeaderSize> header{};
    header[kFrameLengthMsbOffset] = static_cast<uint8_t>((fsize >> kFrameLengthMsbShift) & 0xFFU);
    header[kFrameLengthMiddleOffset] = static_cast<uint8_t>((fsize >> kFrameLengthMiddleShift) & 0xFFU);
    header[kFrameLengthLsbOffset] = static_cast<uint8_t>((fsize >> kFrameLengthLsbShift) & 0xFFU);
    std::memcpy(
        header.data() + kFrameHeaderDataOffset,
        kFrameHeaderStaticRlpBytes.data(),
        kFrameHeaderStaticRlpBytes.size());

    std::array<uint8_t, kFrameHeaderSize> header_ct{};
    impl_->enc.process(header.data(), header_ct.data(), kFrameHeaderSize);

    auto header_mac = impl_->egress_mac.compute_header(header_ct.data());

    ByteBuffer frame_padded(rsize, 0);
    std::memcpy(frame_padded.data(), params.frame_data.data(), fsize);

    ByteBuffer frame_ct(rsize);
    impl_->enc.process(frame_padded.data(), frame_ct.data(), rsize);

    auto frame_mac = impl_->egress_mac.compute_frame(frame_ct.data(), rsize);

    ByteBuffer out;
    out.reserve(kFrameHeaderWithMacSize + rsize + kMacSize);
    out.insert(out.end(), header_ct.begin(),  header_ct.end());
    out.insert(out.end(), header_mac.begin(), header_mac.end());
    out.insert(out.end(), frame_ct.begin(),   frame_ct.end());
    out.insert(out.end(), frame_mac.begin(),  frame_mac.end());
    return out;
}

// ── decrypt_header ────────────────────────────────────────────────────────────

FramingResult<size_t> FrameCipher::decrypt_header(
    gsl::span<const uint8_t, kFrameHeaderSize> header_ct,
    gsl::span<const uint8_t, kMacSize>         header_mac_wire) noexcept
{
    auto expected_mac = impl_->ingress_mac.compute_header(header_ct.data());
    if (CRYPTO_memcmp(header_mac_wire.data(), expected_mac.data(), kMacSize) != 0)
    {
        fc_log()->debug("decrypt_header: MAC mismatch — expected={:02x}{:02x}{:02x}{:02x} got={:02x}{:02x}{:02x}{:02x} seed_len={}",
                        expected_mac[0], expected_mac[1], expected_mac[2], expected_mac[3],
                        header_mac_wire[0], header_mac_wire[1], header_mac_wire[2], header_mac_wire[3],
                        impl_->ingress_mac.written.size());
        return FramingError::kMacMismatch;
    }

    std::array<uint8_t, kFrameHeaderSize> header_pt{};
    impl_->dec.process(header_ct.data(), header_pt.data(), kFrameHeaderSize);

    const size_t fsize = (static_cast<size_t>(header_pt[kFrameLengthMsbOffset]) << kFrameLengthMsbShift)
                       | (static_cast<size_t>(header_pt[kFrameLengthMiddleOffset]) << kFrameLengthMiddleShift)
                       | (static_cast<size_t>(header_pt[kFrameLengthLsbOffset]) << kFrameLengthLsbShift);
    if (fsize == 0 || fsize > kMaxFrameSize)
    {
        return FramingError::kInvalidFrameSize;
    }
    return fsize;
}

// ── decrypt_frame ─────────────────────────────────────────────────────────────

FramingResult<ByteBuffer> FrameCipher::decrypt_frame(
    const FrameDecryptParams& params) noexcept
{
    const size_t header_ciphertext_size = static_cast<size_t>(params.header_ciphertext.size());
    const size_t header_mac_size        = static_cast<size_t>(params.header_mac.size());
    const size_t frame_ciphertext_size  = static_cast<size_t>(params.frame_ciphertext.size());
    const size_t frame_mac_size         = static_cast<size_t>(params.frame_mac.size());

    if (header_ciphertext_size < kFrameHeaderSize
        || header_mac_size < kMacSize
        || frame_mac_size < kMacSize)
    {
        return FramingError::kInvalidFrameSize;
    }

    gsl::span<const uint8_t, kFrameHeaderSize> hct(
        params.header_ciphertext.data(), kFrameHeaderSize);
    gsl::span<const uint8_t, kMacSize> hm(
        params.header_mac.data(), kMacSize);

    auto fsize_result = decrypt_header(hct, hm);
    if (!fsize_result) { return fsize_result.error(); }
    const size_t fsize = fsize_result.value();

    if (frame_ciphertext_size < fsize)
    {
        return FramingError::kInvalidFrameSize;
    }

    const size_t rsize = frame_ciphertext_size;
    auto frame_mac_expected = impl_->ingress_mac.compute_frame(
        params.frame_ciphertext.data(), rsize);
    if (CRYPTO_memcmp(params.frame_mac.data(), frame_mac_expected.data(),
                      kMacSize) != 0)
    {
        fc_log()->debug("decrypt_frame: frame MAC mismatch");
        return FramingError::kMacMismatch;
    }

    ByteBuffer frame_pt(rsize);
    impl_->dec.process(params.frame_ciphertext.data(), frame_pt.data(), rsize);
    frame_pt.resize(fsize);
    return frame_pt;
}

// ── decrypt_frame_body ────────────────────────────────────────────────────────

FramingResult<ByteBuffer> FrameCipher::decrypt_frame_body(
    size_t   fsize,
    ByteView frame_ct_padded,
    ByteView frame_mac) noexcept
{
    const size_t rsize          = static_cast<size_t>(frame_ct_padded.size());
    const size_t frame_mac_size = static_cast<size_t>(frame_mac.size());
    if (rsize < fsize || frame_mac_size < kMacSize)
    {
        return FramingError::kInvalidFrameSize;
    }

    auto frame_mac_expected = impl_->ingress_mac.compute_frame(
        frame_ct_padded.data(), rsize);
    if (CRYPTO_memcmp(frame_mac.data(), frame_mac_expected.data(), kMacSize) != 0)
    {
        fc_log()->debug("decrypt_frame_body: frame MAC mismatch");
        return FramingError::kMacMismatch;
    }

    ByteBuffer frame_pt(rsize);
    impl_->dec.process(frame_ct_padded.data(), frame_pt.data(), rsize);
    frame_pt.resize(fsize);
    return frame_pt;
}

// ── legacy stubs ─────────────────────────────────────────────────────────────

void FrameCipher::update_egress_mac(ByteView /*data*/) noexcept {}
void FrameCipher::update_ingress_mac(ByteView /*data*/) noexcept {}
MacDigest FrameCipher::compute_header_mac(ByteView /*hct*/) noexcept { return {}; }
MacDigest FrameCipher::compute_frame_mac(ByteView /*fct*/) noexcept  { return {}; }

} // namespace rlpx::framing
