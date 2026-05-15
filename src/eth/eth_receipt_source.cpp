// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_receipt_source.hpp>
#include <algorithm>

namespace eth {

EventFilter make_event_filter(
    const codec::Address&   contract_address,
    const std::string&      event_signature,
    std::optional<uint64_t> from_block,
    std::optional<uint64_t> to_block)
{
    EventFilter filter;

    const codec::Address zero_addr{};
    if (contract_address != zero_addr)
    {
        filter.addresses.push_back(contract_address);
    }

    filter.topics.push_back(abi::event_signature_hash(event_signature));
    filter.from_block = from_block;
    filter.to_block = to_block;
    return filter;
}

EthReceiptSourceBridge::EthReceiptSourceBridge(
    EthWatchService&   service,
    IEthReceiptSource& source)
    : service_(service)
    , source_(source)
{
    source_.set_receipt_batch_handler([this](const ReceiptBatch& batch)
    {
        process_receipt_batch(batch);
    });
}

EventWatchId EthReceiptSourceBridge::watch_event(
    const codec::Address&             contract_address,
    const std::string&                event_signature,
    const std::vector<abi::AbiParam>& params,
    DecodedEventCallback              callback,
    std::optional<uint64_t>           from_block,
    std::optional<uint64_t>           to_block)
{
    const auto filter = make_event_filter(contract_address, event_signature, from_block, to_block);
    const auto source_id = source_.add_filter(filter);
    const auto service_id = service_.watch_event(
        contract_address,
        event_signature,
        params,
        std::move(callback),
        from_block,
        to_block);

    subscriptions_.push_back({service_id, source_id});
    return service_id;
}

void EthReceiptSourceBridge::unwatch(EventWatchId id)
{
    service_.unwatch(id);

    const auto it = std::find_if(
        subscriptions_.begin(),
        subscriptions_.end(),
        [id](const Subscription& sub)
        {
            return sub.service_id == id;
        });
    if (it == subscriptions_.end())
    {
        return;
    }

    source_.remove_filter(it->source_id);
    subscriptions_.erase(it);
}

void EthReceiptSourceBridge::process_receipt_batch(const ReceiptBatch& batch)
{
    service_.process_receipts(
        batch.receipts,
        batch.tx_hashes,
        batch.block_number,
        batch.block_hash,
        batch.log_indices);
}

} // namespace eth
