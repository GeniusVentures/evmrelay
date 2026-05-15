// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/messages.hpp>
#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_encoder.hpp>

namespace eth::protocol {

namespace {

ByteBuffer to_byte_buffer(const rlp::Bytes& bytes)
{
    return {bytes.begin(), bytes.end()};
}

EncodeResult finalize_encoding(rlp::RlpEncoder& encoder)
{
    auto result = encoder.GetBytes();
    if (!result)
    {
        return result.error();
    }

    return to_byte_buffer(*result.value());
}

rlp::EncodingOperationResult encode_get_block_headers_payload(rlp::RlpEncoder& encoder, const GetBlockHeadersMessage& msg)
{
    // go-ethereum: HashOrNumber must have exactly one set — both set is invalid.
    if (msg.start_hash.has_value() && msg.start_number.has_value())
    {
        return rlp::EncodingError::kEmptyInput;  // reuse available error; means "invalid input"
    }

    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    if (msg.start_hash.has_value())
    {
        if (!encoder.add(rlp::ByteView(msg.start_hash->data(), msg.start_hash->size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }
    else if (msg.start_number.has_value())
    {
        if (!encoder.add(*msg.start_number))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }
    // else: neither set → empty inner list (go-ethereum nil packet)

    if (!encoder.add(msg.max_headers))
    {
        return rlp::EncodingError::kPayloadTooLarge;
    }
    if (!encoder.add(msg.skip))
    {
        return rlp::EncodingError::kPayloadTooLarge;
    }
    if (!encoder.add(msg.reverse))
    {
        return rlp::EncodingError::kPayloadTooLarge;
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return rlp::outcome::success();
}

rlp::DecodingResult decode_get_block_headers_payload(rlp::RlpDecoder& decoder, GetBlockHeadersMessage& msg)
{
    auto header = decoder.PeekHeader();
    if (!header)
    {
        return header.error();
    }

    if (header.value().list)
    {
        return rlp::DecodingError::kUnexpectedList;
    }

    if (header.value().payload_size_bytes == Hash256{}.size())
    {
        Hash256 hash{};
        if (!decoder.read(hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }

        msg.start_hash = hash;
        msg.start_number.reset();
    }
    else
    {
        uint64_t number = 0;
        if (!decoder.read(number))
        {
            return rlp::DecodingError::kUnexpectedString;
        }

        msg.start_number = number;
        msg.start_hash.reset();
    }

    if (!decoder.read(msg.max_headers))
    {
        return rlp::DecodingError::kUnexpectedString;
    }
    if (!decoder.read(msg.skip))
    {
        return rlp::DecodingError::kUnexpectedString;
    }
    if (!decoder.read(msg.reverse))
    {
        return rlp::DecodingError::kUnexpectedString;
    }

    return rlp::outcome::success();
}

rlp::EncodingOperationResult encode_hash_list(rlp::RlpEncoder& encoder, const std::vector<Hash256>& hashes)
{
    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& hash : hashes)
    {
        if (!encoder.add(rlp::ByteView(hash.data(), hash.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return rlp::outcome::success();
}

rlp::Result<std::vector<Hash256>> decode_hash_list(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    const size_t payload_size = list_size.value();
    const size_t start_remaining = decoder.Remaining().size();
    const size_t target_remaining = start_remaining - payload_size;

    std::vector<Hash256> hashes;

    while (decoder.Remaining().size() > target_remaining)
    {
        Hash256 hash{};
        if (!decoder.read(hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        hashes.push_back(hash);
    }

    if (decoder.Remaining().size() != target_remaining)
    {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return hashes;
}

rlp::Result<rlp::ByteView> consume_next_item(rlp::RlpDecoder& decoder)
{
    const rlp::ByteView before = decoder.Remaining();
    if (before.empty())
    {
        return rlp::DecodingError::kInputTooShort;
    }

    auto skip_result = decoder.SkipItem();
    if (!skip_result)
    {
        return skip_result.error();
    }

    const rlp::ByteView after = decoder.Remaining();
    return before.substr(0, before.size() - after.size());
}

bool consume_eth66_request_id(rlp::RlpDecoder& decoder, std::optional<uint64_t>& request_id)
{
    rlp::RlpDecoder probe(decoder.Remaining());
    uint64_t candidate_request_id = 0;
    if (!probe.read(candidate_request_id))
    {
        return false;
    }

    auto next_header = probe.PeekHeader();
    if (!next_header || !next_header.value().list)
    {
        return false;
    }

    if (!decoder.read(candidate_request_id))
    {
        return false;
    }

    request_id = candidate_request_id;
    return true;
}

rlp::EncodingOperationResult encode_block_headers_payload(rlp::RlpEncoder& encoder, const std::vector<codec::BlockHeader>& headers)
{
    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& header : headers)
    {
        auto encoded_header = codec::encode_block_header(header);
        if (!encoded_header)
        {
            return encoded_header.error();
        }

        if (!encoder.AddRaw(rlp::ByteView(encoded_header.value().data(), encoded_header.value().size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return rlp::outcome::success();
}

rlp::Result<std::vector<codec::BlockHeader>> decode_block_headers_payload(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    const size_t payload_size = list_size.value();
    const size_t start_remaining = decoder.Remaining().size();
    const size_t target_remaining = start_remaining - payload_size;

    std::vector<codec::BlockHeader> headers;

    while (decoder.Remaining().size() > target_remaining)
    {
        auto item_view = consume_next_item(decoder);
        if (!item_view)
        {
            return item_view.error();
        }

        auto header = codec::decode_block_header(item_view.value());
        if (!header)
        {
            return header.error();
        }

        headers.push_back(std::move(header.value()));
    }

    if (decoder.Remaining().size() != target_remaining)
    {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return headers;
}

rlp::EncodingOperationResult encode_receipts_payload(rlp::RlpEncoder& encoder, const std::vector<std::vector<codec::Receipt>>& receipts)
{
    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& block_receipts : receipts)
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }

        for (const auto& receipt : block_receipts)
        {
            auto encoded_receipt = codec::encode_receipt(receipt);
            if (!encoded_receipt)
            {
                return encoded_receipt.error();
            }

            if (!encoder.AddRaw(rlp::ByteView(encoded_receipt.value().data(), encoded_receipt.value().size())))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
        }

        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return rlp::outcome::success();
}

rlp::Result<std::vector<std::vector<codec::Receipt>>> decode_receipts_payload(rlp::RlpDecoder& decoder)
{
    auto outer_list_size = decoder.ReadListHeaderBytes();
    if (!outer_list_size)
    {
        return outer_list_size.error();
    }

    const size_t outer_payload_size = outer_list_size.value();
    const size_t outer_start_remaining = decoder.Remaining().size();
    const size_t outer_target_remaining = outer_start_remaining - outer_payload_size;

    std::vector<std::vector<codec::Receipt>> receipts;

    while (decoder.Remaining().size() > outer_target_remaining)
    {
        auto block_list_size = decoder.ReadListHeaderBytes();
        if (!block_list_size)
        {
            return block_list_size.error();
        }

        const size_t block_payload_size = block_list_size.value();
        const size_t block_start_remaining = decoder.Remaining().size();
        const size_t block_target_remaining = block_start_remaining - block_payload_size;

        std::vector<codec::Receipt> block_receipts;

        while (decoder.Remaining().size() > block_target_remaining)
        {
            auto item_view = consume_next_item(decoder);
            if (!item_view)
            {
                return item_view.error();
            }

            auto receipt = codec::decode_receipt(item_view.value());
            if (!receipt)
            {
                return receipt.error();
            }

            block_receipts.push_back(std::move(receipt.value()));
        }

        if (decoder.Remaining().size() != block_target_remaining)
        {
            return rlp::DecodingError::kListLengthMismatch;
        }

        receipts.push_back(std::move(block_receipts));
    }

    if (decoder.Remaining().size() != outer_target_remaining)
    {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return receipts;
}

rlp::EncodingOperationResult encode_pooled_transactions_payload(rlp::RlpEncoder& encoder, const std::vector<std::vector<uint8_t>>& encoded_transactions)
{
    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& encoded_tx : encoded_transactions)
    {
        if (!encoder.AddRaw(rlp::ByteView(encoded_tx.data(), encoded_tx.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return rlp::outcome::success();
}

rlp::Result<std::vector<std::vector<uint8_t>>> decode_pooled_transactions_payload(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    const size_t payload_size = list_size.value();
    const size_t start_remaining = decoder.Remaining().size();
    const size_t target_remaining = start_remaining - payload_size;

    std::vector<std::vector<uint8_t>> encoded_transactions;

    while (decoder.Remaining().size() > target_remaining)
    {
        auto item_view = consume_next_item(decoder);
        if (!item_view)
        {
            return item_view.error();
        }

        encoded_transactions.emplace_back(item_view.value().begin(), item_view.value().end());
    }

    if (decoder.Remaining().size() != target_remaining)
    {
        return rlp::DecodingError::kListLengthMismatch;
    }

    return encoded_transactions;
}

// ---------------------------------------------------------------------------
// Block body helpers
// ---------------------------------------------------------------------------

rlp::EncodingOperationResult encode_transaction_list(rlp::RlpEncoder& encoder, const std::vector<codec::Transaction>& txs)
{
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
    for (const auto& tx : txs)
    {
        auto encoded = codec::encode_transaction(tx);
        if (!encoded) { return encoded.error(); }

        if (tx.type == codec::TransactionType::kLegacy)
        {
            // Legacy tx is bare RLP - embed directly
            if (!encoder.AddRaw(rlp::ByteView(encoded.value().data(), encoded.value().size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        }
        else
        {
            // EIP-2718 typed tx: encode as RLP byte string so it is skippable by RLP decoders
            if (!encoder.add(rlp::ByteView(encoded.value().data(), encoded.value().size()))) { return rlp::EncodingError::kPayloadTooLarge; }
        }
    }
    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    return rlp::outcome::success();
}

rlp::Result<std::vector<codec::Transaction>> decode_transaction_list(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    const size_t payload  = list_size.value();
    const size_t start    = decoder.Remaining().size();
    const size_t target   = start - payload;

    std::vector<codec::Transaction> txs;
    while (decoder.Remaining().size() > target)
    {
        // Peek at next item to decide how to decode it
        auto next_header = decoder.PeekHeader();
        if (!next_header) { return next_header.error(); }

        if (!next_header.value().list && next_header.value().payload_size_bytes > 0)
        {
            // Could be an EIP-2718 typed tx encoded as a byte string.
            // Read the raw bytes, then inspect first byte.
            rlp::Bytes raw_bytes;
            if (!decoder.read(raw_bytes)) { return rlp::DecodingError::kUnexpectedString; }

            if (!raw_bytes.empty() && raw_bytes[0] < 0x80)
            {
                // Typed transaction - raw_bytes = type || RLP(payload)
                auto tx = codec::decode_transaction(rlp::ByteView(raw_bytes.data(), raw_bytes.size()));
                if (!tx) { return tx.error(); }
                txs.push_back(std::move(tx.value()));
            }
            else
            {
                // Should not happen in well-formed data, but try decoding anyway
                auto tx = codec::decode_transaction(rlp::ByteView(raw_bytes.data(), raw_bytes.size()));
                if (!tx) { return tx.error(); }
                txs.push_back(std::move(tx.value()));
            }
        }
        else
        {
            // Legacy transaction - it's an RLP list, consume it as a raw item
            auto item_view = consume_next_item(decoder);
            if (!item_view) { return item_view.error(); }

            auto tx = codec::decode_transaction(item_view.value());
            if (!tx) { return tx.error(); }
            txs.push_back(std::move(tx.value()));
        }
    }
    if (decoder.Remaining().size() != target) { return rlp::DecodingError::kListLengthMismatch; }
    return txs;
}

rlp::EncodingOperationResult encode_block_body(rlp::RlpEncoder& encoder, const BlockBody& body)
{
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }

    auto tx_res = encode_transaction_list(encoder, body.transactions);
    if (!tx_res) { return tx_res; }

    // ommers list
    auto ommers_res = encode_block_headers_payload(encoder, body.ommers);
    if (!ommers_res) { return ommers_res; }

    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    return rlp::outcome::success();
}

rlp::Result<BlockBody> decode_block_body(rlp::RlpDecoder& decoder)
{
    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    const size_t payload = list_size.value();
    const size_t start   = decoder.Remaining().size();
    const size_t target  = start - payload;

    BlockBody body;

    auto txs = decode_transaction_list(decoder);
    if (!txs) { return txs.error(); }
    body.transactions = std::move(txs.value());

    auto ommers = decode_block_headers_payload(decoder);
    if (!ommers) { return ommers.error(); }
    body.ommers = std::move(ommers.value());

    if (decoder.Remaining().size() != target) { return rlp::DecodingError::kListLengthMismatch; }
    return body;
}

} // namespace

} // namespace eth::protocol

namespace eth {

CommonStatusFields get_common_fields(const StatusMessage& msg) noexcept
{
    return std::visit([](const auto& m) -> CommonStatusFields
    {
        return CommonStatusFields{m.protocol_version, m.network_id, m.genesis_hash, m.fork_id};
    }, msg);
}

} // namespace eth

namespace eth::protocol {

EncodeResult encode_status(const StatusMessage& msg) noexcept
{
    return std::visit([](const auto& m) -> EncodeResult
    {
        rlp::RlpEncoder encoder;

        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(m.protocol_version))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.add(m.network_id))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }

        using MsgType = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<MsgType, eth::StatusMessage68>)
        {
            if (!encoder.add(m.td))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
            if (!encoder.add(rlp::ByteView(m.blockhash.data(), m.blockhash.size())))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
        }

        if (!encoder.add(rlp::ByteView(m.genesis_hash.data(), m.genesis_hash.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }

        // ForkID as a nested list [hash, next]
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(rlp::ByteView(m.fork_id.fork_hash.data(), m.fork_id.fork_hash.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.add(m.fork_id.next_fork))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }

        if constexpr (std::is_same_v<MsgType, eth::StatusMessage69>)
        {
            if (!encoder.add(m.earliest_block))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
            if (!encoder.add(m.latest_block))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
            if (!encoder.add(rlp::ByteView(m.latest_block_hash.data(), m.latest_block_hash.size())))
            {
                return rlp::EncodingError::kPayloadTooLarge;
            }
        }

        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }

        return finalize_encoding(encoder);
    }, msg);
}

DecodeResult<StatusMessage> decode_status(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    uint8_t  protocol_version = 0;
    uint64_t network_id = 0;

    if (!decoder.read(protocol_version))
    {
        return rlp::DecodingError::kUnexpectedString;
    }
    if (!decoder.read(network_id))
    {
        return rlp::DecodingError::kUnexpectedString;
    }

    if (protocol_version == eth::kEthProtocolVersion69)
    {
        eth::StatusMessage69 msg69;
        msg69.protocol_version = protocol_version;
        msg69.network_id = network_id;

        if (!decoder.read(msg69.genesis_hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }

        auto fork_list = decoder.ReadListHeaderBytes();
        if (!fork_list)
        {
            return fork_list.error();
        }
        if (!decoder.read(msg69.fork_id.fork_hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        if (!decoder.read(msg69.fork_id.next_fork))
        {
            return rlp::DecodingError::kUnexpectedString;
        }
        if (!decoder.read(msg69.earliest_block))
        {
            return rlp::DecodingError::kUnexpectedString;
        }
        if (!decoder.read(msg69.latest_block))
        {
            return rlp::DecodingError::kUnexpectedString;
        }
        if (!decoder.read(msg69.latest_block_hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }

        return StatusMessage{msg69};
    }
    else if (protocol_version == eth::kEthProtocolVersion68 ||
             protocol_version == eth::kEthProtocolVersion67 ||
             protocol_version == eth::kEthProtocolVersion66)
    {
        eth::StatusMessage68 msg68;
        msg68.protocol_version = protocol_version;
        msg68.network_id = network_id;

        if (!decoder.read(msg68.td))
        {
            return rlp::DecodingError::kUnexpectedString;
        }
        if (!decoder.read(msg68.blockhash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        if (!decoder.read(msg68.genesis_hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }

        auto fork_list = decoder.ReadListHeaderBytes();
        if (!fork_list)
        {
            return fork_list.error();
        }
        if (!decoder.read(msg68.fork_id.fork_hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        if (!decoder.read(msg68.fork_id.next_fork))
        {
            return rlp::DecodingError::kUnexpectedString;
        }

        return StatusMessage{msg68};
    }
    else
    {
        return rlp::DecodingError::kUnexpectedString;
    }
}

ValidationResult validate_status(
    const eth::StatusMessage& msg,
    uint64_t                  expected_network_id,
    const eth::Hash256&       expected_genesis) noexcept
{
    const auto common = eth::get_common_fields(msg);
    if (common.network_id != expected_network_id)
    {
        return eth::StatusValidationError::kNetworkIDMismatch;
    }
    if (common.genesis_hash != expected_genesis)
    {
        return eth::StatusValidationError::kGenesisMismatch;
    }
    if (const auto* msg69 = std::get_if<eth::StatusMessage69>(&msg))
    {
        if (msg69->latest_block != 0 && msg69->earliest_block > msg69->latest_block)
        {
            return eth::StatusValidationError::kInvalidBlockRange;
        }
    }
    return rlp::outcome::success();
}

EncodeResult encode_new_block_hashes(const NewBlockHashesMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& entry : msg.entries)
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(rlp::ByteView(entry.hash.data(), entry.hash.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.add(entry.number))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return finalize_encoding(encoder);
}

DecodeResult<NewBlockHashesMessage> decode_new_block_hashes(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    NewBlockHashesMessage msg;

    while (!decoder.IsFinished())
    {
        auto entry_list = decoder.ReadListHeaderBytes();
        if (!entry_list)
        {
            return entry_list.error();
        }

        NewBlockHashEntry entry;
        if (!decoder.read(entry.hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        if (!decoder.read(entry.number))
        {
            return rlp::DecodingError::kUnexpectedString;
        }

        msg.entries.push_back(entry);
    }

    return msg;
}

EncodeResult encode_new_pooled_tx_hashes(const NewPooledTransactionHashesMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (!encoder.BeginList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    for (const auto& hash : msg.hashes)
    {
        if (!encoder.add(rlp::ByteView(hash.data(), hash.size())))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
    }

    if (!encoder.EndList())
    {
        return rlp::EncodingError::kUnclosedList;
    }

    return finalize_encoding(encoder);
}

DecodeResult<NewPooledTransactionHashesMessage> decode_new_pooled_tx_hashes(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    NewPooledTransactionHashesMessage msg;

    while (!decoder.IsFinished())
    {
        Hash256 hash{};
        if (!decoder.read(hash))
        {
            return rlp::DecodingError::kUnexpectedLength;
        }
        msg.hashes.push_back(hash);
    }

    return msg;
}

EncodeResult encode_get_block_headers(const GetBlockHeadersMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_get_block_headers_payload(encoder, msg);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_get_block_headers_payload(encoder, msg);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<GetBlockHeadersMessage> decode_get_block_headers(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    GetBlockHeadersMessage msg;

    if (consume_eth66_request_id(decoder, msg.request_id))
    {
        auto payload_list = decoder.ReadListHeaderBytes();
        if (!payload_list)
        {
            return payload_list.error();
        }
    }

    auto payload_result = decode_get_block_headers_payload(decoder, msg);
    if (!payload_result)
    {
        return payload_result.error();
    }

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

EncodeResult encode_block_headers(const BlockHeadersMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_block_headers_payload(encoder, msg.headers);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_block_headers_payload(encoder, msg.headers);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<BlockHeadersMessage> decode_block_headers(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    BlockHeadersMessage msg;

    consume_eth66_request_id(decoder, msg.request_id);

    auto headers_result = decode_block_headers_payload(decoder);
    if (!headers_result)
    {
        return headers_result.error();
    }

    msg.headers = std::move(headers_result.value());

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

EncodeResult encode_get_receipts(const GetReceiptsMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_hash_list(encoder, msg.block_hashes);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_hash_list(encoder, msg.block_hashes);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<GetReceiptsMessage> decode_get_receipts(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    GetReceiptsMessage msg;

    consume_eth66_request_id(decoder, msg.request_id);

    auto hashes_result = decode_hash_list(decoder);
    if (!hashes_result)
    {
        return hashes_result.error();
    }

    msg.block_hashes = std::move(hashes_result.value());

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

EncodeResult encode_receipts(const ReceiptsMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_receipts_payload(encoder, msg.receipts);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_receipts_payload(encoder, msg.receipts);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<ReceiptsMessage> decode_receipts(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    ReceiptsMessage msg;

    consume_eth66_request_id(decoder, msg.request_id);

    auto receipts_result = decode_receipts_payload(decoder);
    if (!receipts_result)
    {
        return receipts_result.error();
    }

    msg.receipts = std::move(receipts_result.value());

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

EncodeResult encode_get_pooled_transactions(const GetPooledTransactionsMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_hash_list(encoder, msg.transaction_hashes);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_hash_list(encoder, msg.transaction_hashes);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<GetPooledTransactionsMessage> decode_get_pooled_transactions(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    GetPooledTransactionsMessage msg;

    consume_eth66_request_id(decoder, msg.request_id);

    auto hashes_result = decode_hash_list(decoder);
    if (!hashes_result)
    {
        return hashes_result.error();
    }

    msg.transaction_hashes = std::move(hashes_result.value());

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

EncodeResult encode_pooled_transactions(const PooledTransactionsMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
        if (!encoder.add(msg.request_id.value()))
        {
            return rlp::EncodingError::kPayloadTooLarge;
        }
        auto payload_result = encode_pooled_transactions_payload(encoder, msg.encoded_transactions);
        if (!payload_result)
        {
            return payload_result.error();
        }
        if (!encoder.EndList())
        {
            return rlp::EncodingError::kUnclosedList;
        }
    }
    else
    {
        auto payload_result = encode_pooled_transactions_payload(encoder, msg.encoded_transactions);
        if (!payload_result)
        {
            return payload_result.error();
        }
    }

    return finalize_encoding(encoder);
}

DecodeResult<PooledTransactionsMessage> decode_pooled_transactions(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size)
    {
        return list_size.error();
    }

    PooledTransactionsMessage msg;

    consume_eth66_request_id(decoder, msg.request_id);

    auto txs_result = decode_pooled_transactions_payload(decoder);
    if (!txs_result)
    {
        return txs_result.error();
    }

    msg.encoded_transactions = std::move(txs_result.value());

    if (!decoder.IsFinished())
    {
        return rlp::DecodingError::kInputTooLong;
    }

    return msg;
}

// GET_BLOCK_BODIES
EncodeResult encode_get_block_bodies(const GetBlockBodiesMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        if (!encoder.add(msg.request_id.value())) { return rlp::EncodingError::kPayloadTooLarge; }
        auto payload_result = encode_hash_list(encoder, msg.block_hashes);
        if (!payload_result) { return payload_result.error(); }
        if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    }
    else
    {
        auto payload_result = encode_hash_list(encoder, msg.block_hashes);
        if (!payload_result) { return payload_result.error(); }
    }

    return finalize_encoding(encoder);
}

DecodeResult<GetBlockBodiesMessage> decode_get_block_bodies(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    GetBlockBodiesMessage msg;
    consume_eth66_request_id(decoder, msg.request_id);

    auto hashes_result = decode_hash_list(decoder);
    if (!hashes_result) { return hashes_result.error(); }
    msg.block_hashes = std::move(hashes_result.value());

    if (!decoder.IsFinished()) { return rlp::DecodingError::kInputTooLong; }
    return msg;
}

// BLOCK_BODIES
EncodeResult encode_block_bodies(const BlockBodiesMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    auto encode_bodies = [&](rlp::RlpEncoder& enc) -> rlp::EncodingOperationResult
    {
        if (!enc.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        for (const auto& body : msg.bodies)
        {
            auto res = encode_block_body(enc, body);
            if (!res) { return res; }
        }
        if (!enc.EndList()) { return rlp::EncodingError::kUnclosedList; }
        return rlp::outcome::success();
    };

    if (msg.request_id.has_value())
    {
        if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }
        if (!encoder.add(msg.request_id.value())) { return rlp::EncodingError::kPayloadTooLarge; }
        auto res = encode_bodies(encoder);
        if (!res) { return res.error(); }
        if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }
    }
    else
    {
        auto res = encode_bodies(encoder);
        if (!res) { return res.error(); }
    }

    return finalize_encoding(encoder);
}

DecodeResult<BlockBodiesMessage> decode_block_bodies(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    auto list_size = decoder.ReadListHeaderBytes();
    if (!list_size) { return list_size.error(); }

    BlockBodiesMessage msg;
    consume_eth66_request_id(decoder, msg.request_id);

    // outer bodies list
    auto bodies_list_size = decoder.ReadListHeaderBytes();
    if (!bodies_list_size) { return bodies_list_size.error(); }

    const size_t payload = bodies_list_size.value();
    const size_t start   = decoder.Remaining().size();
    const size_t target  = start - payload;

    while (decoder.Remaining().size() > target)
    {
        auto body = decode_block_body(decoder);
        if (!body) { return body.error(); }
        msg.bodies.push_back(std::move(body.value()));
    }

    if (decoder.Remaining().size() != target) { return rlp::DecodingError::kListLengthMismatch; }
    if (!decoder.IsFinished()) { return rlp::DecodingError::kInputTooLong; }
    return msg;
}

// NEW_BLOCK
EncodeResult encode_new_block(const NewBlockMessage& msg) noexcept
{
    rlp::RlpEncoder encoder;

    // NewBlock: [[header, txs, ommers], totalDifficulty]
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }

    // Inner block: [header, txList, ommersList]
    if (!encoder.BeginList()) { return rlp::EncodingError::kUnclosedList; }

    auto encoded_header = codec::encode_block_header(msg.header);
    if (!encoded_header) { return encoded_header.error(); }
    if (!encoder.AddRaw(rlp::ByteView(encoded_header.value().data(), encoded_header.value().size()))) { return rlp::EncodingError::kPayloadTooLarge; }

    auto tx_res = encode_transaction_list(encoder, msg.transactions);
    if (!tx_res) { return tx_res.error(); }

    auto ommers_res = encode_block_headers_payload(encoder, msg.ommers);
    if (!ommers_res) { return ommers_res.error(); }

    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

    if (!encoder.add(msg.total_difficulty)) { return rlp::EncodingError::kPayloadTooLarge; }

    if (!encoder.EndList()) { return rlp::EncodingError::kUnclosedList; }

    return finalize_encoding(encoder);
}

DecodeResult<NewBlockMessage> decode_new_block(rlp::ByteView rlp_data) noexcept
{
    rlp::RlpDecoder decoder(rlp_data);

    // Outer list: [[header, txs, ommers], totalDifficulty]
    auto outer_size = decoder.ReadListHeaderBytes();
    if (!outer_size) { return outer_size.error(); }

    // Inner block list: [header, txs, ommers]
    auto inner_size = decoder.ReadListHeaderBytes();
    if (!inner_size) { return inner_size.error(); }

    const size_t inner_payload = inner_size.value();
    const size_t inner_start   = decoder.Remaining().size();
    const size_t inner_target  = inner_start - inner_payload;

    NewBlockMessage msg;

    // Decode header as raw item (includes RLP list prefix)
    auto header_view = consume_next_item(decoder);
    if (!header_view) { return header_view.error(); }
    auto header = codec::decode_block_header(header_view.value());
    if (!header) { return header.error(); }
    msg.header = std::move(header.value());

    auto txs = decode_transaction_list(decoder);
    if (!txs) { return txs.error(); }
    msg.transactions = std::move(txs.value());

    auto ommers = decode_block_headers_payload(decoder);
    if (!ommers) { return ommers.error(); }
    msg.ommers = std::move(ommers.value());

    // Verify we consumed exactly the inner list
    if (decoder.Remaining().size() != inner_target) { return rlp::DecodingError::kListLengthMismatch; }

    if (!decoder.read(msg.total_difficulty)) { return rlp::DecodingError::kUnexpectedString; }

    return msg;
}

} // namespace eth::protocol

