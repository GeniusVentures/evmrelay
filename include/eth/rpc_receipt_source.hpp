// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_RECEIPT_SOURCE_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_RECEIPT_SOURCE_HPP

#include <eth/eth_receipt_source.hpp>
#include <eth/finality_policy.hpp>
#include <eth/json_rpc.hpp>
#include <boost/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace eth::rpc {

class JsonRpcTransport
{
public:
    virtual ~JsonRpcTransport() = default;

    [[nodiscard]] virtual std::optional<std::string> call(
        const boost::json::object& request) = 0;
};

class RpcReceiptSource final : public IEthReceiptSource
{
public:
    RpcReceiptSource(
        JsonRpcTransport& transport,
        FinalityPolicy    finality_policy,
        uint64_t          last_processed_block = 0,
        uint64_t          max_log_range = 1000);

    WatchId add_filter(EventFilter filter) override;
    void remove_filter(WatchId id) override;
    void set_receipt_batch_handler(ReceiptBatchHandler handler) override;
    [[nodiscard]] std::optional<ReceiptResult> get_receipt(const Hash256& tx_hash) override;

    [[nodiscard]] uint64_t last_processed_block() const noexcept
    {
        return last_processed_block_;
    }

    [[nodiscard]] std::optional<FinalityDecision> finality_head();
    bool poll_once();
    bool backfill(uint64_t from_block, uint64_t to_block);

private:
    struct RegisteredFilter
    {
        WatchId     id = 0;
        EventFilter filter;
    };

    [[nodiscard]] uint64_t next_request_id() noexcept;
    [[nodiscard]] std::optional<uint64_t> get_block_number(RpcBlockTag tag);
    bool backfill_filter(
        const EventFilter& filter,
        uint64_t           from_block,
        uint64_t           to_block,
        std::set<Hash256>& seen_transactions);
    bool emit_receipt(const Hash256& tx_hash);

    JsonRpcTransport&             transport_;
    FinalityPolicy                finality_policy_;
    uint64_t                      last_processed_block_ = 0;
    uint64_t                      max_log_range_ = 1000;
    uint64_t                      next_rpc_id_ = 1;
    WatchId                       next_watch_id_ = 1;
    std::vector<RegisteredFilter> filters_;
    ReceiptBatchHandler           handler_;
};

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_RECEIPT_SOURCE_HPP
