// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_watch_runner.hpp>
#include <eth/messages.hpp>

namespace eth {

EthWatchRunner::EthWatchRunner(
    std::shared_ptr<IEthSessionChannel> channel,
    std::string                        chain_name,
    uint64_t                           network_id,
    Hash256                            genesis_hash,
    ForkId                             fork_id,
    std::vector<EthMessageSchema>      eth_message_schemas) noexcept
    : channel_(std::move(channel))
    , chain_name_(std::move(chain_name))
    , network_id_(network_id)
    , genesis_hash_(genesis_hash)
    , fork_id_(fork_id)
    , eth_message_schemas_(std::move(eth_message_schemas))
{
    watch_service_.set_eth_message_schemas(eth_message_schemas_);
}

EthWatchRunner::EthWatchRunner(
    std::shared_ptr<rlpx::RlpxSession> session,
    std::string                        chain_name,
    uint64_t                           network_id,
    Hash256                            genesis_hash,
    ForkId                             fork_id,
    std::vector<EthMessageSchema>      eth_message_schemas) noexcept
    : EthWatchRunner(std::make_shared<RlpxEthSessionChannel>(std::move(session)),
                     std::move(chain_name),
                     network_id,
                     genesis_hash,
                     fork_id,
                     std::move(eth_message_schemas))
{
}

void EthWatchRunner::set_event_callback(WatchEventNotificationCallback callback) noexcept
{
    event_callback_ = std::move(callback);
}

EthWatchService& EthWatchRunner::service() noexcept
{
    return watch_service_;
}

const WatchEventContext& EthWatchRunner::context() const noexcept
{
    static WatchEventContext context{};
    context.chain_name = chain_name_;
    context.network_id = network_id_;
    context.peer_client_id = channel_ ? channel_->peer_info().client_id : std::string{};
    context.peer_address = channel_ ? channel_->peer_info().remote_address : std::string{};
    return context;
}

void EthWatchRunner::notify_event(
    const std::string&                event_signature,
    const MatchedEvent&               event,
    const std::vector<abi::AbiValue>& values) noexcept
{
    if (!event_callback_)
    {
        return;
    }

    WatchEventNotification notification{};
    notification.context = context();
    notification.event = event;
    notification.values = values;
    notification.event_signature = event_signature;
    event_callback_(notification);
}

bool EthWatchRunner::send_local_status() noexcept
{
    const uint8_t negotiated_eth_version = channel_->negotiated_eth_version();
    const uint8_t negotiated_eth_offset = channel_->negotiated_eth_offset();
    if (negotiated_eth_version == 0U || negotiated_eth_offset == 0U)
    {
        return false;
    }

    const auto status = BuildLocalStatusMessage(
        negotiated_eth_version,
        network_id_,
        genesis_hash_,
        fork_id_);

    auto encoded = protocol::encode_status(status);
    if (!encoded)
    {
        return false;
    }

    rlpx::framing::Message status_message{};
    status_message.id = static_cast<uint8_t>(negotiated_eth_offset + protocol::kStatusMessageId);
    status_message.payload = std::move(encoded.value());
    const auto post_result = channel_->post_message(std::move(status_message));
    if (!post_result)
    {
        return false;
    }
    return true;
}

void EthWatchRunner::install_session_bridge() noexcept
{
    watch_service_.set_send_callback([channel = channel_](uint8_t eth_msg_id, std::vector<uint8_t> payload)
    {
        const uint8_t negotiated_eth_offset = channel->negotiated_eth_offset();
        if (negotiated_eth_offset == 0U)
        {
            return;
        }

        rlpx::framing::Message message{};
        message.id = static_cast<uint8_t>(negotiated_eth_offset + eth_msg_id);
        message.payload = std::move(payload);
        (void)channel->post_message(std::move(message));
    });

    channel_->set_eth_message_handler([this](uint8_t eth_msg_id, const rlpx::ByteBuffer& payload)
    {
        const rlp::ByteView payload_view(payload.data(), payload.size());
        watch_service_.process_message(eth_msg_id, payload_view);
    });
}

EventWatchId EthWatchRunner::watch_event(
    const codec::Address&             contract_address,
    const std::string&                event_signature,
    const std::vector<abi::AbiParam>& params,
    std::optional<uint64_t>           from_block,
    std::optional<uint64_t>           to_block) noexcept
{
    return watch_service_.watch_event(
        contract_address,
        event_signature,
        params,
        [this, event_signature](const MatchedEvent& event, const std::vector<abi::AbiValue>& values)
        {
            notify_event(event_signature, event, values);
        },
        from_block,
        to_block);
}

} // namespace eth
