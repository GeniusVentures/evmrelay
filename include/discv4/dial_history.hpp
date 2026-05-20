#ifndef EVMRELAY_INCLUDE_DISCV4_DIAL_HISTORY_HPP
#define EVMRELAY_INCLUDE_DISCV4_DIAL_HISTORY_HPP

#include <array>
#include <chrono>
#include <discv4/discv4_constants.hpp>
#include <string>
#include <unordered_map>

namespace discv4 {

/// @brief Tracks recently-dialed peers and suppresses retry attempts until a
///        configurable cooldown expires.
///
/// Mirrors go-ethereum's `expHeap`-backed dial history in p2p/dial.go.
/// The default expiry (35 s) matches go-ethereum's `dialHistoryExpiration`
/// (inboundThrottleTime 30 s + 5 s guard).
///
/// Not thread-safe — callers must serialise access if needed.
class DialHistory {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = Clock::duration;

    /// @brief Default expiry duration matching go-ethereum's dialHistoryExpiration.
    static constexpr Duration kDefaultExpiry = kDefaultDialHistoryExpiry;

    /// @param expiry  How long a dialed node is suppressed before being retried.
    explicit DialHistory(Duration expiry = kDefaultExpiry) : expiry_(expiry) {}

    /// @brief Record that a node has just been dialed.
    /// @param node_id  64-byte secp256k1 public key identifying the peer.
    void add(const std::array<uint8_t, 64>& node_id)
    {
        entries_[key(node_id)] = Clock::now() + expiry_;
    }

    /// @brief Returns true if the node was recently dialed and the cooldown
    ///        has not yet expired.  Does NOT call expire() automatically —
    ///        call expire() first if you want stale entries pruned.
    /// @param node_id  64-byte secp256k1 public key identifying the peer.
    bool contains(const std::array<uint8_t, 64>& node_id) const
    {
        auto it = entries_.find(key(node_id));
        if (it == entries_.end()) { return false; }
        return Clock::now() < it->second;
    }

    /// @brief Remove all entries whose expiry time has passed.
    void expire()
    {
        const auto now = Clock::now();
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (now >= it->second) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// @brief Number of active (non-expired) entries.
    std::size_t size() const { return entries_.size(); }

private:
    Duration expiry_;
    mutable std::unordered_map<std::string, TimePoint> entries_;

    /// @brief Derive a stable string key from a node_id for use in the map.
    static std::string key(const std::array<uint8_t, 64>& node_id)
    {
        return std::string(reinterpret_cast<const char*>(node_id.data()), node_id.size());
    }
};

} // namespace discv4

#endif // EVMRELAY_INCLUDE_DISCV4_DIAL_HISTORY_HPP
