// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_watch_service.hpp>
#include <eth/messages.hpp>

#include <chrono>
#include <functional>
#include <vector>

namespace {

template <typename Array>
Array make_filled(uint8_t seed)
{
    Array value{};
    for (size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

// Build a 32-byte ABI word with an address in the rightmost 20 bytes
eth::codec::Hash256 make_address_word(const eth::codec::Address& addr)
{
    eth::codec::Hash256 word{};
    std::copy(addr.begin(), addr.end(), word.begin() + 12);
    return word;
}

// Append a uint64 as a big-endian uint256 (32 bytes) to a buffer
void append_uint256(eth::codec::ByteBuffer& buf, uint64_t value)
{
    for (int i = 0; i < 24; ++i) { buf.push_back(0); }
    for (int i = 7; i >= 0; --i)
    {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

/// Build a minimal Transfer log entry with ABI-encoded fields.
eth::codec::LogEntry make_transfer_log(
    const eth::codec::Address& token,
    const eth::codec::Address& from,
    const eth::codec::Address& to,
    uint64_t                   amount)
{
    eth::codec::LogEntry log;
    log.address = token;
    log.topics.push_back(eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    log.topics.push_back(make_address_word(from));
    log.topics.push_back(make_address_word(to));
    append_uint256(log.data, amount);
    return log;
}

discv4::ValidatedPeer make_validated_peer(uint8_t seed)
{
    discv4::ValidatedPeer peer{};
    for (size_t i = 0; i < peer.peer.node_id.size(); ++i)
    {
        peer.peer.node_id[i] = static_cast<uint8_t>(seed + i);
    }
    std::copy(peer.peer.node_id.begin(), peer.peer.node_id.end(), peer.pubkey.begin());
    peer.peer.ip = "127.0.0.1";
    peer.peer.udp_port = static_cast<uint16_t>(30300 + seed);
    peer.peer.tcp_port = static_cast<uint16_t>(30400 + seed);
    discv4::ForkId fork_id{};
    fork_id.hash = {0x11, 0x22, 0x33, 0x44};
    peer.peer.eth_fork_id = fork_id;
    return peer;
}

std::string make_enode(const std::string& ip, uint16_t port, char fill)
{
    return std::string("enode://")
        + std::string(128, fill)
        + "@"
        + ip
        + ":"
        + std::to_string(port);
}

discv4::ChainPeerConfig make_chain_config(
    std::string chain_name,
    std::vector<discv4::ValidatedPeer> nodes,
    std::vector<discv4::ValidatedPeer> bootnodes = {})
{
    discv4::ChainPeerConfig config{};
    config.canonical_name = std::move(chain_name);
    config.network_id = 100;
    config.genesis_hash = make_filled<eth::Hash256>(0x42);
    eth::ForkId fork_id{};
    fork_id.fork_hash = {0x11, 0x22, 0x33, 0x44};
    config.fork_id = fork_id;
    config.nodes = std::move(nodes);
    config.bootnodes = std::move(bootnodes);
    return config;
}

discv4::DialFn no_op_dial_fn()
{
    return [](
        discv4::ValidatedPeer,
        std::function<void(rlpx::DisconnectReason)> done,
        std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
        boost::asio::yield_context)
    {
        done(rlpx::DisconnectReason::kTcpError);
    };
}

} // namespace

// ============================================================================
// EthWatchService — watch_event / unwatch
// ============================================================================

TEST(EthWatchServiceTest, WatchAndUnwatch)
{
    eth::EthWatchService svc;
    EXPECT_EQ(svc.subscription_count(), 0u);

    const auto token = make_filled<eth::codec::Address>(0xAA);
    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    auto id = svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) {});

    EXPECT_EQ(svc.subscription_count(), 1u);
    svc.unwatch(id);
    EXPECT_EQ(svc.subscription_count(), 0u);
}

// ============================================================================
// EthWatchService — process_receipts dispatches decoded callbacks
// ============================================================================

TEST(EthWatchServiceTest, ProcessReceiptsDecodesTransferEvent)
{
    const auto token = make_filled<eth::codec::Address>(0xCC);
    const auto from  = make_filled<eth::codec::Address>(0x11);
    const auto to    = make_filled<eth::codec::Address>(0x22);
    const uint64_t amount = 500000000ULL;

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    struct CallbackCapture {
        eth::MatchedEvent              event;
        std::vector<eth::abi::AbiValue> values;
        int                            call_count = 0;
    } capture;

    eth::EthWatchService svc;
    svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [&capture](const eth::MatchedEvent& ev, const std::vector<eth::abi::AbiValue>& vals)
        {
            capture.event  = ev;
            capture.values = vals;
            ++capture.call_count;
        });

    eth::codec::Receipt receipt;
    receipt.status              = true;
    receipt.cumulative_gas_used = intx::uint256(21000);
    receipt.bloom               = {};
    receipt.logs.push_back(make_transfer_log(token, from, to, amount));

    const eth::Hash256 tx_hash    = make_filled<eth::Hash256>(0xDD);
    const eth::Hash256 block_hash = make_filled<eth::Hash256>(0xEE);

    svc.process_receipts({receipt}, {tx_hash}, 100, block_hash);

    EXPECT_EQ(capture.call_count, 1);
    EXPECT_EQ(capture.event.block_number, 100u);
    EXPECT_EQ(capture.event.block_hash,   block_hash);
    EXPECT_EQ(capture.event.tx_hash,      tx_hash);

    ASSERT_EQ(capture.values.size(), 3u);

    const auto* decoded_from = std::get_if<eth::codec::Address>(&capture.values[0]);
    ASSERT_NE(decoded_from, nullptr);
    EXPECT_EQ(*decoded_from, from);

    const auto* decoded_to = std::get_if<eth::codec::Address>(&capture.values[1]);
    ASSERT_NE(decoded_to, nullptr);
    EXPECT_EQ(*decoded_to, to);

    const auto* decoded_val = std::get_if<intx::uint256>(&capture.values[2]);
    ASSERT_NE(decoded_val, nullptr);
    EXPECT_EQ(*decoded_val, intx::uint256(amount));
}

TEST(EthWatchServiceTest, ProcessReceiptsIgnoresWrongContract)
{
    const auto watched_token  = make_filled<eth::codec::Address>(0xAA);
    const auto other_token    = make_filled<eth::codec::Address>(0xBB);
    const auto from           = make_filled<eth::codec::Address>(0x11);
    const auto to             = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int call_count = 0;
    eth::EthWatchService svc;
    svc.watch_event(watched_token, "Transfer(address,address,uint256)", params,
        [&call_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&)
        {
            ++call_count;
        });

    // Receipt from a different contract
    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(other_token, from, to, 100ULL));

    svc.process_receipts({receipt}, {{}}, 1, {});
    EXPECT_EQ(call_count, 0);
}

TEST(EthWatchServiceTest, ProcessReceiptsMultipleLogsMultipleSubscribers)
{
    const auto token_a = make_filled<eth::codec::Address>(0xAA);
    const auto token_b = make_filled<eth::codec::Address>(0xBB);
    const auto from    = make_filled<eth::codec::Address>(0x11);
    const auto to      = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int count_a = 0;
    int count_b = 0;

    eth::EthWatchService svc;
    svc.watch_event(token_a, "Transfer(address,address,uint256)", params,
        [&count_a](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++count_a; });
    svc.watch_event(token_b, "Transfer(address,address,uint256)", params,
        [&count_b](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++count_b; });

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token_a, from, to, 100ULL));
    receipt.logs.push_back(make_transfer_log(token_b, from, to, 200ULL));
    receipt.logs.push_back(make_transfer_log(token_a, from, to, 300ULL));

    svc.process_receipts({receipt}, {{}}, 1, {});

    EXPECT_EQ(count_a, 2);
    EXPECT_EQ(count_b, 1);
}

