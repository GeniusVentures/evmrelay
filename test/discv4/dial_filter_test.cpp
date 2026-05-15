// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file dial_filter_test.cpp
/// @brief Unit tests for DialScheduler::filter_fn and make_fork_id_filter().
///
/// Verifies that peers are accepted or dropped based on their eth_fork_id
/// before consuming a dial slot — the primary ENR chain pre-filter mechanism.

#include <gtest/gtest.h>
#include <discv4/dial_scheduler.hpp>
#include <rlpx/rlpx_session.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Build a ValidatedPeer with an optional ForkId and unique node_id byte.
discv4::ValidatedPeer make_peer(
    uint8_t                              id_byte,
    std::optional<discv4::ForkId>        fork_id = std::nullopt ) noexcept
{
    discv4::ValidatedPeer vp{};
    vp.peer.ip           = "127.0.0.1";
    vp.peer.tcp_port     = 30303U;
    vp.peer.node_id.fill( id_byte );
    vp.peer.eth_fork_id  = fork_id;
    return vp;
}

/// @brief Instantiate a minimal DialScheduler whose dial_fn counts calls.
std::pair<std::shared_ptr<discv4::DialScheduler>, std::shared_ptr<std::atomic<int>>>
make_counting_scheduler( boost::asio::io_context& io )
{
    auto pool      = std::make_shared<discv4::WatcherPool>( 10, 10 );
    auto dial_count = std::make_shared<std::atomic<int>>( 0 );

    discv4::DialFn count_fn = [dial_count](
        discv4::ValidatedPeer,
        std::function<void()>                                    on_done,
        std::function<void( std::shared_ptr<rlpx::RlpxSession> )>,
        boost::asio::yield_context ) noexcept
    {
        ++( *dial_count );
        on_done();
    };

    auto sched = std::make_shared<discv4::DialScheduler>( io, pool, count_fn );
    return { sched, dial_count };
}

// ---------------------------------------------------------------------------
// Known Sepolia fork hash (CRC32 of Sepolia genesis + applied forks).
// Value from go-ethereum params/config.go / forkid tests.
// ---------------------------------------------------------------------------
constexpr std::array<uint8_t, 4U> kSepoliaForkHash{ 0xfe, 0x33, 0x66, 0xe7 };

} // namespace

// ---------------------------------------------------------------------------
// make_fork_id_filter tests
// ---------------------------------------------------------------------------

/// @brief Filter accepts a peer whose hash exactly matches.
TEST( ForkIdFilterTest, AcceptsMatchingHash )
{
    const discv4::ForkId   fork{ kSepoliaForkHash, 0ULL };
    const discv4::FilterFn filter = discv4::make_fork_id_filter( kSepoliaForkHash );

    discv4::DiscoveredPeer peer;
    peer.eth_fork_id = fork;

    EXPECT_TRUE( filter( peer ) );
}

/// @brief Filter rejects a peer whose hash differs.
TEST( ForkIdFilterTest, RejectsMismatchedHash )
{
    const discv4::ForkId   wrong_fork{ { 0x01U, 0x02U, 0x03U, 0x04U }, 0ULL };
    const discv4::FilterFn filter = discv4::make_fork_id_filter( kSepoliaForkHash );

    discv4::DiscoveredPeer peer;
    peer.eth_fork_id = wrong_fork;

    EXPECT_FALSE( filter( peer ) );
}

/// @brief Filter rejects a peer with no eth_fork_id (ENR absent or no eth entry).
TEST( ForkIdFilterTest, RejectsPeerWithNoForkId )
{
    const discv4::FilterFn filter = discv4::make_fork_id_filter( kSepoliaForkHash );

    discv4::DiscoveredPeer peer;
    // eth_fork_id default-constructed = std::nullopt

    EXPECT_FALSE( filter( peer ) );
}

/// @brief Filter does not care about the `next` field — only the hash matters.
TEST( ForkIdFilterTest, IgnoresNextField )
{
    const discv4::FilterFn filter = discv4::make_fork_id_filter( kSepoliaForkHash );

    discv4::DiscoveredPeer peer;
    peer.eth_fork_id = discv4::ForkId{ kSepoliaForkHash, 999999999ULL };

    EXPECT_TRUE( filter( peer ) )
        << "make_fork_id_filter must match on hash only, not the next field";
}

// ---------------------------------------------------------------------------
// DialScheduler::filter_fn integration tests
// ---------------------------------------------------------------------------

/// @brief Without filter_fn set, all peers are dialed.
TEST( DialSchedulerFilterTest, NoFilterDialsAllPeers )
{
    boost::asio::io_context io;
    auto [sched, dial_count] = make_counting_scheduler( io );

    sched->enqueue( make_peer( 1U ) );
    sched->enqueue( make_peer( 2U ) );
    sched->enqueue( make_peer( 3U ) );

    io.run();

    EXPECT_EQ( dial_count->load(), 3 )
        << "All peers must be dialed when no filter is set";
}

/// @brief With a ForkId filter, only matching peers consume a dial slot.
TEST( DialSchedulerFilterTest, FilterDropsNonMatchingPeers )
{
    boost::asio::io_context io;
    auto [sched, dial_count] = make_counting_scheduler( io );
    sched->filter_fn = discv4::make_fork_id_filter( kSepoliaForkHash );

    // Peer 1: correct chain.
    sched->enqueue( make_peer( 1U, discv4::ForkId{ kSepoliaForkHash, 0ULL } ) );
    // Peer 2: wrong chain.
    sched->enqueue( make_peer( 2U, discv4::ForkId{ { 0xAAU, 0xBBU, 0xCCU, 0xDDU }, 0ULL } ) );
    // Peer 3: no ENR.
    sched->enqueue( make_peer( 3U ) );
    // Peer 4: correct chain.
    sched->enqueue( make_peer( 4U, discv4::ForkId{ kSepoliaForkHash, 12345ULL } ) );

    io.run();

    EXPECT_EQ( dial_count->load(), 2 )
        << "Only the two Sepolia peers must be dialed; wrong-chain and no-ENR peers must be dropped";
}

/// @brief A filter that rejects everything results in zero dials.
TEST( DialSchedulerFilterTest, FilterRejectingAllYieldsZeroDials )
{
    boost::asio::io_context io;
    auto [sched, dial_count] = make_counting_scheduler( io );
    // Reject every peer regardless of ForkId.
    sched->filter_fn = []( const discv4::DiscoveredPeer& ) -> bool { return false; };

    sched->enqueue( make_peer( 1U, discv4::ForkId{ kSepoliaForkHash, 0ULL } ) );
    sched->enqueue( make_peer( 2U, discv4::ForkId{ kSepoliaForkHash, 0ULL } ) );

    io.run();

    EXPECT_EQ( dial_count->load(), 0 )
        << "A reject-all filter must result in zero dial attempts";
}

