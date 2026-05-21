// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_WATCH_SERVICE_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_WATCH_SERVICE_HPP

#include <eth/abi_decoder.hpp>
#include <eth/chain_tracker.hpp>
#include <eth/eth_peer_queue.hpp>
#include <eth/event_filter.hpp>
#include <eth/messages.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <discv4/discv4_client.hpp>
#include <discv5/discv5_client.hpp>
#include <discv5/enr_tree.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eth {

class EthWatchRunner;

/// @brief Subscription handle returned by EthWatchService::watch_event().
using EventWatchId = WatchId;

/// @brief Context metadata attached to a filtered watch event.
struct WatchEventContext
{
    std::string chain_name;
    uint64_t    network_id = 0;
    std::string peer_client_id;
    std::string peer_address;
};

/// @brief Enriched event payload emitted by EthWatchRunner and EthWatchService.
struct WatchEventNotification
{
    WatchEventContext               context;
    MatchedEvent                    event;
    std::vector<abi::AbiValue>      values;
    std::string                     event_signature;
};

/// @brief Callback invoked for each decoded filtered event with chain/session metadata.
using WatchEventNotificationCallback = std::function<void(const WatchEventNotification&)>;

/// @brief Connection pool limits for eth watch peer sessions.
///        Defaults keep three active dial/watch slots per chain.
struct EthWatchConnectionConfig
{
    int max_total_connections = 24;
    int max_connections_per_chain = 3;
};

/// @brief Peer source strategy for production eth-watch startup.
enum class EthWatchDiscoveryMode
{
    /// Use cached `nodes` only; never start discovery from `bootnodes`.
    kCacheOnly,

    /// Use cached `nodes`; start discovery only when no cached nodes are available.
    kDiscoverIfNeeded,

    /// Start from discovery `bootnodes` and do not enqueue cached `nodes` initially.
    kDiscoverFirst,

    /// Enqueue cached `nodes` and also start discovery from `bootnodes`.
    kHybrid
};

/// @brief Typed callback for a decoded event log.
using DecodedEventCallback = std::function<void(
    const MatchedEvent&,
    const std::vector<abi::AbiValue>&)>;

/// @brief Event filter registration consumed by the production watch runtime.
struct EthWatchEventSpec
{
    codec::Address             contract_address{};
    std::string                event_signature;
    std::vector<abi::AbiParam> params;
    std::optional<uint64_t>    from_block;
    std::optional<uint64_t>    to_block;
};

/// @brief Production eth-watch orchestration config.
struct EthWatchServiceConfig
{
    EthWatchConnectionConfig               connection{};
    EthPeerQueueConfig                     peer_queue{};
    std::vector<discv4::ChainPeerConfig>   chains;
    std::vector<EthWatchEventSpec>         watches;
    EthWatchDiscoveryMode                  discovery_mode = EthWatchDiscoveryMode::kDiscoverIfNeeded;
    bool                                   enable_discv4_fallback = true;
    bool                                   enable_enr_tree_discovery = true;
    discv4::discv4Config                  discovery{};
    discv5::discv5Config                  discv5_discovery{};

    /// @brief Optional test seam for replacing live RLPx dialing.
    std::function<discv4::DialFn(const discv4::ChainPeerConfig&)> dial_fn_factory{};

    /// @brief Optional test seam for replacing live discv4 client construction/startup.
    std::function<std::shared_ptr<discv4::discv4_client>(
        boost::asio::io_context&,
        const discv4::discv4Config&)> discovery_client_factory{};

    /// @brief Optional test seam for replacing live discv5 client construction/startup.
    std::function<std::shared_ptr<discv5::discv5_client>(
        boost::asio::io_context&,
        const discv5::discv5Config&)> discv5_client_factory{};

    /// @brief Optional test seam for resolving configured ENR trees to ENR URI seeds.
    std::function<std::vector<std::string>(
        const discv4::ChainPeerConfig&,
        const std::vector<std::string>&)> enr_tree_resolver{};

    /// @brief Optional test seam for replacing live discv4 fallback startup.
    std::function<bool(
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<EthPeerQueue>)> discv4_fallback_starter{};

    /// @brief Optional test seam for replacing live discv5 ENR-tree startup.
    std::function<bool(
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<EthPeerQueue>,
        const std::vector<std::string>&)> discv5_enr_tree_starter{};
};