TEST(EthWatchServiceTest, AnyContractWatchWithZeroAddress)
{
    // Zero address = watch any contract
    const eth::codec::Address zero_addr{};

    const auto token_a = make_filled<eth::codec::Address>(0xAA);
    const auto token_b = make_filled<eth::codec::Address>(0xBB);
    const auto from    = make_filled<eth::codec::Address>(0x11);
    const auto to      = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int call_count = 0;
    eth::EthWatchService svc;
    svc.watch_event(zero_addr, "Transfer(address,address,uint256)", params,
        [&call_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++call_count; });

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token_a, from, to, 1ULL));
    receipt.logs.push_back(make_transfer_log(token_b, from, to, 2ULL));

    svc.process_receipts({receipt}, {{}}, 1, {});
    EXPECT_EQ(call_count, 2);
}

TEST(EthWatchServiceTest, ProcessReceiptsUsesBlockWideLogIndexes)
{
    const auto token = make_filled<eth::codec::Address>(0xCC);
    const auto from  = make_filled<eth::codec::Address>(0x11);
    const auto to    = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    std::vector<uint32_t> log_indexes;
    eth::EthWatchService svc;
    svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [&log_indexes](const eth::MatchedEvent& ev, const std::vector<eth::abi::AbiValue>&)
        {
            log_indexes.push_back(ev.log_index);
        });

    eth::codec::Receipt first;
    first.status = true;
    first.logs.push_back(make_transfer_log(token, from, to, 1ULL));

    eth::codec::Receipt second;
    second.status = true;
    second.logs.push_back(make_transfer_log(token, from, to, 2ULL));
    second.logs.push_back(make_transfer_log(token, from, to, 3ULL));

    const auto tx_a = make_filled<eth::Hash256>(0xA0);
    const auto tx_b = make_filled<eth::Hash256>(0xB0);
    svc.process_receipts({first, second}, {tx_a, tx_b}, 1, {});

    ASSERT_EQ(log_indexes.size(), 3U);
    EXPECT_EQ(log_indexes[0], 0U);
    EXPECT_EQ(log_indexes[1], 1U);
    EXPECT_EQ(log_indexes[2], 2U);
}

