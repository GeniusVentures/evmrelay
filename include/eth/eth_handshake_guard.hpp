// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_GUARD_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_GUARD_HPP

#include <eth/eth_peer_session.hpp>
#include <rlpx/rlpx_session.hpp>
#include <optional>

namespace eth {

/// @brief Result of processing one inbound ETH handshake-phase message.
enum class HandshakeMessageDisposition
{
    kIgnored,
    kAcceptedStatus,
    kRejected
};

/// @brief Process one inbound ETH handshake-phase message.
/// @param message Inbound wire-level RLPx message.
/// @param negotiated_eth_offset Negotiated ETH wire offset.
/// @param negotiated_eth_version Negotiated ETH version.
/// @param network_id Expected local network identifier.
/// @param genesis_hash Expected local genesis hash.
/// @param status_received Whether a valid remote ETH Status was already accepted.
/// @return Disposition indicating whether the message was ignored, accepted, or rejected.
[[nodiscard]] HandshakeMessageDisposition HandleEthHandshakeMessage(
    const rlpx::protocol::Message& message,
    uint8_t                        negotiated_eth_offset,
    uint8_t                        negotiated_eth_version,
    uint64_t                       network_id,
    const Hash256&                 genesis_hash,
    bool&                          status_received) noexcept;

/// @brief Return the ETH-local message id for a wire-level message.
/// @param wire_message_id Wire-level RLPx message id.
/// @param negotiated_eth_offset Negotiated ETH wire offset.
/// @return ETH-local message id if the wire id belongs to ETH; std::nullopt otherwise.
[[nodiscard]] std::optional<uint8_t> NormalizeEthWireMessageId(
    uint8_t wire_message_id,
    uint8_t negotiated_eth_offset) noexcept;

/// @brief Return the latest block number from a validated ETH Status message.
/// @param status Decoded ETH Status message.
/// @return Latest block number for ETH/69, or 0 for earlier layouts.
[[nodiscard]] uint64_t ExtractLatestBlockNumber(
    const StatusMessage& status) noexcept;

/// @brief Decode and validate an inbound ETH Status message.
/// @param message Inbound wire-level RLPx message.
/// @param negotiated_eth_offset Negotiated ETH wire offset.
/// @param negotiated_eth_version Negotiated ETH version.
/// @param network_id Expected local network identifier.
/// @param genesis_hash Expected local genesis hash.
/// @return Decoded valid ETH Status message, or a validation/decode error.
[[nodiscard]] rlp::outcome::result<StatusMessage, StatusValidationError, rlp::outcome::policy::all_narrow>
DecodeValidatedStatusMessage(
    const rlpx::protocol::Message& message,
    uint8_t                        negotiated_eth_offset,
    uint8_t                        negotiated_eth_version,
    uint64_t                       network_id,
    const Hash256&                 genesis_hash,
    const std::vector<EthMessageSchema>& eth_message_schemas = {}) noexcept;

} // namespace eth


#endif // EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_GUARD_HPP
