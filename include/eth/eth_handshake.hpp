// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_HPP

#include <eth/eth_peer_session.hpp>
#include <boost/asio/spawn.hpp>

namespace eth {

/// @brief Execute the ETH Status startup handshake for a negotiated ETH session.
///
/// @param start Handshake parameters bound to the negotiated ETH session/channel.
/// @param yield Boost.Asio stackful coroutine context used to await the first ETH message.
/// @return ETH-layer handshake result containing the validated remote status.
[[nodiscard]] rlp::outcome::result<EthStatusHandshakeResult, StatusValidationError, rlp::outcome::policy::all_narrow>
PerformEthStatusHandshake(
    const EthStatusHandshakeStart& start,
    boost::asio::yield_context     yield) noexcept;

} // namespace eth


#endif // EVMRELAY_INCLUDE_ETH_ETH_HANDSHAKE_HPP