// ============================================================================
// EthWatchService — process_message (wire message dispatch)
// ============================================================================

TEST(EthWatchServiceTest, ProcessMessageReceiptsDispatchesCallbacks)
{
    const auto token = make_filled<eth::codec::Address>(0xCC);
    const auto from  = make_filled<eth::codec::Address>(0x11);
    const auto to    = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int call_count = 0;
    eth::EthWatchService svc;
    svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [&call_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++call_count; });

    // Build a ReceiptsMessage with one block's receipts containing a Transfer log
    eth::ReceiptsMessage receipts_msg;
    receipts_msg.request_id = 1;

    eth::codec::Receipt receipt;
    receipt.status              = true;
    receipt.cumulative_gas_used = intx::uint256(21000);
    receipt.bloom               = {};
    receipt.logs.push_back(make_transfer_log(token, from, to, 999ULL));
    receipts_msg.receipts.push_back({receipt});

    auto encoded = eth::protocol::encode_receipts(receipts_msg);
    ASSERT_TRUE(encoded.has_value());

    svc.process_message(
        eth::protocol::kReceiptsMessageId,
        rlp::ByteView(encoded.value().data(), encoded.value().size()));

    EXPECT_EQ(call_count, 1);
}

TEST(EthWatchServiceTest, ProcessMessageUnknownIdIsIgnored)
{
    eth::EthWatchService svc;
    // Should not crash on unknown message id
    const std::vector<uint8_t> garbage = {0x01, 0x02, 0x03};
    svc.process_message(0xFF, rlp::ByteView(garbage.data(), garbage.size()));
}

// ============================================================================
// EthWatchService — set_send_callback / request flow
// ============================================================================

