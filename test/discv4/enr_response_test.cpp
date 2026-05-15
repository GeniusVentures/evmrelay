// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file enr_response_test.cpp
/// @brief Unit tests for discv4_enr_response wire decode.
///
/// Mirrors go-ethereum v4wire_test.go coverage for ENRResponse.
/// Wire fixture is assembled manually (zeroed hash/sig) to stay independent of
/// the signing path — the same approach used by discv4_protocol_test.cpp for PONG.

#include <gtest/gtest.h>
#include "discv4/discv4_enr_response.hpp"
#include "discv4/discv4_constants.hpp"
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_decoder.hpp>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Build a minimal ENR record RLP: RLP([signature_64bytes, seq_uint64])
///        with no key-value pairs — the smallest valid EIP-778 record.
std::vector<uint8_t> make_minimal_enr_record( uint64_t seq = 0U )
{
    rlp::RlpEncoder enc;
    EXPECT_TRUE( enc.BeginList() );

    // Signature — 64 zero bytes (placeholder; Parse() does not verify it).
    const std::array<uint8_t, 64U> sig{};
    EXPECT_TRUE( enc.add( rlp::ByteView( sig.data(), sig.size() ) ) );

    // Sequence number.
    EXPECT_TRUE( enc.add( seq ) );

    EXPECT_TRUE( enc.EndList() );

    auto res = enc.MoveBytes();
    EXPECT_TRUE( res.has_value() );
    return std::vector<uint8_t>( res.value().begin(), res.value().end() );
}

/// @brief Build a full discv4 ENRResponse wire packet (zeroed hash + sig).
///        Layout: hash(32) || sig(65) || type(1) || RLP([reply_tok(32), record])
std::vector<uint8_t> make_enr_response_wire(
    const std::array<uint8_t, discv4::kWireHashSize>& reply_tok,
    const std::vector<uint8_t>&                       record_rlp )
{
    // Build RLP payload: [reply_tok, record]
    rlp::RlpEncoder enc;
    EXPECT_TRUE( enc.BeginList() );
    EXPECT_TRUE( enc.add( rlp::ByteView( reply_tok.data(), reply_tok.size() ) ) );
    // Embed the record as a raw already-encoded RLP item.
    EXPECT_TRUE( enc.AddRaw( rlp::ByteView( record_rlp.data(), record_rlp.size() ) ) );
    EXPECT_TRUE( enc.EndList() );

    auto rlp_res = enc.MoveBytes();
    EXPECT_TRUE( rlp_res.has_value() );

    // Assemble wire packet with zeroed hash and signature.
    std::vector<uint8_t> wire;
    wire.resize( discv4::kWireHashSize, 0U );                 // hash  (32)
    wire.resize( discv4::kWireHashSize + discv4::kWireSigSize, 0U ); // sig   (65)
    wire.push_back( discv4::kPacketTypeEnrResponse );          // type  (1)
    wire.insert( wire.end(),
                 rlp_res.value().begin(),
                 rlp_res.value().end() );                      // payload
    return wire;
}

} // namespace

// ---------------------------------------------------------------------------
// ENRResponse parse tests
// ---------------------------------------------------------------------------

/// @brief Parse() succeeds on a well-formed wire packet.
TEST( EnrResponseParseTest, ParseSucceeds )
{
    const std::array<uint8_t, discv4::kWireHashSize> reply_tok{};
    const auto record = make_minimal_enr_record();
    const auto wire   = make_enr_response_wire( reply_tok, record );

    const rlp::ByteView bv( wire.data(), wire.size() );
    auto result = discv4::discv4_enr_response::Parse( bv );

    ASSERT_TRUE( result.has_value() ) << "Parse() must succeed on a valid ENRResponse wire packet";
}

/// @brief request_hash round-trips through encode → parse.
TEST( EnrResponseParseTest, RequestHashRoundTrips )
{
    std::array<uint8_t, discv4::kWireHashSize> reply_tok{};
    // Fill with a recognisable pattern.
    for ( size_t i = 0U; i < reply_tok.size(); ++i )
    {
        reply_tok[i] = static_cast<uint8_t>( i + 1U );
    }

    const auto record = make_minimal_enr_record();
    const auto wire   = make_enr_response_wire( reply_tok, record );

    const rlp::ByteView bv( wire.data(), wire.size() );
    auto result = discv4::discv4_enr_response::Parse( bv );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().request_hash, reply_tok );
}

