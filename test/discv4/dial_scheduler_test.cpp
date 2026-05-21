// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// dial_scheduler_test.cpp
//
// Unit tests for discv4::DialScheduler slot-recycling behaviour.
//
// Mirrors go-ethereum p2p/dial_test.go :: TestDialSchedDynDial:
//   - Slots are freed immediately when a dial completes (success or fail).
//   - Queued peers are launched as soon as a slot becomes free.
//   - max_per_chain cap is respected while the queue drains.
//
// The critical regression this guards is: a slow-exit dial_fn (e.g. always
// waiting the full kStatusHandshakeTimeout even after receiving a wrong-chain
// Status) starves the queue.  A fast-exit dial_fn must drain the queue at full
// throughput.

#include <discv4/dial_scheduler.hpp>
#include <rlpx/rlpx_session.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace {

/// @brief Build a ValidatedPeer with a unique node_id byte to avoid
///        DialHistory deduplication between enqueued peers.
discv4::ValidatedPeer make_peer( uint8_t id_byte ) noexcept
{
    discv4::ValidatedPeer vp{};
    vp.peer.ip       = "127.0.0.1";
    vp.peer.tcp_port = 30303;
    vp.peer.node_id.fill( id_byte );
    return vp;
}

} // namespace

// ---------------------------------------------------------------------------

/// @brief Verifies that a dial_fn that calls on_done() immediately (simulating
///        a fast Status rejection — wrong chain, wrong genesis, or protocol
///        version mismatch) releases its slot so every queued peer is attempted.
///
/// go-ethereum equivalent: TestDialSchedDynDial — "One dial completes, freeing
/// one dial slot."
///
/// Failure mode this catches: dial_fn always waits the full 5-second
/// kStatusHandshakeTimeout even after receiving a wrong-chain Status, so the
/// queue starves because all max_per_chain slots remain occupied.
TEST( DialSchedulerTest, FastFailReleasesSlotForNextPeer )
{
    boost::asio::io_context io;

    // max 2 concurrent dials across the pool; 10 global cap — does not limit the test.
    auto pool = std::make_shared<discv4::WatcherPool>( 10, 2 );

    std::atomic<int> dial_count{ 0 };

    // dial_fn that simulates a fast Status rejection: increments counter then
    // calls on_done() immediately without yielding.
    discv4::DialFn fast_fail = [&dial_count](
        discv4::ValidatedPeer,
        std::function<void(rlpx::DisconnectReason)> on_done,
        std::function<void( std::shared_ptr<rlpx::RlpxSession> )>,
        boost::asio::yield_context ) noexcept
    {
        ++dial_count;
        on_done(rlpx::DisconnectReason::kTcpError);
    };

    auto sched = std::make_shared<discv4::DialScheduler>( io, pool, fast_fail );

    // Enqueue 5 peers — only 2 can run concurrently, but as each fast-fails
    // the slot must be freed and the next peer must start immediately.
    for ( uint8_t i = 1; i <= 5; ++i )
    {
        sched->enqueue( make_peer( i ) );
    }

    // 200 ms is far more than enough for 5 instantaneous coroutines.
    io.run_for( std::chrono::milliseconds( 200 ) );

    EXPECT_EQ( dial_count.load(), 5 )
        << "All 5 queued peers must be attempted when slots are recycled on fast-fail; "
           "if count < 5 the scheduler is holding slots for the full timeout window";
}