/// @brief Callback used by EthWatchService to send an outgoing eth message.
///
/// @param eth_msg_id  Eth-layer message id (before adding the rlpx offset).
/// @param payload     Encoded message bytes.
using SendCallback = std::function<void(uint8_t eth_msg_id, std::vector<uint8_t> payload)>;

struct EthWatchRuntimeStatsSnapshot
{
    /// TCP/socket connection attempts that failed before RLPx auth produced any peer reason.
    uint64_t tcp_connect_failures = 0;
    uint64_t tcp_connected = 0;
    uint64_t auth_success = 0;
    uint64_t local_hello_sent = 0;
    /// Peer sent RLPx Disconnect as its first post-auth message, before peer HELLO.
    uint64_t peer_disconnect_before_hello = 0;
    uint64_t peer_hello_accepted = 0;
    uint64_t eth_status_sent = 0;
    uint64_t remote_status_accepted = 0;
    uint64_t remote_status_rejected = 0;
    EthPeerQueueStatsSnapshot peer_queue{};
};

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
    ~EthWatchService();

    EthWatchService(const EthWatchService&) = delete;
    EthWatchService& operator=(const EthWatchService&) = delete;
    EthWatchService(EthWatchService&&) = default;
    EthWatchService& operator=(EthWatchService&&) = default;

    /// @brief Initialize production orchestration from chain/watch config.
    ///
    /// The method stores config only. Call run() with an io_context to create
    /// schedulers, peer queues, optional discovery fallback, and live sessions.
    [[nodiscard]] bool initialize(
        EthWatchServiceConfig          config,
        WatchEventNotificationCallback callback) noexcept;

    /// @brief Start production eth-watch orchestration on @p io.
    void run(boost::asio::io_context& io) noexcept;

    /// @brief Stop schedulers and discovery clients created by run().
    void stop() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] size_t runtime_chain_count() const noexcept;
    [[nodiscard]] size_t scheduler_count() const noexcept;
    [[nodiscard]] size_t peer_queue_count() const noexcept;
    [[nodiscard]] size_t discovery_client_count() const noexcept;
    [[nodiscard]] size_t discv4_fallback_count() const noexcept;
    [[nodiscard]] size_t active_runner_count() const noexcept;
    [[nodiscard]] WatchStatsSnapshot aggregate_runtime_stats() const noexcept;
    [[nodiscard]] EthWatchRuntimeStatsSnapshot aggregate_connection_stats() const noexcept;
    [[nodiscard]] std::shared_ptr<EthPeerQueue> peer_queue(const std::string& chain_name) const noexcept;

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
        const Hash256&                     block_hash,
        const std::vector<std::vector<uint32_t>>& log_indices = {}) noexcept;

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

    struct RuntimeChain
    {
        discv4::ChainPeerConfig                 config;
        std::shared_ptr<discv4::DialScheduler>  scheduler;
        std::shared_ptr<EthPeerQueue>           peer_queue;
        std::shared_ptr<discv4::discv4_client>  discovery_client;
        std::shared_ptr<discv5::discv5_client>  discv5_client;
        bool                                    discv4_fallback_started = false;
        bool                                    discv5_enr_tree_started = false;
        bool                                    discv5_cache_enr_started = false;
        std::shared_ptr<EthWatchRuntimeStatsSnapshot> stats;
    };

    [[nodiscard]] discv4::DialFn make_default_dial_fn(
        const discv4::ChainPeerConfig& chain_config) noexcept;
    void start_discv4_fallback(
        boost::asio::io_context& io,
        RuntimeChain&            runtime) noexcept;
    bool start_enr_tree_discovery(
        boost::asio::io_context& io,
        RuntimeChain&            runtime) noexcept;
    bool start_discv5_discovery(
        boost::asio::io_context&       io,
        RuntimeChain&                  runtime,
        const std::vector<std::string>& bootstrap_enrs) noexcept;

    bool                           orchestration_initialized_ = false;
    bool                           orchestration_running_ = false;
    EthWatchServiceConfig          orchestration_config_{};
    WatchEventNotificationCallback orchestration_callback_{};
    std::vector<RuntimeChain>      runtime_chains_{};
    std::vector<std::shared_ptr<EthWatchRunner>> active_runners_{};
    std::unordered_map<std::string, std::shared_ptr<EthWatchRuntimeStatsSnapshot>> runtime_stats_by_chain_;
};

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_WATCH_SERVICE_HPP
