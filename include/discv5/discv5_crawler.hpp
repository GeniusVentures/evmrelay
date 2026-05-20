// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV5_DISCV5_CRAWLER_HPP
#define EVMRELAY_INCLUDE_DISCV5_DISCV5_CRAWLER_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <discv5/discv5_error.hpp>
#include <discv5/discv5_types.hpp>

namespace discv5
{

// ---------------------------------------------------------------------------
// CrawlerStats
// ---------------------------------------------------------------------------

/// @brief Snapshot of crawler activity counters.
///
/// Returned by @p discv5_crawler::stats() for monitoring / reporting.
struct CrawlerStats
{
    size_t queued{};        ///< Peers currently in the outbound query queue
    size_t measured{};      ///< Peers that returned at least one NODES reply
    size_t failed{};        ///< Peers that timed out or returned an error
    size_t discovered{};    ///< Unique valid peers forwarded to the callback
    size_t invalid_enr{};   ///< Records rejected due to parse/signature failure
    size_t wrong_chain{};   ///< Records dropped by the fork-id filter
    size_t no_eth_entry{};  ///< Records without an "eth" entry when filter active
    size_t duplicates{};    ///< Records deduplicated against known node_ids
};

// ---------------------------------------------------------------------------
// discv5_crawler
// ---------------------------------------------------------------------------

/// @brief Discv5 peer crawler: seed → FINDNODE loop → ValidatedPeer emission.
///
/// Manages four peer sets that mirror the nim dcrawl pattern:
///  - **queued**:     nodes to be queried next (FINDNODE not yet sent)
///  - **measured**:   nodes that responded to at least one FINDNODE query
///  - **failed**:     nodes that timed out or returned an error
///  - **discovered**: deduplication set; node_ids already forwarded downstream
///
/// Protocol implementation note
/// ----------------------------
/// The first iteration keeps the network I/O intentionally simple: it pings
/// bootstrap nodes with ENR-sourced addresses and processes NODES replies.
/// A full WHOAREYOU/HANDSHAKE session layer will be added in a future sprint.
///
/// Thread safety
/// -------------
/// start() and stop() must be called from the same thread that drives the
/// provided @p asio::io_context.  The stats() accessor is lock-protected.
class discv5_crawler
{
public:
    /// @brief Construct the crawler with a fully-populated configuration.
    ///
    /// @param config  Crawler parameters.  A copy is taken.
    explicit discv5_crawler(const discv5Config& config) noexcept;

    ~discv5_crawler() = default;

    // Non-copyable, non-movable (owns the peer-set state).
    discv5_crawler(const discv5_crawler&)            = delete;
    discv5_crawler& operator=(const discv5_crawler&) = delete;
    discv5_crawler(discv5_crawler&&)                 = delete;
    discv5_crawler& operator=(discv5_crawler&&)      = delete;

    // -----------------------------------------------------------------------
    // Public interface
    // -----------------------------------------------------------------------

    /// @brief Seed the crawler with an additional bootstrap ENR record.
    ///
    /// May be called before or after start().  Records added after start() are
    /// processed in the next query round.
    ///
    /// @param record  Parsed and signature-verified EnrRecord.
    void add_bootstrap(const EnrRecord& record) noexcept;

    /// @brief Register the callback invoked for each newly discovered peer.
    ///
    /// Replaces any previously registered callback.  The callback is invoked
    /// synchronously from the crawler's internal processing loop; it must not
    /// block.
    ///
    /// @param callback  Function to call with a ValidatedPeer.
    void set_peer_discovered_callback(PeerDiscoveredCallback callback) noexcept;

    /// @brief Register the error callback for non-fatal diagnostics.
    ///
    /// @param callback  Function to call with an error description string.
    void set_error_callback(ErrorCallback callback) noexcept;

    /// @brief Enqueue all bootstrap seeds and transition to running state.
    ///
    /// Seeds are taken from the @p discv5Config::bootstrap_enrs list.
    ///
    /// @return  success or kCrawlerAlreadyRunning.
    VoidResult start() noexcept;

    /// @brief Stop the crawler and clear the running flag.
    ///
    /// Does not drain the queued set — a subsequent start() will resume from
    /// where processing left off.
    ///
    /// @return  success or kCrawlerNotRunning.
    VoidResult stop() noexcept;

    /// @brief Manually enqueue a set of ValidatedPeer entries from an external
    ///        source (e.g. a NODES reply decoded by the client layer).
    ///
    /// Deduplicates against the known node_id set before enqueueing.
    ///
    /// @param peers  Peers to consider as FINDNODE candidates.
    void process_found_peers(const std::vector<ValidatedPeer>& peers) noexcept;

    /// @brief Accept peers decoded from a live NODES response.
    ///
    /// Queues new peers for further crawling and also forwards them through
    /// the normal discovered-peer callback path.
    ///
    /// @param peers  Peers decoded from a live response packet.
    void ingest_discovered_peers(const std::vector<ValidatedPeer>& peers) noexcept;

    /// @brief Return a snapshot of current activity counters (thread-safe).
    [[nodiscard]] CrawlerStats stats() const noexcept;

    /// @brief Returns true if the crawler has been started and not yet stopped.
    [[nodiscard]] bool is_running() const noexcept;

    // -----------------------------------------------------------------------
    // Internal helpers (public for testing)
    // -----------------------------------------------------------------------

    /// @brief Mark a peer as measured (responded to a query).
    void mark_measured(const NodeId& node_id) noexcept;

    /// @brief Mark a peer as failed (query timed out or returned error).
    void mark_failed(const NodeId& node_id) noexcept;

    /// @brief Return the next queued NodeId to probe, or nullopt if queue empty.
    std::optional<ValidatedPeer> dequeue_next() noexcept;

    /// @brief True if node_id has already been forwarded to the callback.
    [[nodiscard]] bool is_discovered(const NodeId& node_id) const noexcept;

private:
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// @brief Attempt to parse and enqueue a single ENR URI string.
    void enqueue_enr_uri(const std::string& uri) noexcept;

    /// @brief Attempt to parse and enqueue a single enode URI string.
    void enqueue_enode_uri(const std::string& uri) noexcept;

    /// @brief Forward a valid peer through the fork-id filter and callback.
    void emit_peer(const ValidatedPeer& peer) noexcept;

    /// @brief Convert a NodeId to a string key for use in sets/maps.
    static std::string node_key(const NodeId& id) noexcept;

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    discv5Config config_;

    mutable std::mutex state_mutex_;

    /// Peers waiting to be queried (dequeue → FINDNODE).
    std::vector<ValidatedPeer>            queued_peers_;

    /// node_id keys of peers that have responded at least once.
    std::unordered_set<std::string>       measured_ids_;

    /// node_id keys of peers that failed to respond.
    std::unordered_set<std::string>       failed_ids_;

    /// node_id keys of peers already forwarded to the callback (dedup).
    std::unordered_set<std::string>       discovered_ids_;

    // Activity counters — mirrored from the set sizes for lock-free reads.
    std::atomic<size_t> stat_discovered_{};
    std::atomic<size_t> stat_invalid_enr_{};
    std::atomic<size_t> stat_wrong_chain_{};
    std::atomic<size_t> stat_no_eth_entry_{};
    std::atomic<size_t> stat_duplicates_{};

    std::atomic<bool>   running_{false};

    PeerDiscoveredCallback peer_callback_{};
    ErrorCallback          error_callback_{};
};

} // namespace discv5

#endif // EVMRELAY_INCLUDE_DISCV5_DISCV5_CRAWLER_HPP
