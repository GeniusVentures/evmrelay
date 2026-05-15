// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_handshake_guard.hpp>
#include <eth/eth_peer_session.hpp>

namespace eth {

StatusMessage BuildLocalStatusMessage(
    uint8_t        negotiated_protocol_version,
    uint64_t       network_id,
    const Hash256& genesis_hash,
    const ForkId&  fork_id) noexcept
{
    if (negotiated_protocol_version <= kEthProtocolVersion68)
    {
        StatusMessage68 status68;
        status68.protocol_version = negotiated_protocol_version;
        status68.network_id = network_id;
        status68.genesis_hash = genesis_hash;
        status68.fork_id = fork_id;
        status68.td = 0;
        status68.blockhash = genesis_hash;
        return status68;
    }

    StatusMessage69 status69;
    status69.protocol_version = negotiated_protocol_version;
    status69.network_id = network_id;
    status69.genesis_hash = genesis_hash;
    status69.fork_id = fork_id;
    status69.earliest_block = 0;
    status69.latest_block = 0;
    status69.latest_block_hash = genesis_hash;
    return status69;
}

protocol::ValidationResult ValidateRemoteStatusMessage(
    const StatusMessage& remote_status,
    uint8_t              negotiated_protocol_version,
    uint64_t             expected_network_id,
    const Hash256&       expected_genesis_hash) noexcept
{
    const auto common = get_common_fields(remote_status);
    if (common.protocol_version != negotiated_protocol_version)
    {
        return StatusValidationError::kProtocolVersionMismatch;
    }

    return protocol::validate_status(remote_status, expected_network_id, expected_genesis_hash);
}

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

bool StartEthStatusHandshake(
    const EthStatusHandshakeStart& start) noexcept
{
    if (!start.channel)
    {
        return false;
    }

    start.channel->set_eth_message_handler(start.inbound_message_handler);
    return true;
}

} // namespace eth

