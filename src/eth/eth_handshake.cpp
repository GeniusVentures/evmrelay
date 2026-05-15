// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_handshake.hpp>
#include <eth/eth_handshake_guard.hpp>

namespace eth {

rlp::outcome::result<EthStatusHandshakeResult, StatusValidationError, rlp::outcome::policy::all_narrow>
PerformEthStatusHandshake(
    const EthStatusHandshakeStart& start,
    boost::asio::yield_context     yield) noexcept
{
    if (!start.channel)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const uint8_t negotiated_eth_version = start.channel->negotiated_eth_version();
    const uint8_t negotiated_eth_offset = start.channel->negotiated_eth_offset();
    if (negotiated_eth_version == 0U || negotiated_eth_offset == 0U)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const auto status = BuildLocalStatusMessage(
        negotiated_eth_version,
        start.network_id,
        start.genesis_hash,
        start.fork_id);

    auto encoded = protocol::encode_status(status);
    if (!encoded)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    rlpx::framing::Message status_message{};
    status_message.id = static_cast<uint8_t>(negotiated_eth_offset + protocol::kStatusMessageId);
    status_message.payload = std::move(encoded.value());
    const auto post_result = start.channel->post_message(std::move(status_message));
    if (!post_result)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    for (;;)
    {
        auto inbound_result = start.channel->receive_message_with_timeout(
            protocol::kStatusHandshakeTimeout,
            yield);
        if (!inbound_result)
        {
            return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
        }

        rlpx::protocol::Message inbound_message{};
        inbound_message.id = inbound_result.value().id;
        inbound_message.payload = std::move(inbound_result.value().payload);

        bool status_received = false;
        const auto handshake_disposition = HandleEthHandshakeMessage(
            inbound_message,
            negotiated_eth_offset,
            negotiated_eth_version,
            start.network_id,
            start.genesis_hash,
            status_received);

        if (handshake_disposition == HandshakeMessageDisposition::kAcceptedStatus)
        {
            const auto validated_status = DecodeValidatedStatusMessage(
                inbound_message,
                negotiated_eth_offset,
                negotiated_eth_version,
                start.network_id,
                start.genesis_hash);
            if (!validated_status)
            {
                return rlp::outcome::failure(validated_status.error());
            }

            EthStatusHandshakeResult result{};
            result.remote_status = validated_status.value();
            return result;
        }

        if (handshake_disposition == HandshakeMessageDisposition::kRejected)
        {
            const auto validated_status = DecodeValidatedStatusMessage(
                inbound_message,
                negotiated_eth_offset,
                negotiated_eth_version,
                start.network_id,
                start.genesis_hash);
            if (!validated_status)
            {
                return rlp::outcome::failure(validated_status.error());
            }
            return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
        }
    }
}

} // namespace eth

