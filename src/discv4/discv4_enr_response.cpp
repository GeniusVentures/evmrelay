// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv4/discv4_enr_response.hpp"
#include "discv4/discv4_constants.hpp"

namespace discv4 {

// ASCII bytes for the ENR key "eth" (go-ethereum eth/protocols/eth/discovery.go).
static constexpr std::array<uint8_t, 3U> kEthKey{ 0x65U, 0x74U, 0x68U };

rlp::Result<discv4_enr_response> discv4_enr_response::Parse( rlp::ByteView raw ) noexcept
{
    if ( raw.size() <= kWireHeaderSize )
    {
        return rlp::DecodingError::kInputTooShort;
    }

    if ( raw[kWirePacketTypeOffset] != kPacketTypeEnrResponse )
    {
        return rlp::DecodingError::kUnexpectedString;
    }

    rlp::ByteView payload( raw.data() + kWireHeaderSize, raw.size() - kWireHeaderSize );
    rlp::RlpDecoder decoder( payload );

    BOOST_OUTCOME_TRY( bool is_list, decoder.IsList() );
    if ( !is_list )
    {
        return rlp::DecodingError::kUnexpectedString;
    }
    BOOST_OUTCOME_TRY( size_t outer_len, decoder.ReadListHeaderBytes() );
    (void)outer_len;

    discv4_enr_response response;

    BOOST_OUTCOME_TRY( decoder.read( response.request_hash ) );

    BOOST_OUTCOME_TRY( auto record_header, decoder.PeekHeader() );
    const size_t record_total = record_header.header_size_bytes + record_header.payload_size_bytes;

    if ( decoder.Remaining().size() < record_total )
    {
        return rlp::DecodingError::kInputTooShort;
    }

    response.record_rlp.assign(
        decoder.Remaining().data(),
        decoder.Remaining().data() + record_total );

    BOOST_OUTCOME_TRY( decoder.SkipItem() );

    return response;
}

rlp::Result<ForkId> discv4_enr_response::ParseEthForkId() const noexcept
{
    if ( record_rlp.empty() )
    {
        return rlp::DecodingError::kInputTooShort;
    }

    rlp::ByteView   record_view( record_rlp.data(), record_rlp.size() );
    rlp::RlpDecoder record_dec( record_view );

    // ENR record: RLP([signature, seq, k0, v0, k1, v1, ...])
    BOOST_OUTCOME_TRY( bool is_list, record_dec.IsList() );
    if ( !is_list )
    {
        return rlp::DecodingError::kUnexpectedString;
    }
    BOOST_OUTCOME_TRY( size_t record_len, record_dec.ReadListHeaderBytes() );
    (void)record_len;

    // Skip signature (first element).
    BOOST_OUTCOME_TRY( record_dec.SkipItem() );

    // Skip sequence number (second element).
    BOOST_OUTCOME_TRY( record_dec.SkipItem() );

    // Iterate key/value pairs looking for key == "eth".
    while ( !record_dec.IsFinished() )
    {
        rlp::Bytes key_bytes;
        BOOST_OUTCOME_TRY( record_dec.read( key_bytes ) );

        if ( key_bytes.size() == kEthKey.size() &&
             std::equal( key_bytes.begin(), key_bytes.end(), kEthKey.begin() ) )
        {
            // Value: RLP(enrEntry) = RLP([ [hash4, next_uint64] ])
            // Outer list = enrEntry fields; inner list = ForkId fields.
            BOOST_OUTCOME_TRY( bool val_is_list, record_dec.IsList() );
            if ( !val_is_list )
            {
                return rlp::DecodingError::kUnexpectedString;
            }
            BOOST_OUTCOME_TRY( size_t outer_val_len, record_dec.ReadListHeaderBytes() );
            (void)outer_val_len;

            BOOST_OUTCOME_TRY( bool fork_is_list, record_dec.IsList() );
            if ( !fork_is_list )
            {
                return rlp::DecodingError::kUnexpectedString;
            }
            BOOST_OUTCOME_TRY( size_t fork_len, record_dec.ReadListHeaderBytes() );
            (void)fork_len;

            ForkId fork_id;
            BOOST_OUTCOME_TRY( record_dec.read( fork_id.hash ) );
            BOOST_OUTCOME_TRY( record_dec.read( fork_id.next ) );

            return fork_id;
        }

        // Not "eth" — skip the value and continue.
        BOOST_OUTCOME_TRY( record_dec.SkipItem() );
    }

    // `eth` key not present in record.
    return rlp::DecodingError::kUnexpectedString;
}

} // namespace discv4

