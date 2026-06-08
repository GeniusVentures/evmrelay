// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_streaming.hpp>
#include "test_helpers.hpp"
#include <random>
#include <vector>
#include <array>

using namespace rlp;
using namespace rlp::test;

namespace {

Bytes make_random_bytes(size_t length, uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    Bytes out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i)
    {
        out.push_back(dist(rng));
    }
    return out;
}

std::vector<Bytes> make_fuzz_inputs()
{
    std::vector<Bytes> inputs;

    for (uint64_t seed = 1000; seed < 1500; ++seed)
    {
        inputs.push_back(make_random_bytes(1 + (seed % 128), seed));
    }

    for (uint64_t seed = 2000; seed < 2100; ++seed)
    {
        inputs.push_back(make_random_bytes(256 + (seed % 768), seed));
    }

    return inputs;
}

} // namespace

TEST(RlpFuzzTest, DecoderNeverCrashesOnRandomInput)
{
    const auto fuzz_inputs = make_fuzz_inputs();

    for (const auto& bytes : fuzz_inputs)
    {
        ByteView view(bytes);

        while (!view.empty())
        {
            RlpDecoder decoder(view);

            auto peek_result = decoder.PeekHeader();
            if (!peek_result)
            {
                break;
            }

            Bytes out_bytes;
            (void)decoder.read(out_bytes);

            uint64_t out_uint = 0;
            (void)decoder.read(out_uint);

            intx::uint256 out_big{};
            (void)decoder.read(out_big);

            bool out_bool = false;
            (void)decoder.read(out_bool);

            std::vector<uint64_t> out_vec_u64;
            (void)decoder.read_vector(out_vec_u64);

            auto is_list = decoder.IsList();
            if (is_list.has_value() && is_list.value())
            {
                std::vector<uint64_t> out_vec_list;
                (void)decoder.read_vector(out_vec_list);
            }

            std::array<uint8_t, 32> out_arr{};
            (void)decoder.read(out_arr);

            (void)decoder.IsList();
            (void)decoder.IsString();
            (void)decoder.PeekPayloadSizeBytes();
            (void)decoder.Remaining();
            (void)decoder.IsFinished();

            (void)decoder.ReadListHeaderBytes();
            (void)decoder.SkipItem();

            view = decoder.Remaining();
        }
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderNeverCrashesOnMalformedHeaders)
{
    std::vector<Bytes> malformed = {
        {},
        {0x80},
        {0xc0},
        {0xb7},
        {0xbf},
        {0xf7},
        {0xff},
        from_hex("b8"),
        from_hex("b900"),
        from_hex("b90001"),
        from_hex("b9040000"),
        from_hex("ba00000001"),
        from_hex("bb00000000000000"),
        from_hex("f8"),
        from_hex("f900"),
        from_hex("f90001"),
        from_hex("f9040000"),
        from_hex("f8ff"),
        from_hex("b83900"),
        from_hex("bffffffd00"),
        from_hex("b8ffffffffffffff"),
        from_hex("f83d00"),
    };

    for (const auto& bytes : malformed)
    {
        ByteView view(bytes);
        RlpDecoder decoder(view);

        (void)decoder.PeekHeader();
        (void)decoder.IsFinished();
        (void)decoder.Remaining();

        Bytes out;
        (void)decoder.read(out);

        uint64_t val = 0;
        (void)decoder.read(val);

        std::vector<uint64_t> vec;
        (void)decoder.read_vector(vec);

        (void)decoder.SkipItem();
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderNeverCrashesOnTruncatedPayload)
{
    std::vector<Bytes> truncated;

    truncated.push_back(from_hex("83aa"));
    truncated.push_back(from_hex("83aabb"));
    truncated.push_back(from_hex("b84000"));
    truncated.push_back(from_hex("b8400000000000000000000000000000000000"));
    truncated.push_back(from_hex("f90400"));
    truncated.push_back(from_hex("f90400010203"));
    truncated.push_back(from_hex("83"));
    truncated.push_back(from_hex("b8"));
    truncated.push_back(from_hex("f9"));
    truncated.push_back(from_hex("b9000000000000000000000000000000000000000000000000000000000000000000"));
    truncated.push_back(from_hex("b8ffffffffffffffff"));
    truncated.push_back(from_hex("b8ffffffffffffffff0000"));

    for (const auto& bytes : truncated)
    {
        ByteView view(bytes);
        RlpDecoder decoder(view);

        while (!decoder.IsFinished())
        {
            auto header = decoder.PeekHeader();
            if (!header)
            {
                break;
            }

            Bytes out;
            auto result = decoder.read(out);
            if (!result)
            {
                break;
            }
        }
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderNeverCrashesOnOversizedClaims)
{
    std::vector<Bytes> oversized;

    oversized.push_back(from_hex("b8000000"));
    oversized.push_back(from_hex("b801ff"));
    oversized.push_back(from_hex("b801ff00"));

    oversized.push_back(from_hex("b849"));
    for (size_t i = 0; i < 60; ++i) oversized.back().push_back(0);

    std::string huge;
    huge += (char)0xb9;
    huge += (char)0x00;
    huge += (char)0x00;
    huge += (char)0x01;
    huge += (char)0x00;
    huge += (char)0x00;
    huge += (char)0x00;
    for (size_t i = 0; i < 256; ++i) huge += (char)0x00;
    oversized.push_back({huge.begin(), huge.end()});

    for (const auto& bytes : oversized)
    {
        ByteView view(bytes);
        RlpDecoder decoder(view);

        (void)decoder.PeekHeader();

        Bytes out;
        (void)decoder.read(out);

        uint64_t val = 0;
        (void)decoder.read(val);

        intx::uint256 bigval{};
        (void)decoder.read(bigval);
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderNeverCrashesOnNestedLists)
{
    std::vector<Bytes> nested;

    nested.push_back(from_hex("c0"));
    nested.push_back(from_hex("c100"));
    nested.push_back(from_hex("c7c001c380"));
    nested.push_back(from_hex("cecac0c0c0c0c0c0c0c0"));
    nested.push_back(from_hex("cfc0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0"));
    nested.push_back(from_hex("f83ec0c1c0c0c0c0c0c0c0c0c0c0c0c0c0c000"));
    nested.push_back(from_hex("d0c0d0c0d0c0d0c0"));

    for (const auto& bytes : nested)
    {
        ByteView view(bytes);
        RlpDecoder decoder(view);

        while (!decoder.IsFinished())
        {
            auto header = decoder.PeekHeader();
            if (!header)
            {
                break;
            }

            if (header.value().list)
            {
                (void)decoder.ReadListHeaderBytes();
            }
            else
            {
                (void)decoder.SkipItem();
            }
        }
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderNeverCrashesOnSingleByteEdgeCases)
{
    std::vector<uint8_t> single_byte_values;
    for (int i = 0; i < 256; ++i)
    {
        single_byte_values.push_back(static_cast<uint8_t>(i));
    }

    for (auto byte_val : single_byte_values)
    {
        Bytes input = {byte_val};
        ByteView view(input);
        RlpDecoder decoder(view);

        (void)decoder.PeekHeader();
        (void)decoder.IsFinished();
        (void)decoder.Remaining();

        Bytes out;
        (void)decoder.read(out);

        uint64_t val = 0;
        (void)decoder.read(val);

        bool b = false;
        (void)decoder.read(b);

        std::array<uint8_t, 1> arr{};
        (void)decoder.read(arr);

        std::array<uint8_t, 32> arr32{};
        (void)decoder.read(arr32);

        (void)decoder.ReadListHeaderBytes();
        (void)decoder.SkipItem();

        auto is_list = decoder.IsList();
        if (is_list.has_value() && is_list.value())
        {
            std::vector<uint64_t> vec;
            (void)decoder.read_vector(vec);
        }
    }
    SUCCEED();
}

TEST(RlpFuzzTest, DecoderHandlesEmptyAndSingleZero)
{
    {
        auto bytes = from_hex("80");
        ByteView view(bytes);
        RlpDecoder decoder(view);
        Bytes out;
        ASSERT_TRUE(decoder.read(out));
        EXPECT_TRUE(out.empty());
        EXPECT_TRUE(decoder.IsFinished());
    }

    {
        auto bytes = from_hex("00");
        ByteView view(bytes);
        RlpDecoder decoder(view);
        Bytes out;
        ASSERT_TRUE(decoder.read(out));
        EXPECT_EQ(to_hex(out), "00");
        EXPECT_TRUE(decoder.IsFinished());
    }

    {
        auto bytes = from_hex("0f");
        ByteView view(bytes);
        RlpDecoder decoder(view);
        uint64_t val = 0;
        ASSERT_TRUE(decoder.read(val));
        EXPECT_EQ(val, 15U);
        EXPECT_TRUE(decoder.IsFinished());
    }
}

TEST(RlpFuzzTest, DecoderHandlesLeadingZeros)
{
    {
        auto bytes = from_hex("820001");
        ByteView view(bytes);
        RlpDecoder decoder(view);
        uint64_t val = 0;
        EXPECT_FALSE(decoder.read(val));
    }

    {
        auto bytes = from_hex("8100");
        ByteView view(bytes);
        RlpDecoder decoder(view);
        uint64_t val = 0;
        EXPECT_FALSE(decoder.read(val));
    }
}

TEST(RlpFuzzTest, StreamingDecoderNeverCrashesOnRandomInput)
{
    const auto fuzz_inputs = make_fuzz_inputs();

    for (const auto& bytes : fuzz_inputs)
    {
        ByteView view(bytes);
        RlpDecoder decoder(view);

        auto header = decoder.PeekHeader();
        if (!header)
        {
            continue;
        }

        if (header.value().list)
        {
            RlpChunkedListDecoder chunked(view);
            (void)chunked.peekTotalSize();
            (void)chunked.peekChunkCount();

            while (true)
            {
                auto chunk = chunked.readChunk();
                if (!chunk || chunk.value().empty())
                {
                    break;
                }
            }
        }
        else
        {
            RlpLargeStringDecoder large(view);
            (void)large.peekPayloadSize();

            while (true)
            {
                auto chunk = large.readChunk(64);
                if (!chunk || chunk.value().empty())
                {
                    break;
                }
            }
        }
    }
    SUCCEED();
}
