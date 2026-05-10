// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <eth/eth_session_channel.hpp>

namespace eth {

RlpxEthSessionChannel::RlpxEthSessionChannel(std::shared_ptr<rlpx::RlpxSession> session) noexcept
    : session_(std::move(session))
{
}

uint8_t RlpxEthSessionChannel::negotiated_eth_version() const noexcept
{
    return session_->negotiated_eth_version();
}

uint8_t RlpxEthSessionChannel::negotiated_eth_offset() const noexcept
{
    return session_->negotiated_eth_offset();
}

const rlpx::PeerInfo& RlpxEthSessionChannel::peer_info() const noexcept
{
    return session_->peer_info();
}

rlpx::VoidResult RlpxEthSessionChannel::post_message(rlpx::framing::Message message) noexcept
{
    return session_->post_message(std::move(message));
}

rlpx::Result<rlpx::framing::Message> RlpxEthSessionChannel::receive_message(
    boost::asio::yield_context yield) noexcept
{
    return session_->receive_message(yield);
}

rlpx::Result<rlpx::framing::Message> RlpxEthSessionChannel::receive_message_with_timeout(
    std::chrono::steady_clock::duration timeout,
    boost::asio::yield_context          yield) noexcept
{
    return session_->receive_message_with_timeout(timeout, yield);
}

void RlpxEthSessionChannel::set_eth_message_handler(rlpx::EthMessageHandler handler) noexcept
{
    session_->set_eth_message_handler(std::move(handler));
}

} // namespace eth

