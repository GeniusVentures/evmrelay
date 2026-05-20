// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <discv5/enr_tree.hpp>

#include <unordered_map>

namespace
{

const char* kValidEnr =
    "enr:-KG4QMOEswP62yzDjSwWS4YEjtTZ5PO6r65CPqYBkgTTkrpaedQ8uEUo1uMALtJIvb2w_WWEVmg5yt1UAuK1ftxUU7QDhGV0aDKQu6TalgMAAAD__________4JpZIJ2NIJpcIQEnfA2iXNlY3AyNTZrMaEDfol8oLr6XJ7FsdAYE7lpJhKMls4G_v6qQOGKJUWGb_uDdGNwgiMog3VkcIIjKA";

} // namespace

TEST(EnrTreeResolverTest, ParsesEnrTreeUrl)
{
    discv5::EnrTreeUrl url{};
    ASSERT_TRUE(discv5::EnrTreeResolver::parse_url(
        "enrtree://PUBKEY@example.org",
        url));
    EXPECT_EQ(url.public_key, "PUBKEY");
    EXPECT_EQ(url.domain, "example.org");
}

TEST(EnrTreeResolverTest, ResolvesRootBranchAndValidEnrs)
{
    const std::unordered_map<std::string, std::vector<std::string>> records = {
        {"example.org", {"enrtree-root:v1 e=root l=links seq=1 sig=ignored"}},
        {"root.example.org", {"enrtree-branch:leaf,bad"}},
        {"leaf.example.org", {kValidEnr}},
        {"bad.example.org", {"enr:invalid"}}
    };

    discv5::EnrTreeResolver resolver(
        [&records](const std::string& name)
        {
            const auto it = records.find(name);
            return it == records.end() ? std::vector<std::string>{} : it->second;
        });

    const auto enrs = resolver.resolve({"enrtree://PUBKEY@example.org"});
    ASSERT_EQ(enrs.size(), 1U);
    EXPECT_EQ(enrs.front(), kValidEnr);
}

TEST(EnrTreeResolverTest, ResolvesBreadthFirstAcrossBranches)
{
    const std::unordered_map<std::string, std::vector<std::string>> records = {
        {"example.org", {"enrtree-root:v1 e=root l=links seq=1 sig=ignored"}},
        {"root.example.org", {"enrtree-branch:deep,leaf"}},
        {"deep.example.org", {"enrtree-branch:deeper"}},
        {"deeper.example.org", {"enrtree-branch:deepest"}},
        {"deepest.example.org", {"enr:invalid"}},
        {"leaf.example.org", {kValidEnr}}
    };

    discv5::EnrTreeResolver resolver(
        [&records](const std::string& name)
        {
            const auto it = records.find(name);
            return it == records.end() ? std::vector<std::string>{} : it->second;
        });

    const auto enrs = resolver.resolve({"enrtree://PUBKEY@example.org"}, 1U);
    ASSERT_EQ(enrs.size(), 1U);
    EXPECT_EQ(enrs.front(), kValidEnr);
}

TEST(EnrTreeResolverTest, DefaultEthereumRootsAreConfigured)
{
    const auto roots = discv5::default_enr_tree_urls_for_chain("ethereum-mainnet", 1U);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_NE(roots.front().find("all.mainnet.ethdisco.net"), std::string::npos);
}