/// @brief record_rlp contains the expected raw bytes of the ENR record.
TEST( EnrResponseParseTest, RecordRlpRoundTrips )
{
    const std::array<uint8_t, discv4::kWireHashSize> reply_tok{};
    const auto record = make_minimal_enr_record( 42U );
    const auto wire   = make_enr_response_wire( reply_tok, record );

    const rlp::ByteView bv( wire.data(), wire.size() );
    auto result = discv4::discv4_enr_response::Parse( bv );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().record_rlp, record );
}

/// @brief Parse() rejects a packet whose type byte is not 0x06.
TEST( EnrResponseParseTest, WrongPacketTypeIsRejected )
{
    const std::array<uint8_t, discv4::kWireHashSize> reply_tok{};
    const auto record = make_minimal_enr_record();
    auto wire         = make_enr_response_wire( reply_tok, record );

    // Corrupt the packet-type byte.
    wire[discv4::kWirePacketTypeOffset] = discv4::kPacketTypePong;

    const rlp::ByteView bv( wire.data(), wire.size() );
    auto result = discv4::discv4_enr_response::Parse( bv );

    EXPECT_FALSE( result.has_value() ) << "Parse() must reject wrong packet type";
}

/// @brief Parse() rejects a buffer that is too short to contain a wire header.
TEST( EnrResponseParseTest, TooShortInputIsRejected )
{
    // Anything shorter than kWireHeaderSize + 1 must fail.
    const std::vector<uint8_t> too_short( discv4::kWireHeaderSize - 1U, 0U );
    const rlp::ByteView bv( too_short.data(), too_short.size() );

    auto result = discv4::discv4_enr_response::Parse( bv );

    EXPECT_FALSE( result.has_value() ) << "Parse() must reject truncated input";
}

// ---------------------------------------------------------------------------
// ParseEthForkId tests
// ---------------------------------------------------------------------------

/// @brief Build an ENR record with an `eth` entry containing the given ForkId.
///
/// ENR record: RLP([sig_64, seq, "eth", RLP(enrEntry)])
/// enrEntry value:  RLP([ RLP([hash4, next_uint64]) ])
///   outer list = enrEntry struct fields
///   inner list = ForkId struct fields
std::vector<uint8_t> make_enr_record_with_eth( const discv4::ForkId& fork_id, uint64_t seq = 1U )
{
    // Build the ForkId inner list: RLP([hash4, next])
    rlp::RlpEncoder fork_enc;
    EXPECT_TRUE( fork_enc.BeginList() );
    EXPECT_TRUE( fork_enc.add( rlp::ByteView( fork_id.hash.data(), fork_id.hash.size() ) ) );
    EXPECT_TRUE( fork_enc.add( fork_id.next ) );
    EXPECT_TRUE( fork_enc.EndList() );
    auto fork_bytes = fork_enc.MoveBytes();
    EXPECT_TRUE( fork_bytes.has_value() );

    // Build the enrEntry outer list: RLP([ ForkId ])
    rlp::RlpEncoder entry_enc;
    EXPECT_TRUE( entry_enc.BeginList() );
    EXPECT_TRUE( entry_enc.AddRaw( rlp::ByteView( fork_bytes.value().data(), fork_bytes.value().size() ) ) );
    EXPECT_TRUE( entry_enc.EndList() );
    auto entry_bytes = entry_enc.MoveBytes();
    EXPECT_TRUE( entry_bytes.has_value() );

    // Build the full ENR record: RLP([sig_64, seq, "eth", entry_value])
    rlp::RlpEncoder rec_enc;
    EXPECT_TRUE( rec_enc.BeginList() );

    // Signature — 64 zero bytes.
    const std::array<uint8_t, 64U> sig{};
    EXPECT_TRUE( rec_enc.add( rlp::ByteView( sig.data(), sig.size() ) ) );

    // Sequence number.
    EXPECT_TRUE( rec_enc.add( seq ) );

    // Key: "eth" = {0x65, 0x74, 0x68}
    const std::array<uint8_t, 3U> eth_key{ 0x65U, 0x74U, 0x68U };
    EXPECT_TRUE( rec_enc.add( rlp::ByteView( eth_key.data(), eth_key.size() ) ) );

    // Value: enrEntry RLP.
    EXPECT_TRUE( rec_enc.AddRaw( rlp::ByteView( entry_bytes.value().data(), entry_bytes.value().size() ) ) );

    EXPECT_TRUE( rec_enc.EndList() );
    auto rec_bytes = rec_enc.MoveBytes();
    EXPECT_TRUE( rec_bytes.has_value() );
    return std::vector<uint8_t>( rec_bytes.value().begin(), rec_bytes.value().end() );
}

