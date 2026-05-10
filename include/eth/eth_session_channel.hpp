// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <rlpx/rlpx_session.hpp>

namespace eth {

/// @brief Minimal session-facing seam used by ETH runner logic and tests.
class IEthSessionChannel
{
public:
    virtual ~IEthSessionChannel() = default;

    [[nodiscard]] virtual uint8_t negotiated_eth_version() const noexcept = 0;
    [[nodiscard]] virtual uint8_t negotiated_eth_offset() const noexcept = 0;
    [[nodiscard]] virtual const rlpx::PeerInfo& peer_info() const noexcept = 0;
    [[nodiscard]] virtual rlpx::VoidResult post_message(rlpx::framing::Message message) noexcept = 0;
    [[nodiscard]] virtual rlpx::Result<rlpx::framing::Message> receive_message(
        boost::asio::yield_context yield) noexcept = 0;
    [[nodiscard]] virtual rlpx::Result<rlpx::framing::Message> receive_message_with_timeout(
        std::chrono::steady_clock::duration timeout,
        boost::asio::yield_context          yield) noexcept = 0;
    virtual void set_eth_message_handler(rlpx::EthMessageHandler handler) noexcept = 0;
};

/// @brief RlpxSession adapter implementing the minimal ETH session seam.
class RlpxEthSessionChannel final : public IEthSessionChannel
{
public:
    explicit RlpxEthSessionChannel(std::shared_ptr<rlpx::RlpxSession> session) noexcept;

    [[nodiscard]] uint8_t negotiated_eth_version() const noexcept override;
    [[nodiscard]] uint8_t negotiated_eth_offset() const noexcept override;
    [[nodiscard]] const rlpx::PeerInfo& peer_info() const noexcept override;
    [[nodiscard]] rlpx::VoidResult post_message(rlpx::framing::Message message) noexcept override;
    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message(
        boost::asio::yield_context yield) noexcept override;
    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message_with_timeout(
        std::chrono::steady_clock::duration timeout,
        boost::asio::yield_context          yield) noexcept override;
    void set_eth_message_handler(rlpx::EthMessageHandler handler) noexcept override;

private:
    std::shared_ptr<rlpx::RlpxSession> session_;
};

} // namespace eth

