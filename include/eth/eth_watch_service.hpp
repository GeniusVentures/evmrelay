// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <eth/abi_decoder.hpp>
#include <eth/chain_tracker.hpp>
#include <eth/event_filter.hpp>
#include <eth/messages.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eth {

/// @brief Subscription handle returned by EthWatchService::watch_event().
using EventWatchId = WatchId;

/// @brief Typed callback for a decoded event log.
using DecodedEventCallback = std::function<void(
    const MatchedEvent&,
    const std::vector<abi::AbiValue>&)>;

/// @brief Callback used by EthWatchService to send an outgoing eth message.
///
/// @param eth_msg_id  Eth-layer message id (before adding the rlpx offset).
/// @param payload     Encoded message bytes.
using SendCallback = std::function<void(uint8_t eth_msg_id, std::vector<uint8_t> payload)>;

/// @brief Snapshot of live EthWatchService traffic counters.
struct WatchStatsSnapshot
{
    uint64_t eth_messages_seen = 0;
    uint64_t new_block_hashes_messages = 0;
    uint64_t new_block_messages = 0;
    uint64_t receipts_messages = 0;
    uint64_t decode_failures = 0;
    uint64_t receipts_requested = 0;
    uint64_t receipts_processed = 0;
    uint64_t logs_seen = 0;
    uint64_t matched_logs = 0;
    uint64_t discarded_logs = 0;
};

/// @brief Ties together EventWatcher, ABI decoding, and eth message dispatch.
///
/// Usage:
///   1. Call set_send_callback() so the service can emit GetReceipts requests.
///   2. Register subscriptions via watch_event().
///   3. Feed incoming eth wire messages via process_message().
///   4. Matching logs trigger the registered DecodedEventCallback.
///
/// Thread-safety: not thread-safe; all calls must be externally synchronized.
class EthWatchService
{
public:
    EthWatchService() = default;
    ~EthWatchService() = default;

    EthWatchService(const EthWatchService&) = delete;
    EthWatchService& operator=(const EthWatchService&) = delete;
    EthWatchService(EthWatchService&&) = default;
    EthWatchService& operator=(EthWatchService&&) = default;

    /// @brief Provide a callback used to send outgoing eth messages.
    ///
    /// Must be called before process_message() if automatic GetReceipts
    /// requests are desired.  Safe to omit if the caller handles receipts
    /// manually via process_receipts().
    void set_send_callback(SendCallback cb) noexcept;

    /// @brief Register a watch for a specific contract event.
    ///
    /// @param contract_address  Contract to watch; empty address = any contract.
    /// @param event_signature   Canonical Solidity signature, e.g.
    ///                          "Transfer(address,address,uint256)".
    /// @param params            Full parameter list in declaration order
    ///                          (mark indexed ones with AbiParam::indexed = true).
    /// @param callback          Called for each matching decoded log.
    /// @param from_block        Optional lower block bound for the filter.
    /// @param to_block          Optional upper block bound for the filter.
    /// @return WatchId that can be passed to unwatch().
    EventWatchId watch_event(
        const codec::Address&             contract_address,
        const std::string&                event_signature,
        const std::vector<abi::AbiParam>& params,
        DecodedEventCallback              callback,
        std::optional<uint64_t>           from_block = std::nullopt,
        std::optional<uint64_t>           to_block   = std::nullopt) noexcept;

    /// @brief Return the highest block number seen so far (0 if none).
    [[nodiscard]] uint64_t tip() const noexcept
    {
        return chain_tracker_.tip();
    }

    /// @brief Return the hash of the highest block seen, if any.
    [[nodiscard]] std::optional<Hash256> tip_hash() const noexcept
    {
        return chain_tracker_.tip_hash();
    }

    /// @brief Remove a previously registered subscription.
    void unwatch(EventWatchId id) noexcept;

    /// @brief Return the number of active subscriptions.
    [[nodiscard]] size_t subscription_count() const noexcept
    {
        return watcher_.subscription_count();
    }

    /// @brief Return a snapshot of the live watcher counters.
    [[nodiscard]] WatchStatsSnapshot stats() const noexcept
    {
        return stats_;
    }

    /// @brief Process a raw eth wire message payload.
    ///
    /// Call this from your generic_handler with the eth-layer message id
    /// (i.e. already minus the rlpx offset) and the raw payload bytes.
    ///
    /// Handles NewBlockHashes (0x01), NewBlock (0x07), Receipts (0x10).
    /// When a send callback is registered, automatically emits GetReceipts
    /// for new blocks.
    ///
    /// @param eth_msg_id  Eth-layer message id (offset already subtracted).
    /// @param payload     Raw message bytes.
    void process_message(uint8_t eth_msg_id, rlp::ByteView payload) noexcept;

    /// @brief Directly process a batch of receipts for a known block.
    ///
    /// @param receipts      The receipts for all transactions in the block.
    /// @param tx_hashes     Corresponding transaction hashes (same order).
    /// @param block_number  Block number.
    /// @param block_hash    Block hash.
    void process_receipts(
        const std::vector<codec::Receipt>& receipts,
        const std::vector<Hash256>&        tx_hashes,
        uint64_t                           block_number,
        const Hash256&                     block_hash) noexcept;

    /// @brief Directly process a NewBlock message.
    void process_new_block(const NewBlockMessage& msg,
                           const Hash256&         block_hash) noexcept;

    /// @brief Request receipts for a block by hash.
    ///
    /// Encodes and sends a GetReceipts message via the send callback.
    /// Records the pending request so the Receipts response can be correlated.
    ///
    /// @param block_hash    Hash of the block whose receipts are wanted.
    /// @param block_number  Block number (stored for context in the callback).
    void request_receipts(const Hash256& block_hash, uint64_t block_number) noexcept;

private:
    /// @brief Context stored for an outstanding GetReceipts request.
    struct PendingRequest
    {
        Hash256  block_hash{};
        uint64_t block_number = 0;
    };

    struct Subscription
    {
        EventWatchId               id;
        std::string                event_signature;
        std::vector<abi::AbiParam> params;
        DecodedEventCallback       callback;
    };

    SendCallback              send_cb_;
    ChainTracker              chain_tracker_;
    EventWatcher              watcher_;
    std::vector<Subscription> subscriptions_;
    EventWatchId              next_id_     = 1;
    uint64_t                  next_req_id_ = 1;
    WatchStatsSnapshot        stats_{};

    /// Outstanding GetReceipts requests keyed by request_id.
    std::map<uint64_t, PendingRequest> pending_requests_;
};

} // namespace eth

