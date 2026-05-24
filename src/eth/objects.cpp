// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/objects.hpp>
#include <eth/eth_constants.hpp>
#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_encoder.hpp>

namespace eth::codec {

namespace {

EncodeResult finalize_encoding(rlp::RlpEncoder& encoder) {
    auto result = encoder.GetBytes();
    if (!result) {
        return result.error();
    }
    return ByteBuffer(result.value()->begin(), result.value()->end());
}

rlp::Result<void> decode_status_or_state_root(rlp::RlpDecoder& decoder, Receipt& receipt) {
    auto header = decoder.PeekHeader();
    if (!header) {
        return header.error();
    }

    if (header.value().list) {
        return rlp::DecodingError::kUnexpectedList;
    }

    if (header.value().payload_size_bytes == Hash256{}.size()) {
        Hash256 root{};
        auto read_result = decoder.read(root);
        if (!read_result) {
            return read_result.error();
        }
        receipt.state_root = root;
        receipt.status.reset();
        return rlp::outcome::success();
    }

    rlp::Bytes status_bytes;
    auto status_result = decoder.read(status_bytes);
    if (!status_result) {
        return status_result.error();
    }

    if (status_bytes.empty()) {
        receipt.status = false;
        receipt.state_root.reset();
        return rlp::outcome::success();
    }

    if (status_bytes.size() == 1) {
        receipt.status = (status_bytes[0] != 0);
        receipt.state_root.reset();
        return rlp::outcome::success();
    }

    return rlp::DecodingError::kUnexpectedLength;
}

rlp::Result<LogEntry> decode_log_entry_from_decoder(rlp::RlpDecoder& decoder) {
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) {
        return list_size.error();
    }

    const size_t payload_size = list_size.value();
    const size_t start_remaining = decoder.Remaining().size();
    const size_t target_remaining = start_remaining - payload_size;

    LogEntry entry;
    if (!decoder.read(entry.address)) {
        return rlp::DecodingError::kUnexpectedLength;
    }

    auto topics_list_size = decoder.ReadListHeaderBytes();
    if (!topics_list_size) {
        return topics_list_size.error();
    }

    const size_t topics_payload = topics_list_size.value();
    const size_t topics_start = decoder.Remaining().size();
    const size_t topics_target = topics_start - topics_payload;

    while (decoder.Remaining().size() > topics_target) {
        Hash256 topic{};
        if (!decoder.read(topic)) {
            return rlp::DecodingError::kUnexpectedLength;
        }
        entry.topics.push_back(topic);
    }

    if (decoder.Remaining().size() != topics_target) {
        return rlp::DecodingError::kListLengthMismatch;
    }

    rlp::Bytes data_bytes;
    if (!decoder.read(data_bytes)) {
        return rlp::DecodingError::kUnexpectedString;
    }
    entry.data.assign(data_bytes.begin(), data_bytes.end());