/// @brief ParseEthForkId() extracts hash and next from a record with an eth entry.
TEST( EnrForkIdParseTest, ParseEthForkIdRoundTrips )
{
    const discv4::ForkId expected{ { 0xDE, 0xAD, 0xBE, 0xEF }, 12345678ULL };

    const auto record = make_enr_record_with_eth( expected );

    discv4::discv4_enr_response resp;
    resp.record_rlp = record;

    auto result = resp.ParseEthForkId();
    ASSERT_TRUE( result.has_value() ) << "ParseEthForkId() must succeed on a record with eth entry";
    EXPECT_EQ( result.value(), expected );
}

/// @brief ParseEthForkId() returns error when record has no eth entry.
TEST( EnrForkIdParseTest, MissingEthEntryReturnsError )
{
    // A minimal record with no key-value pairs.
    const auto record = make_minimal_enr_record( 1U );

    discv4::discv4_enr_response resp;
    resp.record_rlp = record;

    auto result = resp.ParseEthForkId();
    EXPECT_FALSE( result.has_value() ) << "ParseEthForkId() must fail when eth key is absent";
}

/// @brief ParseEthForkId() skips unrelated keys and finds eth.
TEST( EnrForkIdParseTest, FindsEthKeyAmongOtherKeys )
{
    const discv4::ForkId expected{ { 0x01, 0x02, 0x03, 0x04 }, 0ULL };

    // Build ForkId + enrEntry bytes (reuse helper logic inline).
    rlp::RlpEncoder fork_enc;
    (void)fork_enc.BeginList();
    (void)fork_enc.add( rlp::ByteView( expected.hash.data(), expected.hash.size() ) );
    (void)fork_enc.add( expected.next );
    (void)fork_enc.EndList();
    auto fork_bytes = fork_enc.MoveBytes();

    rlp::RlpEncoder entry_enc;
    (void)entry_enc.BeginList();
    (void)entry_enc.AddRaw( rlp::ByteView( fork_bytes.value().data(), fork_bytes.value().size() ) );
    (void)entry_enc.EndList();
    auto entry_bytes = entry_enc.MoveBytes();

    // Record with an extra key "abc" before "eth" (ENR keys must be sorted; "abc" < "eth").
    rlp::RlpEncoder rec_enc;
    (void)rec_enc.BeginList();
    const std::array<uint8_t, 64U> sig{};
    (void)rec_enc.add( rlp::ByteView( sig.data(), sig.size() ) );
    (void)rec_enc.add( uint64_t{ 1U } );
    // key "abc"
    const std::array<uint8_t, 3U> abc_key{ 0x61U, 0x62U, 0x63U };
    (void)rec_enc.add( rlp::ByteView( abc_key.data(), abc_key.size() ) );
    // value for "abc" — a simple byte string
    const std::array<uint8_t, 1U> abc_val{ 0xFFU };
    (void)rec_enc.add( rlp::ByteView( abc_val.data(), abc_val.size() ) );
    // key "eth"
    const std::array<uint8_t, 3U> eth_key{ 0x65U, 0x74U, 0x68U };
    (void)rec_enc.add( rlp::ByteView( eth_key.data(), eth_key.size() ) );
    (void)rec_enc.AddRaw( rlp::ByteView( entry_bytes.value().data(), entry_bytes.value().size() ) );
    (void)rec_enc.EndList();
    auto rec_bytes = rec_enc.MoveBytes();

    discv4::discv4_enr_response resp;
    resp.record_rlp = std::vector<uint8_t>( rec_bytes.value().begin(), rec_bytes.value().end() );

    auto result = resp.ParseEthForkId();
    ASSERT_TRUE( result.has_value() ) << "ParseEthForkId() must find eth key after skipping abc";
    EXPECT_EQ( result.value(), expected );
}

/// @brief ParseEthForkId() returns error on empty record_rlp.
TEST( EnrForkIdParseTest, EmptyRecordReturnsError )
{
    discv4::discv4_enr_response resp;
    // record_rlp is default-empty
    auto result = resp.ParseEthForkId();
    EXPECT_FALSE( result.has_value() ) << "ParseEthForkId() must fail on empty record_rlp";
}
