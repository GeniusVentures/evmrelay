// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_watch_service.hpp>
#include <eth/eth_watch_dialer.hpp>
#include <eth/messages.hpp>

namespace eth {

// ---------------------------------------------------------------------------
// make_eth_watcher_pool
// ---------------------------------------------------------------------------

std::shared_ptr<discv4::WatcherPool> make_eth_watcher_pool(
    const EthWatchConnectionConfig& config)
{
    return std::make_shared<discv4::WatcherPool>(
        discv4::WatcherPoolConfig{
            config.max_total_connections,
            config.max_connections_per_chain
        });
}

// ---------------------------------------------------------------------------
// start_eth_watch_chain_peer_dialing
// ---------------------------------------------------------------------------

std::shared_ptr<discv4::DialScheduler> start_eth_watch_chain_peer_dialing(
    boost::asio::io_context&                  io,
    std::shared_ptr<discv4::WatcherPool>      pool,
    discv4::DialFn                            dial_fn,
    const std::vector<discv4::ValidatedPeer>& peers)
{
    auto scheduler = std::make_shared<discv4::DialScheduler>(
        io,
        std::move(pool),
        std::move(dial_fn));

    for (const auto& peer : peers)
    {
        scheduler->enqueue(peer);
    }

    return scheduler;
}

// ---------------------------------------------------------------------------
// set_send_callback
// ---------------------------------------------------------------------------

void EthWatchService::set_send_callback(SendCallback cb) noexcept
{
    send_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// watch_event
// ---------------------------------------------------------------------------

EventWatchId EthWatchService::watch_event(
    const codec::Address&             contract_address,
    const std::string&                event_signature,
    const std::vector<abi::AbiParam>& params,
    DecodedEventCallback              callback,
    std::optional<uint64_t>           from_block,
    std::optional<uint64_t>           to_block) noexcept
{
    const EventWatchId id = next_id_++;

    EventFilter filter;

    const codec::Address zero_addr{};
    if (contract_address != zero_addr)
    {
        filter.addresses.push_back(contract_address);
    }

    filter.topics.push_back(abi::event_signature_hash(event_signature));
    filter.from_block = from_block;
    filter.to_block   = to_block;

    subscriptions_.push_back({id, event_signature, params, std::move(callback)});

    watcher_.watch(filter, [this, id](const MatchedEvent& ev)
    {
        auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
            [id](const Subscription& s) { return s.id == id; });
        if (it == subscriptions_.end())
        {
            return;
        }
        auto decoded = abi::decode_log(ev.log, it->event_signature, it->params);
        if (!decoded)
        {
            return;
        }
        it->callback(ev, decoded.value());
    });

    return id;
}

// ---------------------------------------------------------------------------
// unwatch
// ---------------------------------------------------------------------------

void EthWatchService::unwatch(EventWatchId id) noexcept
{
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [id](const Subscription& s) { return s.id == id; }),
        subscriptions_.end());

    watcher_.unwatch(id);
}

// ---------------------------------------------------------------------------
// request_receipts
// ---------------------------------------------------------------------------

void EthWatchService::request_receipts(const Hash256& block_hash,
                                       uint64_t       block_number) noexcept
{
    if (!send_cb_)
    {
        return;
    }

    // Deduplicate — skip if we have already requested receipts for this block
    if (!chain_tracker_.mark_seen(block_hash, block_number))
    {
        return;
    }

    const uint64_t req_id = next_req_id_++;

    GetReceiptsMessage req;
    req.request_id = req_id;
    req.block_hashes.push_back(block_hash);

    auto encoded = protocol::encode_get_receipts(req);
    if (!encoded)
    {
        return;
    }

    pending_requests_[req_id] = {block_hash, block_number};
    ++stats_.receipts_requested;
    send_cb_(protocol::kGetReceiptsMessageId, std::move(encoded.value()));
}

// ---------------------------------------------------------------------------
// process_message
// ---------------------------------------------------------------------------

void EthWatchService::process_message(uint8_t eth_msg_id, rlp::ByteView payload) noexcept
{
    ++stats_.eth_messages_seen;

    if (eth_msg_id == protocol::kNewBlockHashesMessageId)
    {
        ++stats_.new_block_hashes_messages;
        auto decoded = protocol::decode_new_block_hashes(payload);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }
        for (const auto& entry : decoded.value().entries)
        {
            request_receipts(entry.hash, entry.number);
        }
        return;
    }

    if (eth_msg_id == protocol::kNewBlockMessageId)
    {
        ++stats_.new_block_messages;
        auto decoded = protocol::decode_new_block(payload);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }
        // NewBlock does not include a block hash on the wire — use zeroed sentinel.
        // Still trigger request_receipts so callers with a send_cb get receipts.
        const Hash256 block_hash{};
        process_new_block(decoded.value(), block_hash);
        return;
    }

    if (eth_msg_id == protocol::kReceiptsMessageId)
    {
        ++stats_.receipts_messages;
        auto decoded = protocol::decode_receipts(payload);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }

        const auto& msg = decoded.value();
        size_t block_idx = 0;
        for (const auto& block_receipts : msg.receipts)
        {
            uint64_t block_number = 0;
            Hash256  block_hash{};

            // Correlate to a pending request if request_id is present
            if (msg.request_id.has_value())
            {
                // Each block in the response corresponds to one hash in the request.
                // We issued one hash per request, so request_id maps 1:1.
                if (block_idx == 0)
                {
                    auto it = pending_requests_.find(msg.request_id.value());
                    if (it != pending_requests_.end())
                    {
                        block_hash   = it->second.block_hash;
                        block_number = it->second.block_number;
                        pending_requests_.erase(it);
                    }
                }
            }

            std::vector<Hash256> tx_hashes(block_receipts.size());
            process_receipts(block_receipts, tx_hashes, block_number, block_hash);
            ++block_idx;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// process_receipts
// ---------------------------------------------------------------------------

void EthWatchService::process_receipts(
    const std::vector<codec::Receipt>& receipts,
    const std::vector<Hash256>&        tx_hashes,
    uint64_t                           block_number,
    const Hash256&                     block_hash,
    const std::vector<std::vector<uint32_t>>& log_indices) noexcept
{
    const size_t count = std::min(receipts.size(), tx_hashes.size());
    stats_.receipts_processed += static_cast<uint64_t>(count);
    uint32_t next_log_index = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t log_count = receipts[i].logs.size();
        uint32_t first_log_index = next_log_index;
        if (i < log_indices.size() && !log_indices[i].empty())
        {
            first_log_index = log_indices[i].front();
        }
        const size_t matched = watcher_.process_receipt(
            receipts[i],
            tx_hashes[i],
            block_number,
            block_hash,
            first_log_index);
        stats_.logs_seen += static_cast<uint64_t>(log_count);
        stats_.matched_logs += static_cast<uint64_t>(matched);
        stats_.discarded_logs += static_cast<uint64_t>(log_count - matched);
        next_log_index += static_cast<uint32_t>(log_count);
    }
}

// ---------------------------------------------------------------------------
// process_new_block
// ---------------------------------------------------------------------------

void EthWatchService::process_new_block(const NewBlockMessage& msg,
                                        const Hash256&         block_hash) noexcept
{
    // Request receipts for each transaction in the block so logs can be watched.
    // The block number comes from the embedded header.
    request_receipts(block_hash, msg.header.number);
}

} // namespace eth
