// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <eth/messages.hpp>
#include <eth/eth_session_channel.hpp>
#include <boost/asio/spawn.hpp>

namespace eth {

/// @brief Callback invoked when the remote ETH Status message is accepted.
using EthStatusAcceptedHandler = std::function<void(const StatusMessage&)>;

/// @brief Parameters for starting the ETH Status handshake on a negotiated session.
struct EthStatusHandshakeStart
{
    std::shared_ptr<IEthSessionChannel> channel;
    uint64_t                           network_id = 0;
    Hash256                            genesis_hash{};
    ForkId                             fork_id{};
    EthStatusAcceptedHandler           accepted_status_handler;
    rlpx::EthMessageHandler            inbound_message_handler;
};

/// @brief Result of the ETH Status startup handshake owned by the ETH layer.
struct EthStatusHandshakeResult
{
    StatusMessage remote_status{};
};

/// @brief Build the local ETH Status message for the negotiated ETH protocol version.
///
/// @param negotiated_protocol_version The negotiated ETH subprotocol version.
/// @param network_id                  Local chain network id.
/// @param genesis_hash                Local chain genesis hash.
/// @param fork_id                     Local chain fork id.
/// @return ETH/68 or ETH/69 Status message matching the negotiated version.
[[nodiscard]] StatusMessage BuildLocalStatusMessage(
    uint8_t             negotiated_protocol_version,
    uint64_t            network_id,
    const Hash256&      genesis_hash,
    const ForkId&       fork_id) noexcept;

/// @brief Validate a remote ETH Status message against negotiated version and chain.
///
/// @param remote_status                Decoded remote Status message.
/// @param negotiated_protocol_version  Negotiated ETH subprotocol version.
/// @param expected_network_id          Expected chain network id.
/// @param expected_genesis_hash        Expected chain genesis hash.
/// @return Success when the status matches the negotiated version and chain.
[[nodiscard]] protocol::ValidationResult ValidateRemoteStatusMessage(
    const StatusMessage& remote_status,
    uint8_t              negotiated_protocol_version,
    uint64_t             expected_network_id,
    const Hash256&       expected_genesis_hash) noexcept;

/// @brief Install post-handshake ETH inbound handling on a negotiated session.
///
/// @param start Handshake start parameters bound to the negotiated session/channel.
/// @return True when the post-handshake handler was installed successfully.
[[nodiscard]] bool StartEthStatusHandshake(
    const EthStatusHandshakeStart& start) noexcept;

} // namespace eth

