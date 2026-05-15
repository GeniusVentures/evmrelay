// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_RECEIPT_SOURCE_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_RECEIPT_SOURCE_HPP

#include <eth/eth_watch_service.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace eth {

/// @brief Receipt plus the chain context needed to verify and deduplicate a log.
struct ReceiptResult
{
    codec::Receipt       receipt;
    Hash256              tx_hash{};
    uint64_t             block_number = 0;
    Hash256              block_hash{};
    std::vector<uint32_t> log_indices;
};

/// @brief Receipts from one block, normalized across RLPx, RPC, or certified sources.
struct ReceiptBatch
{
    std::vector<codec::Receipt>       receipts;
    std::vector<Hash256>              tx_hashes;
    std::vector<std::vector<uint32_t>> log_indices;
    uint64_t                          block_number = 0;
    Hash256                           block_hash{};
};

using ReceiptBatchHandler = std::function<void(const ReceiptBatch&)>;

/// @brief Transport-neutral source of receipt/log evidence for EthWatchService.
class IEthReceiptSource
{
public:
    virtual ~IEthReceiptSource() = default;

    /// @brief Let the source optimize acquisition around the same filter used by EthWatchService.
    virtual WatchId add_filter(EventFilter filter) = 0;

    /// @brief Remove a previously registered acquisition filter.
    virtual void remove_filter(WatchId id) = 0;

    /// @brief Set the handler that receives normalized receipt batches.
    virtual void set_receipt_batch_handler(ReceiptBatchHandler handler) = 0;

    /// @brief Fetch one receipt by transaction hash when the source supports lookup verification.
    [[nodiscard]] virtual std::optional<ReceiptResult> get_receipt(const Hash256& tx_hash) = 0;
};

/// @brief Connects a transport-neutral receipt source to the existing EthWatchService path.
class EthReceiptSourceBridge
{
public:
    EthReceiptSourceBridge(EthWatchService& service, IEthReceiptSource& source);

    EventWatchId watch_event(
        const codec::Address&             contract_address,
        const std::string&                event_signature,
        const std::vector<abi::AbiParam>& params,
        DecodedEventCallback              callback,
        std::optional<uint64_t>           from_block = std::nullopt,
        std::optional<uint64_t>           to_block   = std::nullopt);

    void unwatch(EventWatchId id);
    void process_receipt_batch(const ReceiptBatch& batch);

private:
    struct Subscription
    {
        EventWatchId service_id = 0;
        WatchId      source_id = 0;
    };

    EthWatchService&         service_;
    IEthReceiptSource&       source_;
    std::vector<Subscription> subscriptions_;
};

[[nodiscard]] EventFilter make_event_filter(
    const codec::Address&   contract_address,
    const std::string&      event_signature,
    std::optional<uint64_t> from_block = std::nullopt,
    std::optional<uint64_t> to_block   = std::nullopt);

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_RECEIPT_SOURCE_HPP