TEST(EthWatchServiceTest, SetSendCallbackCalledOnNewBlockHashes)
{
    eth::EthWatchService svc;

    struct Capture {
        uint8_t              msg_id = 0;
        std::vector<uint8_t> payload;
        int                  call_count = 0;
    } capture;

    svc.set_send_callback([&capture](uint8_t id, std::vector<uint8_t> p)
    {
        capture.msg_id     = id;
        capture.payload    = std::move(p);
        ++capture.call_count;
    });

    // Build a NewBlockHashes message with two entries
    eth::NewBlockHashesMessage nbh;
    nbh.entries.push_back({make_filled<eth::Hash256>(0x01), 100});
    nbh.entries.push_back({make_filled<eth::Hash256>(0x02), 101});

    auto encoded = eth::protocol::encode_new_block_hashes(nbh);
    ASSERT_TRUE(encoded.has_value());

    svc.process_message(
        eth::protocol::kNewBlockHashesMessageId,
        rlp::ByteView(encoded.value().data(), encoded.value().size()));

    // Should have emitted one GetReceipts per block hash
    EXPECT_EQ(capture.call_count, 2);
    EXPECT_EQ(capture.msg_id, eth::protocol::kGetReceiptsMessageId);
}

TEST(EthWatchServiceTest, NoSendCallbackDoesNotCrashOnNewBlockHashes)
{
    eth::EthWatchService svc;
    // No send callback registered — should silently do nothing

    eth::NewBlockHashesMessage nbh;
    nbh.entries.push_back({make_filled<eth::Hash256>(0x01), 100});

    auto encoded = eth::protocol::encode_new_block_hashes(nbh);
    ASSERT_TRUE(encoded.has_value());

    // Should not crash
    svc.process_message(
        eth::protocol::kNewBlockHashesMessageId,
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
}

TEST(EthWatchServiceTest, ReceiptsCorrelatedToRequestId)
{
    const auto token = make_filled<eth::codec::Address>(0xCC);
    const auto from  = make_filled<eth::codec::Address>(0x11);
    const auto to    = make_filled<eth::codec::Address>(0x22);
    const auto block_hash = make_filled<eth::Hash256>(0xBB);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int call_count = 0;
    uint64_t received_block_number = 0;

    eth::EthWatchService svc;

    // Capture the GetReceipts payload so we can build a correlated response
    std::vector<uint8_t> get_receipts_payload;
    svc.set_send_callback([&get_receipts_payload](uint8_t, std::vector<uint8_t> p)
    {
        get_receipts_payload = std::move(p);
    });

    svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [&call_count, &received_block_number](
            const eth::MatchedEvent& ev,
            const std::vector<eth::abi::AbiValue>&)
        {
            ++call_count;
            received_block_number = ev.block_number;
        });

    // Trigger a GetReceipts by announcing a new block hash
    eth::NewBlockHashesMessage nbh;
    nbh.entries.push_back({block_hash, 999});
    auto nbh_encoded = eth::protocol::encode_new_block_hashes(nbh);
    ASSERT_TRUE(nbh_encoded.has_value());

    svc.process_message(
        eth::protocol::kNewBlockHashesMessageId,
        rlp::ByteView(nbh_encoded.value().data(), nbh_encoded.value().size()));

    ASSERT_FALSE(get_receipts_payload.empty());

    // Decode the GetReceipts to get the request_id
    auto get_req = eth::protocol::decode_get_receipts(
        rlp::ByteView(get_receipts_payload.data(), get_receipts_payload.size()));
    ASSERT_TRUE(get_req.has_value());
    ASSERT_TRUE(get_req.value().request_id.has_value());

    const uint64_t req_id = get_req.value().request_id.value();

    // Build the correlated Receipts response
    eth::ReceiptsMessage resp;
    resp.request_id = req_id;

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token, from, to, 42ULL));
    resp.receipts.push_back({receipt});

    auto resp_encoded = eth::protocol::encode_receipts(resp);
    ASSERT_TRUE(resp_encoded.has_value());

    svc.process_message(
        eth::protocol::kReceiptsMessageId,
        rlp::ByteView(resp_encoded.value().data(), resp_encoded.value().size()));

    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(received_block_number, 999u);
}

