// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv4/discv4_enr_request.hpp"
#include "discv4/discv4_constants.hpp"
#include <rlp/rlp_encoder.hpp>

namespace discv4 {

std::vector<uint8_t> discv4_enr_request::RlpPayload() const noexcept
{
    rlp::RlpEncoder encoder;

    if ( auto res = encoder.BeginList(); !res )
    {
        return {};
    }
    if ( auto res = encoder.add( expiration ); !res )
    {
        return {};
    }
    if ( auto res = encoder.EndList(); !res )
    {
        return {};
    }

    auto bytes_result = encoder.MoveBytes();
    if ( !bytes_result )
    {
        return {};
    }

    rlp::Bytes bytes = std::move( bytes_result.value() );
    bytes.insert( bytes.begin(), kPacketTypeEnrRequest );
    return std::vector<uint8_t>( bytes.begin(), bytes.end() );
}

} // namespace discv4

