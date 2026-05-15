// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_receipt_source.hpp>
#include <algorithm>

namespace eth::rpc {

RpcReceiptSource::RpcReceiptSource(
    JsonRpcTransport& transport,
    FinalityPolicy    finality_policy,
    uint64_t          last_processed_block,
    uint64_t          max_log_range)
    : transport_(transport)
    , finality_policy_(finality_policy)
    , last_processed_block_(last_processed_block)
    , max_log_range_(std::max<uint64_t>(1, max_log_range))
{
}

WatchId RpcReceiptSource::add_filter(EventFilter filter)
{
    const WatchId id = next_watch_id_++;
    filters_.push_back({id, std::move(filter)});
    return id;
}

void RpcReceiptSource::remove_filter(WatchId id)
{
    filters_.erase(
        std::remove_if(
            filters_.begin(),
            filters_.end(),
            [id](const RegisteredFilter& entry)
            {
                return entry.id == id;
            }),
        filters_.end());
}

void RpcReceiptSource::set_receipt_batch_handler(ReceiptBatchHandler handler)
{
    handler_ = std::move(handler);
}

std::optional<ReceiptResult> RpcReceiptSource::get_receipt(const Hash256& tx_hash)
{
    const auto response = transport_.call(
        make_get_transaction_receipt_request(tx_hash, next_request_id()));
    if (!response.has_value())
    {
        return std::nullopt;
    }
    return parse_transaction_receipt_response(*response);
}

std::optional<FinalityDecision> RpcReceiptSource::finality_head()
{
    ChainHeadSnapshot heads;
    if (finality_policy_.prefer_finalized)
    {
        heads.finalized_number = get_block_number(RpcBlockTag::kFinalized);
    }
    if (finality_policy_.prefer_safe)
    {
        heads.safe_number = get_block_number(RpcBlockTag::kSafe);
    }
    heads.latest_number = get_block_number(RpcBlockTag::kLatest);

    const auto decision = choose_finality_head(finality_policy_, heads);
    if (!decision)
    {
        return std::nullopt;
    }
    return decision;
}

bool RpcReceiptSource::poll_once()
{
    const auto head = finality_head();
    if (!head.has_value())
    {
        return false;
    }
    if (head->block_number <= last_processed_block_)
    {
        return true;
    }
    return backfill(last_processed_block_ + 1, head->block_number);
}

bool RpcReceiptSource::backfill(uint64_t from_block, uint64_t to_block)
{
    if (from_block > to_block)
    {
        return true;
    }

    for (uint64_t range_start = from_block; range_start <= to_block;)
    {
        const uint64_t range_end = range_start + std::min(
            max_log_range_ - 1,
            to_block - range_start);
        std::set<Hash256> seen_transactions;

        for (const auto& entry : filters_)
        {
            if (!backfill_filter(entry.filter, range_start, range_end, seen_transactions))
            {
                return false;
            }
        }

        last_processed_block_ = range_end;
        if (range_end == to_block)
        {
            break;
        }
        range_start = range_end + 1;
    }

    return true;
}

uint64_t RpcReceiptSource::next_request_id() noexcept
{
    return next_rpc_id_++;
}

std::optional<uint64_t> RpcReceiptSource::get_block_number(RpcBlockTag tag)
{
    const auto response = transport_.call(
        make_get_block_by_number_request(tag, next_request_id()));
    if (!response.has_value())
    {
        return std::nullopt;
    }
    return parse_block_number_response(*response);
}

bool RpcReceiptSource::backfill_filter(
    const EventFilter& filter,
    uint64_t           from_block,
    uint64_t           to_block,
    std::set<Hash256>& seen_transactions)
{
    const auto response = transport_.call(
        make_get_logs_request(filter, from_block, to_block, next_request_id()));
    if (!response.has_value())
    {
        return false;
    }

    const auto logs = parse_get_logs_response(*response);
    if (!logs.has_value())
    {
        return false;
    }

    for (const auto& log : *logs)
    {
        if (!seen_transactions.insert(log.tx_hash).second)
        {
            continue;
        }
        if (!emit_receipt(log.tx_hash))
        {
            return false;
        }
    }

    return true;
}

bool RpcReceiptSource::emit_receipt(const Hash256& tx_hash)
{
    const auto receipt = get_receipt(tx_hash);
    if (!receipt.has_value())
    {
        return false;
    }

    if (handler_)
    {
        ReceiptBatch batch;
        batch.receipts.push_back(receipt->receipt);
        batch.tx_hashes.push_back(receipt->tx_hash);
        batch.log_indices.push_back(receipt->log_indices);
        batch.block_number = receipt->block_number;
        batch.block_hash = receipt->block_hash;
        handler_(batch);
    }

    return true;
}

} // namespace eth::rpc
