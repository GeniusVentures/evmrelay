// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_handshake_guard.hpp>
#include <eth/messages.hpp>

namespace eth {

std::optional<uint8_t> NormalizeEthWireMessageId(
    uint8_t wire_message_id,
    uint8_t negotiated_eth_offset) noexcept
{
    if (negotiated_eth_offset == 0U || wire_message_id < negotiated_eth_offset)
    {
        return std::nullopt;
    }

    return static_cast<uint8_t>(wire_message_id - negotiated_eth_offset);
}

uint64_t ExtractLatestBlockNumber(const StatusMessage& status) noexcept
{
    return std::visit([](const auto& message) -> uint64_t
    {
        if constexpr (std::is_same_v<std::decay_t<decltype(message)>, StatusMessage69>)
        {
            return message.latest_block;
        }
        return 0;
    }, status);
}

rlp::outcome::result<StatusMessage, StatusValidationError, rlp::outcome::policy::all_narrow>
DecodeValidatedStatusMessage(
    const rlpx::protocol::Message& message,
    uint8_t                        negotiated_eth_offset,
    uint8_t                        negotiated_eth_version,
    uint64_t                       network_id,
    const Hash256&                 genesis_hash) noexcept
{
    const auto eth_id = NormalizeEthWireMessageId(message.id, negotiated_eth_offset);
    if (!eth_id.has_value() || *eth_id != protocol::kStatusMessageId)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const rlp::ByteView payload(message.payload.data(), message.payload.size());
    const auto decoded = protocol::decode_status(payload);
    if (!decoded)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const auto valid = ValidateRemoteStatusMessage(
        decoded.value(),
        negotiated_eth_version,
        network_id,
        genesis_hash);
    if (!valid)
    {
        return rlp::outcome::failure(valid.error());
    }

    return decoded.value();
}

HandshakeMessageDisposition HandleEthHandshakeMessage(
    const rlpx::protocol::Message& message,
    uint8_t                        negotiated_eth_offset,
    uint8_t                        negotiated_eth_version,
    uint64_t                       network_id,
    const Hash256&                 genesis_hash,
    bool&                          status_received) noexcept
{
    const auto eth_id = NormalizeEthWireMessageId(message.id, negotiated_eth_offset);
    if (!eth_id.has_value())
    {
        return HandshakeMessageDisposition::kIgnored;
    }

    if (*eth_id != protocol::kStatusMessageId)
    {
        if (!status_received)
        {
            return HandshakeMessageDisposition::kRejected;
        }
        return HandshakeMessageDisposition::kIgnored;
    }

    const auto decoded = DecodeValidatedStatusMessage(
        message,
        negotiated_eth_offset,
        negotiated_eth_version,
        network_id,
        genesis_hash);
    if (!decoded)
    {
        return HandshakeMessageDisposition::kRejected;
    }

    status_received = true;
    return HandshakeMessageDisposition::kAcceptedStatus;
}

} // namespace eth

