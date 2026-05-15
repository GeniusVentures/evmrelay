// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file enr_request_test.cpp
/// @brief Unit tests for discv4_enr_request wire encode.
///
/// Mirrors go-ethereum v4wire_test.go coverage for ENRRequest.

#include <gtest/gtest.h>
#include "discv4/discv4_enr_request.hpp"
#include "discv4/discv4_constants.hpp"
#include <rlp/rlp_decoder.hpp>
#include <cstdint>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Decode the RLP payload portion of a discv4_enr_request wire buffer.
///        Expects: packet-type(1) || RLP([expiration_uint64])
struct DecodedEnrRequest
{
    uint8_t  packet_type  = 0U;
    uint64_t expiration   = 0U;
};

DecodedEnrRequest decode_enr_request( const std::vector<uint8_t>& wire )
{
    DecodedEnrRequest result;
    EXPECT_FALSE( wire.empty() );

    result.packet_type = wire[0];

    rlp::ByteView bv( wire.data() + 1, wire.size() - 1 );
    rlp::RlpDecoder decoder( bv );

    auto list_len = decoder.ReadListHeaderBytes();
    EXPECT_TRUE( list_len.has_value() ) << "Expected outer list header";

    EXPECT_TRUE( decoder.read( result.expiration ) ) << "Expected expiration uint64";

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// ENRRequest encode tests
// ---------------------------------------------------------------------------

/// @brief RlpPayload() returns a non-empty buffer.
TEST( EnrRequestEncodeTest, PayloadIsNonEmpty )
{
    discv4::discv4_enr_request req;
    req.expiration = 1000U;

    const auto wire = req.RlpPayload();
    EXPECT_FALSE( wire.empty() );
}

/// @brief First byte is the ENRRequest packet type (0x05).
TEST( EnrRequestEncodeTest, FirstByteIsPacketType )
{
    discv4::discv4_enr_request req;
    req.expiration = 1000U;

    const auto wire = req.RlpPayload();
    ASSERT_FALSE( wire.empty() );
    EXPECT_EQ( wire[0], discv4::kPacketTypeEnrRequest );
}

/// @brief Expiration round-trips through RLP encode → decode.
TEST( EnrRequestEncodeTest, ExpirationRoundTrips )
{
    constexpr uint64_t kExpiry = 9'999'999'999ULL;

    discv4::discv4_enr_request req;
    req.expiration = kExpiry;

    const auto wire    = req.RlpPayload();
    const auto decoded = decode_enr_request( wire );

    EXPECT_EQ( decoded.packet_type, discv4::kPacketTypeEnrRequest );
    EXPECT_EQ( decoded.expiration,  kExpiry );
}

/// @brief Zero expiration encodes and decodes correctly.
TEST( EnrRequestEncodeTest, ZeroExpirationRoundTrips )
{
    discv4::discv4_enr_request req;
    req.expiration = 0U;

    const auto wire    = req.RlpPayload();
    const auto decoded = decode_enr_request( wire );

    EXPECT_EQ( decoded.packet_type, discv4::kPacketTypeEnrRequest );
    EXPECT_EQ( decoded.expiration,  0U );
}

/// @brief Two requests with different expirations produce different payloads.
TEST( EnrRequestEncodeTest, DifferentExpirationsProduceDifferentPayloads )
{
    discv4::discv4_enr_request req1;
    req1.expiration = 100U;

    discv4::discv4_enr_request req2;
    req2.expiration = 200U;

    EXPECT_NE( req1.RlpPayload(), req2.RlpPayload() );
}