    if (decoder.Remaining().size() != target_remaining) {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return entry;
}

// ---------------------------------------------------------------------------
// Access list helpers (internal)
// ---------------------------------------------------------------------------

rlp::EncodingOperationResult encode_access_list_entry_to_encoder(rlp::RlpEncoder& encoder, const AccessListEntry& entry)
{
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
    if (!encoder.add(rlp::ByteView(entry.address.data(), entry.address.size()))) { return rlp::EncodingError::kPayloadTooLarge; }

    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
    for (const auto& key : entry.storage_keys)
    {
        if (!encoder.add(rlp::ByteView(key.data(), key.size()))) { return rlp::EncodingError::kPayloadTooLarge; }
    }
    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    return rlp::outcome::success();
}

rlp::Result<AccessListEntry> decode_access_list_entry_from_decoder(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    const size_t payload = list_size.value();
    const size_t start   = decoder.Remaining().size();
    const size_t target  = start - payload;

    AccessListEntry entry;
    if (!decoder.read(entry.address)) { return rlp::DecodingError::kUnexpectedLength; }

    auto keys_size = decoder.ReadListHeaderBytes();
    if (!keys_size) { return keys_size.error(); }

    const size_t kp  = keys_size.value();
    const size_t ks  = decoder.Remaining().size();
    const size_t kt  = ks - kp;

    while (decoder.Remaining().size() > kt)
    {
        Hash256 key{};
        if (!decoder.read(key)) { return rlp::DecodingError::kUnexpectedLength; }
        entry.storage_keys.push_back(key);
    }
    if (decoder.Remaining().size() != kt) { return rlp::DecodingError::kListLengthMismatch; }
    if (decoder.Remaining().size() != target) { return rlp::DecodingError::kListLengthMismatch; }

    return entry;
}

// Encode an access list (the outer list of [address, [keys]] pairs)
rlp::EncodingOperationResult encode_access_list(rlp::RlpEncoder& encoder, const std::vector<AccessListEntry>& access_list)
{
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
    for (const auto& entry : access_list)
    {
        auto res = encode_access_list_entry_to_encoder(encoder, entry);
        if (!res) { return res; }
    }
    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    return rlp::outcome::success();
}

rlp::Result<std::vector<AccessListEntry>> decode_access_list(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    const size_t payload = list_size.value();
    const size_t start   = decoder.Remaining().size();
    const size_t target  = start - payload;

    std::vector<AccessListEntry> entries;
    while (decoder.Remaining().size() > target)
    {
        auto entry = decode_access_list_entry_from_decoder(decoder);
        if (!entry) { return entry.error(); }
        entries.push_back(std::move(entry.value()));
    }
    if (decoder.Remaining().size() != target) { return rlp::DecodingError::kListLengthMismatch; }
    return entries;
}

} // namespace

EncodeResult encode_log_entry(const LogEntry& entry) noexcept {
    rlp::RlpEncoder encoder;

    if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
    if (!encoder.add(rlp::ByteView(entry.address.data(), entry.address.size()))) return rlp::EncodingError::kPayloadTooLarge;

    if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
    for (const auto& topic : entry.topics) {
        if (!encoder.add(rlp::ByteView(topic.data(), topic.size()))) return rlp::EncodingError::kPayloadTooLarge;
    }
    if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

    if (!encoder.add(rlp::ByteView(entry.data.data(), entry.data.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

    return finalize_encoding(encoder);
}

DecodeResult<LogEntry> decode_log_entry(rlp::ByteView rlp_data) noexcept {
    rlp::RlpDecoder decoder(rlp_data);
    return decode_log_entry_from_decoder(decoder);
}

EncodeResult encode_access_list_entry(const AccessListEntry& entry) noexcept
{
    rlp::RlpEncoder encoder;
    auto res = encode_access_list_entry_to_encoder(encoder, entry);
    if (!res) { return res.error(); }
    return finalize_encoding(encoder);
}

DecodeResult<AccessListEntry> decode_access_list_entry(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);
    return decode_access_list_entry_from_decoder(decoder);
}

EncodeResult encode_transaction(const Transaction& tx) noexcept
{
    rlp::RlpEncoder encoder;

    if (tx.type == TransactionType::kLegacy)
    {
        // Legacy: RLP([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
        if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        if (!encoder.add(tx.nonce)) { return rlp::EncodingError::kPayloadTooLarge; }
        const intx::uint256 gp = tx.gas_price.value_or(intx::uint256(0));
        if (!encoder.add(gp)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.gas_limit)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (tx.to.has_value())
        {
            if (!encoder.add(rlp::ByteView(tx.to->data(), tx.to->size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        else
        {
            if (!encoder.add(rlp::ByteView{})) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        if (!encoder.add(tx.value)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(rlp::ByteView(tx.data.data(), tx.data.size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.v)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.r)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.s)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

        return finalize_encoding(encoder);
    }

    // EIP-2718 typed transactions: type_byte || RLP(payload)
    if (tx.type == TransactionType::kAccessList)
    {
        // EIP-2930: 0x01 || RLP([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList, v, r, s])
        if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        if (!encoder.add(tx.chain_id.value_or(kDefaultChainId))) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.nonce)) { return rlp::EncodingError::kPayloadTooLarge; }
        const intx::uint256 gp = tx.gas_price.value_or(intx::uint256(0));
        if (!encoder.add(gp)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.gas_limit)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (tx.to.has_value())
        {
            if (!encoder.add(rlp::ByteView(tx.to->data(), tx.to->size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        else
        {
            if (!encoder.add(rlp::ByteView{})) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        if (!encoder.add(tx.value)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(rlp::ByteView(tx.data.data(), tx.data.size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        auto al_res = encode_access_list(encoder, tx.access_list);
        if (!al_res) { return al_res.error(); }
        if (!encoder.add(tx.v)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.r)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.s)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

        auto rlp_bytes = finalize_encoding(encoder);
        if (!rlp_bytes) { return rlp_bytes; }

        ByteBuffer typed;
        typed.reserve(kTypedTxPrefixSize + rlp_bytes.value().size());
        typed.push_back(static_cast<uint8_t>(TransactionType::kAccessList));
        typed.insert(typed.end(), rlp_bytes.value().begin(), rlp_bytes.value().end());
        return typed;
    }

    if (tx.type == TransactionType::kDynamicFee)
    {
        // EIP-1559: 0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, v, r, s])
        if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        if (!encoder.add(tx.chain_id.value_or(kDefaultChainId))) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.nonce)) { return rlp::EncodingError::kPayloadTooLarge; }
        const intx::uint256 mpf = tx.max_priority_fee_per_gas.value_or(intx::uint256(0));
        if (!encoder.add(mpf)) { return rlp::EncodingError::kPayloadTooLarge; }
        const intx::uint256 mf = tx.max_fee_per_gas.value_or(intx::uint256(0));
        if (!encoder.add(mf)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.gas_limit)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (tx.to.has_value())
        {
            if (!encoder.add(rlp::ByteView(tx.to->data(), tx.to->size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        else
        {
            if (!encoder.add(rlp::ByteView{})) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        if (!encoder.add(tx.value)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(rlp::ByteView(tx.data.data(), tx.data.size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        auto al_res = encode_access_list(encoder, tx.access_list);
        if (!al_res) { return al_res.error(); }
        if (!encoder.add(tx.v)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.r)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.add(tx.s)) { return rlp::EncodingError::kPayloadTooLarge; }
        if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

        auto rlp_bytes = finalize_encoding(encoder);
        if (!rlp_bytes) { return rlp_bytes; }

        ByteBuffer typed;
        typed.reserve(kTypedTxPrefixSize + rlp_bytes.value().size());
        typed.push_back(static_cast<uint8_t>(TransactionType::kDynamicFee));
        typed.insert(typed.end(), rlp_bytes.value().begin(), rlp_bytes.value().end());
        return typed;
    }

    return rlp::EncodingError::kEmptyInput;
}

DecodeResult<Transaction> decode_transaction(rlp::ByteView raw_data) noexcept
{
    if (raw_data.empty()) { return rlp::DecodingError::kInputTooShort; }

    Transaction tx;

    // Detect EIP-2718 typed transaction: first byte < kRlpListPrefixMin means it is a type prefix
    if (raw_data[0] < kRlpListPrefixMin)
    {
        const uint8_t type_byte = raw_data[0];
        if (type_byte == static_cast<uint8_t>(TransactionType::kAccessList))
        {
            tx.type = TransactionType::kAccessList;
        }
        else if (type_byte == static_cast<uint8_t>(TransactionType::kDynamicFee))
        {
            tx.type = TransactionType::kDynamicFee;
        }
        else if (type_byte == static_cast<uint8_t>(TransactionType::kOpDeposit))
        {
            tx.type = TransactionType::kOpDeposit;
            return tx;
        }
        else
        {
            return rlp::DecodingError::kUnexpectedString;
        }

        const rlp::ByteView rlp_payload = raw_data.substr(kTypedTxPrefixSize);
        rlp::RlpDecoder decoder(rlp_payload);

        auto list_size = decoder.ReadListHeaderBytes();
        if (!list_size) { return list_size.error(); }

        uint64_t chain_id = 0;
        if (!decoder.read(chain_id)) { return rlp::DecodingError::kUnexpectedString; }
        tx.chain_id = chain_id;

        if (!decoder.read(tx.nonce)) { return rlp::DecodingError::kUnexpectedString; }

        if (tx.type == TransactionType::kAccessList)
        {
            intx::uint256 gp{};
            if (!decoder.read(gp)) { return rlp::DecodingError::kUnexpectedString; }
            tx.gas_price = gp;
        }
        else
        {
            intx::uint256 mpf{};
            if (!decoder.read(mpf)) { return rlp::DecodingError::kUnexpectedString; }
            tx.max_priority_fee_per_gas = mpf;
            intx::uint256 mf{};
            if (!decoder.read(mf)) { return rlp::DecodingError::kUnexpectedString; }
            tx.max_fee_per_gas = mf;
        }

        if (!decoder.read(tx.gas_limit)) { return rlp::DecodingError::kUnexpectedString; }

        // to: empty bytes = contract creation
        {
            auto to_header = decoder.PeekHeader();
            if (!to_header) { return to_header.error(); }
            if (to_header.value().payload_size_bytes == 0)
            {
                rlp::Bytes empty{};
                if (!decoder.read(empty)) { return rlp::DecodingError::kUnexpectedString; }
                tx.to.reset();
            }
            else
            {
                Address addr{};
                if (!decoder.read(addr)) { return rlp::DecodingError::kUnexpectedLength; }
                tx.to = addr;
            }
        }

        if (!decoder.read(tx.value)) { return rlp::DecodingError::kUnexpectedString; }

        rlp::Bytes data_bytes;
        if (!decoder.read(data_bytes)) { return rlp::DecodingError::kUnexpectedString; }
        tx.data.assign(data_bytes.begin(), data_bytes.end());

        auto al_result = decode_access_list(decoder);
        if (!al_result) { return al_result.error(); }
        tx.access_list = std::move(al_result.value());

        if (!decoder.read(tx.v)) { return rlp::DecodingError::kUnexpectedString; }
        if (!decoder.read(tx.r)) { return rlp::DecodingError::kUnexpectedString; }
        if (!decoder.read(tx.s)) { return rlp::DecodingError::kUnexpectedString; }

        return tx;
    }

    // Legacy transaction: RLP list
    tx.type = TransactionType::kLegacy;
    rlp::RlpDecoder decoder(raw_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    if (!decoder.read(tx.nonce)) { return rlp::DecodingError::kUnexpectedString; }

    intx::uint256 gp{};
    if (!decoder.read(gp)) { return rlp::DecodingError::kUnexpectedString; }
    tx.gas_price = gp;

    if (!decoder.read(tx.gas_limit)) { return rlp::DecodingError::kUnexpectedString; }

    {
        auto to_header = decoder.PeekHeader();
        if (!to_header) { return to_header.error(); }
        if (to_header.value().payload_size_bytes == 0)
        {
            rlp::Bytes empty{};
            if (!decoder.read(empty)) { return rlp::DecodingError::kUnexpectedString; }
            tx.to.reset();
        }
        else
        {
            Address addr{};
            if (!decoder.read(addr)) { return rlp::DecodingError::kUnexpectedLength; }
            tx.to = addr;
        }
    }

    if (!decoder.read(tx.value)) { return rlp::DecodingError::kUnexpectedString; }

    rlp::Bytes data_bytes;
    if (!decoder.read(data_bytes)) { return rlp::DecodingError::kUnexpectedString; }
    tx.data.assign(data_bytes.begin(), data_bytes.end());

    if (!decoder.read(tx.v)) { return rlp::DecodingError::kUnexpectedString; }
    if (!decoder.read(tx.r)) { return rlp::DecodingError::kUnexpectedString; }
    if (!decoder.read(tx.s)) { return rlp::DecodingError::kUnexpectedString; }

    return tx;
}

EncodeResult encode_receipt(const Receipt& receipt) noexcept {
    rlp::RlpEncoder encoder;

    if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;

    if (receipt.state_root.has_value()) {
        if (!encoder.add(rlp::ByteView(receipt.state_root->data(), receipt.state_root->size()))) return rlp::EncodingError::kPayloadTooLarge;
    } else if (receipt.status.has_value()) {
        if (!encoder.add(static_cast<uint8_t>(receipt.status.value() ? 1 : 0))) return rlp::EncodingError::kPayloadTooLarge;
    } else {
        return rlp::EncodingError::kEmptyInput;
    }

    if (!encoder.add(receipt.cumulative_gas_used)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(receipt.bloom.data(), receipt.bloom.size()))) return rlp::EncodingError::kPayloadTooLarge;

    if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
    for (const auto& log : receipt.logs) {
        if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
        if (!encoder.add(rlp::ByteView(log.address.data(), log.address.size()))) return rlp::EncodingError::kPayloadTooLarge;

        if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
        for (const auto& topic : log.topics) {
            if (!encoder.add(rlp::ByteView(topic.data(), topic.size()))) return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

        if (!encoder.add(rlp::ByteView(log.data.data(), log.data.size()))) return rlp::EncodingError::kPayloadTooLarge;
        if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;
    }
    if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

    if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

    return finalize_encoding(encoder);
}

DecodeResult<Receipt> decode_receipt(rlp::ByteView rlp_data) noexcept {
    if (!rlp_data.empty() && rlp_data[0] < kRlpListPrefixMin)
    {
        rlp_data = rlp_data.substr(kTypedTxPrefixSize);
    }

    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) {
        return list_size.error();
    }

    Receipt receipt;
    auto status_result = decode_status_or_state_root(decoder, receipt);
    if (!status_result) {
        return status_result.error();
    }

    if (!decoder.read(receipt.cumulative_gas_used)) {
        return rlp::DecodingError::kUnexpectedString;
    }

    if (!decoder.read(receipt.bloom)) {
        return rlp::DecodingError::kUnexpectedLength;
    }

    auto logs_list_size = decoder.ReadListHeaderBytes();
    if (!logs_list_size) {
        return logs_list_size.error();
    }

    const size_t logs_payload = logs_list_size.value();
    const size_t start_remaining = decoder.Remaining().size();
    const size_t target_remaining = start_remaining - logs_payload;

    while (decoder.Remaining().size() > target_remaining) {
        auto log_result = decode_log_entry_from_decoder(decoder);
        if (!log_result) {
            return log_result.error();
        }
        receipt.logs.push_back(std::move(log_result.value()));
    }

    if (decoder.Remaining().size() != target_remaining) {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return receipt;
}

EncodeResult encode_block_header(const BlockHeader& header) noexcept {
    rlp::RlpEncoder encoder;

    if (!encoder.BeginList()) return rlp::EncodingError::kUnclosedList;
    if (!encoder.add(rlp::ByteView(header.parent_hash.data(), header.parent_hash.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.ommers_hash.data(), header.ommers_hash.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.beneficiary.data(), header.beneficiary.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.state_root.data(), header.state_root.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.transactions_root.data(), header.transactions_root.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.receipts_root.data(), header.receipts_root.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.logs_bloom.data(), header.logs_bloom.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(header.difficulty)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(header.number)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(header.gas_limit)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(header.gas_used)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(header.timestamp)) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.extra_data.data(), header.extra_data.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.mix_hash.data(), header.mix_hash.size()))) return rlp::EncodingError::kPayloadTooLarge;
    if (!encoder.add(rlp::ByteView(header.nonce.data(), header.nonce.size()))) return rlp::EncodingError::kPayloadTooLarge;

    if (header.base_fee_per_gas.has_value()) {
        if (!encoder.add(header.base_fee_per_gas.value())) return rlp::EncodingError::kPayloadTooLarge;
    }

    if (!encoder.EndList()) return rlp::EncodingError::kUnclosedList;

    return finalize_encoding(encoder);
}

DecodeResult<BlockHeader> decode_block_header(rlp::ByteView rlp_data) noexcept {
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) {
        return list_size.error();
    }

    BlockHeader header;

    if (!decoder.read(header.parent_hash)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.ommers_hash)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.beneficiary)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.state_root)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.transactions_root)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.receipts_root)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.logs_bloom)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.difficulty)) return rlp::DecodingError::kUnexpectedString;
    if (!decoder.read(header.number)) return rlp::DecodingError::kUnexpectedString;
    if (!decoder.read(header.gas_limit)) return rlp::DecodingError::kUnexpectedString;
    if (!decoder.read(header.gas_used)) return rlp::DecodingError::kUnexpectedString;
    if (!decoder.read(header.timestamp)) return rlp::DecodingError::kUnexpectedString;

    rlp::Bytes extra_bytes;
    if (!decoder.read(extra_bytes)) return rlp::DecodingError::kUnexpectedString;
    header.extra_data.assign(extra_bytes.begin(), extra_bytes.end());

    if (!decoder.read(header.mix_hash)) return rlp::DecodingError::kUnexpectedLength;
    if (!decoder.read(header.nonce)) return rlp::DecodingError::kUnexpectedLength;

    if (!decoder.IsFinished()) {
        intx::uint256 base_fee{};
        if (!decoder.read(base_fee)) return rlp::DecodingError::kUnexpectedString;
        header.base_fee_per_gas = base_fee;
    }

    while (!decoder.IsFinished()) {
        auto skipped = decoder.SkipItem();
        if (!skipped) return skipped.error();
    }

    return header;
}

} // namespace eth::codec