TEST(EthWatchServiceTest, BlockRangeFilteringRespected)
{
    const auto token = make_filled<eth::codec::Address>(0xAA);
    const auto from  = make_filled<eth::codec::Address>(0x11);
    const auto to    = make_filled<eth::codec::Address>(0x22);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    int call_count = 0;
    eth::EthWatchService svc;
    svc.watch_event(token, "Transfer(address,address,uint256)", params,
        [&call_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++call_count; },
        /*from_block=*/100,
        /*to_block=*/200);

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token, from, to, 1ULL));

    svc.process_receipts({receipt}, {{}}, 50,  {});   // before range — no dispatch
    svc.process_receipts({receipt}, {{}}, 150, {});   // inside range — dispatched
    svc.process_receipts({receipt}, {{}}, 250, {});   // after range — no dispatch

    EXPECT_EQ(call_count, 1);
}

// ============================================================================
// EthWatchService — production orchestration
// ============================================================================

TEST(EthWatchServiceTest, InitializeRunCreatesSchedulerAndPeerQueueFromCachedNodes)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.chains.push_back(make_chain_config("test-chain", {make_validated_peer(0x10)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    EXPECT_EQ(svc.runtime_chain_count(), 1U);
    EXPECT_EQ(svc.scheduler_count(), 1U);
    EXPECT_EQ(svc.peer_queue_count(), 1U);
    EXPECT_EQ(svc.discovery_client_count(), 0U);

    auto queue = svc.peer_queue("test-chain");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->cached_peer_count(), 1U);
    EXPECT_FALSE(queue->needs_discovery());
}

TEST(EthWatchServiceTest, EmptyCachedNodesWithBootnodesStartsDiscv4Fallback)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.discovery.bind_port = 0;
    config.chains.push_back(make_chain_config(
        "gnosis-mainnet",
        {},
        {make_validated_peer(0x20)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue>)
    {
        return true;
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    EXPECT_EQ(svc.runtime_chain_count(), 1U);
    EXPECT_EQ(svc.scheduler_count(), 1U);
    EXPECT_EQ(svc.peer_queue_count(), 1U);
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);

    auto queue = svc.peer_queue("gnosis-mainnet");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->cached_peer_count(), 0U);
    EXPECT_EQ(queue->discovery_bootnodes().size(), 1U);
    EXPECT_TRUE(queue->needs_discovery());
}

TEST(EthWatchServiceTest, CacheOnlyModeUsesCachedNodesAndSkipsBootnodeDiscovery)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.discovery_mode = eth::EthWatchDiscoveryMode::kCacheOnly;
    config.chains.push_back(make_chain_config(
        "cache-only-chain",
        {make_validated_peer(0x21)},
        {make_validated_peer(0x22)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue>)
    {
        return true;
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("cache-only-chain");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->cached_peer_count(), 1U);
    EXPECT_EQ(queue->discovery_bootnodes().size(), 1U);
    EXPECT_EQ(svc.discv4_fallback_count(), 0U);
}

TEST(EthWatchServiceTest, DiscoverFirstModeStartsDiscoveryWithoutPreloadingCachedNodes)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.discovery_mode = eth::EthWatchDiscoveryMode::kDiscoverFirst;
    config.chains.push_back(make_chain_config(
        "discover-first-chain",
        {make_validated_peer(0x23)},
        {make_validated_peer(0x24)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue && queue->cached_peer_count() == 0U && queue->needs_discovery();
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("discover-first-chain");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->cached_peer_count(), 0U);
    EXPECT_EQ(queue->discovery_bootnodes().size(), 1U);
    EXPECT_TRUE(queue->needs_discovery());
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
}

TEST(EthWatchServiceTest, HybridModePreloadsCachedNodesAndStartsDiscovery)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.discovery_mode = eth::EthWatchDiscoveryMode::kHybrid;
    config.chains.push_back(make_chain_config(
        "hybrid-chain",
        {make_validated_peer(0x25)},
        {make_validated_peer(0x26)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue && queue->cached_peer_count() == 1U && !queue->discovery_bootnodes().empty();
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("hybrid-chain");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->cached_peer_count(), 1U);
    EXPECT_EQ(queue->discovery_bootnodes().size(), 1U);
    EXPECT_FALSE(queue->needs_discovery());
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
}

TEST(EthWatchServiceTest, LoadedGnosisConfigWithNoCachedNodesStartsDiscoveryFallback)
{
    boost::asio::io_context io;

    const std::string json_text = std::string("{")
        + "\"gnosis-chain\":{"
        + "\"networkId\":100,"
        + "\"genesisHex\":\"4f1dd23188aab3a0b3768e6a2b5f6cbf3fcb259af45d37b228a8a0ae61161f80\","
        + "\"forkId\":\"06000064\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":[],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '7') + "\"}"
        + "]}}";

    auto loaded_config = discv4::load_chain_peer_config_from_json_text(
        "gnosis-chain",
        json_text);
    ASSERT_TRUE(loaded_config.has_value());
    ASSERT_TRUE(loaded_config->nodes.empty());
    ASSERT_EQ(loaded_config->bootnodes.size(), 1U);

    eth::EthWatchServiceConfig config{};
    config.discovery.bind_port = 0;
    config.chains.push_back(*loaded_config);
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig& chain,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return chain.canonical_name == "gnosis-chain"
            && chain.network_id == 100U
            && chain.nodes.empty()
            && !chain.bootnodes.empty()
            && queue
            && queue->needs_discovery();
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("gnosis-chain");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
    EXPECT_EQ(queue->cached_peer_count(), 0U);
    ASSERT_EQ(queue->discovery_bootnodes().size(), 1U);
    EXPECT_EQ(queue->discovery_bootnodes().front().peer.ip, "10.0.0.2");
    EXPECT_EQ(queue->discovery_bootnodes().front().peer.tcp_port, 30304U);
    EXPECT_TRUE(queue->needs_discovery());
}

TEST(EthWatchServiceTest, Discv4FallbackDiscoveredPeerFeedsProductionDialQueue)
{
    boost::asio::io_context io;
    const auto discovered_peer = make_validated_peer(0x28);

    eth::EthWatchServiceConfig config{};
    config.connection.max_total_connections = 0;
    config.connection.max_connections_per_chain = 0;
    config.chains.push_back(make_chain_config(
        "gnosis-mainnet",
        {},
        {make_validated_peer(0x20)}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.discv4_fallback_starter = [discovered_peer](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue && queue->enqueue_discovered_peer(discovered_peer.peer);
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("gnosis-mainnet");
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(queue->scheduler(), nullptr);
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
    EXPECT_EQ(queue->discovered_peer_count(), 1U);
    EXPECT_EQ(queue->scheduler()->queue.size(), 1U);
}

TEST(EthWatchServiceTest, EnrTreeDiscoveryStartsDiscv5AndKeepsSeedsDiscoveryOnly)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.chains.push_back(make_chain_config("polygon-mainnet", {}, {}));
    config.chains.back().enr_trees.push_back(
        "enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@pos.polygon-peers.io");
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.enr_tree_resolver = [](
        const discv4::ChainPeerConfig&,
        const std::vector<std::string>& urls)
    {
        EXPECT_EQ(urls.size(), 1U);
        return std::vector<std::string>{"enr:-resolved"};
    };
    config.discv5_enr_tree_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig& chain,
        std::shared_ptr<eth::EthPeerQueue> queue,
        const std::vector<std::string>& bootstrap_enrs)
    {
        return chain.canonical_name == "polygon-mainnet"
            && queue
            && queue->cached_peer_count() == 0U
            && queue->discovery_bootnodes().empty()
            && bootstrap_enrs.size() == 1U
            && bootstrap_enrs.front() == "enr:-resolved";
    };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue>)
    {
        return false;
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("polygon-mainnet");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(svc.discv4_fallback_count(), 0U);
    EXPECT_EQ(queue->cached_peer_count(), 0U);
    EXPECT_TRUE(queue->discovery_bootnodes().empty());
}

TEST(EthWatchServiceTest, EnrTreeDiscoveryFallsBackToDiscv4BootnodesWhenResolutionIsEmpty)
{
    boost::asio::io_context io;

    eth::EthWatchServiceConfig config{};
    config.chains.push_back(make_chain_config(
        "polygon-mainnet",
        {},
        {make_validated_peer(0x44)}));
    config.chains.back().enr_trees.push_back(
        "enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@pos.polygon-peers.io");
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    config.enr_tree_resolver = [](
        const discv4::ChainPeerConfig&,
        const std::vector<std::string>&)
    {
        return std::vector<std::string>{};
    };
    config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue && queue->needs_discovery() && queue->discovery_bootnodes().size() == 1U;
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("polygon-mainnet");
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
    EXPECT_EQ(queue->discovery_bootnodes().size(), 1U);
}

TEST(EthWatchServiceTest, DiscoveryPeersQueueWhileDialSlotIsSaturatedAndDrainOnRelease)
{
    boost::asio::io_context io;
    std::vector<discv4::ValidatedPeer> dialed_peers;
    std::vector<std::function<void(rlpx::DisconnectReason)>> release_callbacks;

    const auto cached_peer = make_validated_peer(0x31);
    const auto discovered_peer_a = make_validated_peer(0x32);
    const auto discovered_peer_b = make_validated_peer(0x33);

    eth::EthWatchServiceConfig config{};
    config.connection.max_total_connections = 1;
    config.connection.max_connections_per_chain = 1;
    config.discovery_mode = eth::EthWatchDiscoveryMode::kHybrid;
    config.chains.push_back(make_chain_config(
        "saturated-chain",
        {cached_peer},
        {make_validated_peer(0x34)}));
    config.dial_fn_factory = [&dialed_peers, &release_callbacks](const discv4::ChainPeerConfig&)
    {
        return [&dialed_peers, &release_callbacks](
            discv4::ValidatedPeer peer,
            std::function<void(rlpx::DisconnectReason)> done,
            std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
            boost::asio::yield_context)
        {
            dialed_peers.push_back(peer);
            release_callbacks.push_back(std::move(done));
        };
    };
    config.discv4_fallback_starter = [discovered_peer_a, discovered_peer_b](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue
            && queue->enqueue_discovered_peer(discovered_peer_a.peer)
            && queue->enqueue_discovered_peer(discovered_peer_b.peer);
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("saturated-chain");
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(queue->scheduler(), nullptr);
    EXPECT_EQ(svc.discv4_fallback_count(), 1U);
    EXPECT_EQ(queue->discovered_peer_count(), 2U);
    EXPECT_EQ(queue->scheduler()->active, 1);
    EXPECT_EQ(queue->scheduler()->queue.size(), 2U);

    io.run_for(std::chrono::milliseconds(50));
    ASSERT_EQ(dialed_peers.size(), 1U);
    EXPECT_EQ(dialed_peers[0].peer.node_id, cached_peer.peer.node_id);
    ASSERT_EQ(release_callbacks.size(), 1U);

    release_callbacks[0](rlpx::DisconnectReason::kTcpError);
    io.restart();
    io.run_for(std::chrono::milliseconds(50));

    ASSERT_EQ(dialed_peers.size(), 2U);
    EXPECT_EQ(dialed_peers[1].peer.node_id, discovered_peer_a.peer.node_id);
    EXPECT_EQ(queue->scheduler()->active, 1);
    EXPECT_EQ(queue->scheduler()->queue.size(), 1U);

    release_callbacks[1](rlpx::DisconnectReason::kTcpError);
    io.restart();
    io.run_for(std::chrono::milliseconds(50));

    ASSERT_EQ(dialed_peers.size(), 3U);
    EXPECT_EQ(dialed_peers[2].peer.node_id, discovered_peer_b.peer.node_id);
    EXPECT_EQ(queue->scheduler()->active, 1);
    EXPECT_TRUE(queue->scheduler()->queue.empty());
}

TEST(EthWatchServiceTest, DiscoveryCanContinueProducingPeersAfterDialFailureReleasesSlot)
{
    boost::asio::io_context io;
    std::vector<discv4::ValidatedPeer> dialed_peers;
    std::vector<std::function<void(rlpx::DisconnectReason)>> release_callbacks;

    const auto first_peer = make_validated_peer(0x35);
    const auto later_peer = make_validated_peer(0x36);

    eth::EthWatchServiceConfig config{};
    config.connection.max_total_connections = 1;
    config.connection.max_connections_per_chain = 1;
    config.chains.push_back(make_chain_config(
        "continued-discovery-chain",
        {},
        {make_validated_peer(0x37)}));
    config.dial_fn_factory = [&dialed_peers, &release_callbacks](const discv4::ChainPeerConfig&)
    {
        return [&dialed_peers, &release_callbacks](
            discv4::ValidatedPeer peer,
            std::function<void(rlpx::DisconnectReason)> done,
            std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
            boost::asio::yield_context)
        {
            dialed_peers.push_back(peer);
            release_callbacks.push_back(std::move(done));
        };
    };
    config.discv4_fallback_starter = [first_peer](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig&,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return queue && queue->enqueue_discovered_peer(first_peer.peer);
    };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("continued-discovery-chain");
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(queue->scheduler(), nullptr);

    io.run_for(std::chrono::milliseconds(50));
    ASSERT_EQ(dialed_peers.size(), 1U);
    EXPECT_EQ(dialed_peers[0].peer.node_id, first_peer.peer.node_id);
    ASSERT_EQ(release_callbacks.size(), 1U);

    release_callbacks[0](rlpx::DisconnectReason::kTcpError);
    EXPECT_EQ(queue->scheduler()->active, 0);
    EXPECT_TRUE(queue->enqueue_discovered_peer(later_peer.peer));
    EXPECT_EQ(queue->discovered_peer_count(), 2U);

    io.restart();
    io.run_for(std::chrono::milliseconds(50));

    ASSERT_EQ(dialed_peers.size(), 2U);
    EXPECT_EQ(dialed_peers[1].peer.node_id, later_peer.peer.node_id);
    EXPECT_EQ(queue->scheduler()->active, 1);
}

TEST(EthWatchServiceTest, SchedulerFeedbackRequeuesThroughProductionPeerQueue)
{
    boost::asio::io_context io;
    const auto peer = make_validated_peer(0x30);

    eth::EthWatchServiceConfig config{};
    config.connection.max_total_connections = 0;
    config.connection.max_connections_per_chain = 0;
    config.chains.push_back(make_chain_config("feedback-chain", {peer}));
    config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };

    eth::EthWatchService svc;
    ASSERT_TRUE(svc.initialize(std::move(config), [](const eth::WatchEventNotification&) {}));
    svc.run(io);

    auto queue = svc.peer_queue("feedback-chain");
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(queue->scheduler(), nullptr);
    ASSERT_TRUE(static_cast<bool>(queue->scheduler()->feedback_fn));

    queue->scheduler()->feedback_fn(peer, rlpx::DisconnectReason::kTooManyPeers, true);

    EXPECT_EQ(queue->requeued_peer_count(), 0U);
    EXPECT_EQ(queue->too_many_peers_backoff_count(), 1U);
    EXPECT_EQ(queue->scheduler()->queue.size(), 1U);
}

TEST(EthWatchServiceTest, InitializeRejectsIncompleteRuntimeConfig)
{
    eth::EthWatchService svc;

    eth::EthWatchServiceConfig empty_config{};
    EXPECT_FALSE(svc.initialize(std::move(empty_config), [](const eth::WatchEventNotification&) {}));

    eth::EthWatchServiceConfig no_peers_config{};
    no_peers_config.chains.push_back(make_chain_config("no-peers", {}, {}));
    EXPECT_FALSE(svc.initialize(std::move(no_peers_config), [](const eth::WatchEventNotification&) {}));

    eth::EthWatchServiceConfig disabled_discovery_config{};
    disabled_discovery_config.discovery_mode = eth::EthWatchDiscoveryMode::kDiscoverFirst;
    disabled_discovery_config.enable_discv4_fallback = false;
    disabled_discovery_config.chains.push_back(make_chain_config(
        "disabled-discovery",
        {make_validated_peer(0x40)},
        {make_validated_peer(0x41)}));
    EXPECT_FALSE(svc.initialize(std::move(disabled_discovery_config), [](const eth::WatchEventNotification&) {}));
}
