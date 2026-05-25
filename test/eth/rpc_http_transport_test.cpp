// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <eth/rpc_http_transport.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <cstring>
#include <utility>
#include <stdexcept>
#include <thread>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

constexpr const char* kTestCertPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDCTCCAfGgAwIBAgIUMT/MuIjUpU+GdzFPN2yWzvWIHEgwDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDUyNDIzMzcyOFoXDTI2MDUy\n"
    "NTIzMzcyOFowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEApWdBLRsDn6LRZCJyT60Zm1njR2iTrJyc7Mk+I4ZoCpkc\n"
    "AJSU2KscINKIpmz9QGr7dAz79NI1JLHrTIL7bvdkFZrNNtglZp2o1YnFheqzApLM\n"
    "kGlQh6Jej1/qNzQXLSk0VjBPxPXTOXzjTwuDVsnMz0nw0JQavmlzdaE1b8W7QJe4\n"
    "+bE1bOxlhiBb3No1uq3DRtR5ozzi+rYRd3sE5KdLkg7oB3gVG2E0N1N2q3iSdBST\n"
    "QTgLID++0ZiXf/VhtcEhVdVbMZUpIbe/wetCs8Eu+sGX8K8VoPqDOm/PgIphz2na\n"
    "pO6pgp+7zRr9tJ6NZoSQmNQhlOZ6+oqjf9gNnV/BcwIDAQABo1MwUTAdBgNVHQ4E\n"
    "FgQUSB9+gDJD9X/kncHpjJrX2ZnppIcwHwYDVR0jBBgwFoAUSB9+gDJD9X/kncHp\n"
    "jJrX2ZnppIcwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAUv9d\n"
    "+T5YGoK+cI4rFPdf58q4zVuG/7pQNTYRyfsKJV+QEHQWSHp4HhiQ8XBm04N/oduQ\n"
    "VKCBG0A0Ry2ODJZO98NBGHCfAqET9Vx4/hK9GTO8QYm9QhULxK9OxTyFCPWC+tv0\n"
    "Ubd4kYQuUjAI40vaC2mbx6wfCDS0YwSq4sLc5ekovjm4Cj7lfcmsTO0nXzxJF1WZ\n"
    "ktTdFDRHWkxCVC40NC+RWFDoh37jHAXYbrBb466ocjh7z66z3GjjQL3iwGe0cNiS\n"
    "DBa0d+rq5MKmEsOSfSJnwhRk7bouLO6pjWK93++Z8wHQBYOGiulPhVccOQb37Y3q\n"
    "ZeMPl1bIf1YA9ieCwg==\n"
    "-----END CERTIFICATE-----\n";

constexpr const char* kTestKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQClZ0EtGwOfotFk\n"
    "InJPrRmbWeNHaJOsnJzsyT4jhmgKmRwAlJTYqxwg0oimbP1Aavt0DPv00jUksetM\n"
    "gvtu92QVms022CVmnajVicWF6rMCksyQaVCHol6PX+o3NBctKTRWME/E9dM5fONP\n"
    "C4NWyczPSfDQlBq+aXN1oTVvxbtAl7j5sTVs7GWGIFvc2jW6rcNG1HmjPOL6thF3\n"
    "ewTkp0uSDugHeBUbYTQ3U3areJJ0FJNBOAsgP77RmJd/9WG1wSFV1VsxlSkht7/B\n"
    "60KzwS76wZfwrxWg+oM6b8+AimHPadqk7qmCn7vNGv20no1mhJCY1CGU5nr6iqN/\n"
    "2A2dX8FzAgMBAAECggEAAko5rBwxoqYoa/smG1We2HhtcxxO3xbpUps+pv3sttOD\n"
    "1buONqm3y/neNnT6f0fROhFr85Rtc+1FVVvYNfhq2aRjrvIId9vlmH89zYArAPVz\n"
    "Vj8pruxf5BrvLTB6xFShCt3EXPUM+utwYjRwmbpVgZEuf6oX7LGdDZsxGDO5R0hM\n"
    "vxSYXfBzoYa0pT30B7U69j9Cz6ILQhZwauihW43eu756sBsiq25jOymDHTJM80AK\n"
    "wSZjOLu9bIqEVu4st//HJE+kJdivPhfqUKBdCD6zMwYXM+Fyqzhi/7A7h0aYxYnv\n"
    "6VbI/a3RfJ3R2WWSZRCCQ5hI3KnR/QvRWJslzFWIgQKBgQDfXvKtwTKrVrYTUSYK\n"
    "PRl6P9GSTYna3E2KaPAvMxSK9clmsNIOzXSsEyAWMwT9qTGY7sHckR/I3BfBei46\n"
    "KpGoQpMouixzLpNqgmeSE2E1MA7niNtjad7D0vIVGSh8mh7OD6d35ZvpaM9OaFwC\n"
    "wF4pR97U641aIxE0OTuyAvoPUwKBgQC9kJWuRJsAKd2J1IQ48Oc8MjX0oCHyEwUR\n"
    "3kgsWrPGvQjQ7S0MKeMg0Kn89JKTahzpDpzTNDDYWTYZ5LdD5bVDoW1Ud+0zMOMi\n"
    "35TnMOiw41nX6f6dhjAAFqNhYnThOEpzrShw3v8qrLh2Jj1VbRb7IZdPZ1JcaX4G\n"
    "wy4E2TjhYQKBgQCdIxZl1bvnfSCphjTUjxcVQUAVRCbuqHyEGj6ddbnF6BK/AzVC\n"
    "5JZnVy0DcPDZ4eTaSVxn5lAN5YdwvJs4oCnHzM3poM8UWHesPgDOaoO//wb9KvHr\n"
    "hdcIu6VB4mjw/xscqzaMyiJcmTb9Wb0g0mNrdvvznaHa/0BjFMBCAoYXsQKBgC5A\n"
    "NqT8TC0wCcN1PIWAEYsYXR3AbEfZ6CTB7S4VO0PEH4CKPbF4DtiU0MTND240N7WN\n"
    "QSou07QVoCOVMDm6tA06N6iiUhdpWCHMF1KJFl0CO4t4pgzdDp0W6On70bSZvWCX\n"
    "4QQZBHzvA1qgXdqX8UF4oqhW9ztg6cTQnkvEjCJBAoGAW/dUCeRvexRJCMnitXso\n"
    "6Cht1D1Vc1vjPScRSu/+QPsAO9ZNTR46KFQi/+qg8lnqjCL9AiTSJKDaC+btg2wV\n"
    "JGw4vKeQ/bPZhugHmwGjEGdkqfnuZrb8FUJP2boEgVyLllTXyFrxboDRPRAwBvqa\n"
    "k0X2+z8/FVckmsCSxAN+Fnk=\n"
    "-----END PRIVATE KEY-----\n";

struct LoopbackHttpServer
{
    explicit LoopbackHttpServer(std::string response_body)
        : acceptor(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0))
        , port(acceptor.local_endpoint().port())
        , response_body(std::move(response_body))
        , worker([this]()
        {
            tcp::socket socket(io);
            boost::system::error_code ec;
            acceptor.accept(socket, ec);
            if (ec)
            {
                return;
            }

            http::request<http::string_body> request;
            beast::flat_buffer buffer;
            http::read(socket, buffer, request, ec);
            if (!ec)
            {
                received_method = std::string(request.method_string());
                received_target = std::string(request.target());
                received_body = request.body();
                received_json = boost::json::parse(received_body).as_object();

                http::response<http::string_body> response{http::status::ok, 11};
                response.set(http::field::content_type, "application/json");
                response.body() = this->response_body;
                response.prepare_payload();
                http::write(socket, response, ec);
            }

            socket.shutdown(tcp::socket::shutdown_both, ec);
        })
    {
    }

    ~LoopbackHttpServer()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    asio::io_context io;
    tcp::acceptor    acceptor;
    uint16_t         port = 0;
    std::string      response_body;
    std::thread      worker;
    std::string      received_method;
    std::string      received_target;
    std::string      received_body;
    boost::json::object received_json;
};

struct LoopbackHttpsServer
{
    explicit LoopbackHttpsServer(std::string response_body)
        : ssl_context(ssl::context::tls_server)
        , acceptor(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0))
        , port(acceptor.local_endpoint().port())
        , response_body(std::move(response_body))
    {
        boost::system::error_code ec;
        ssl_context.use_certificate_chain(
            asio::buffer(kTestCertPem, std::strlen(kTestCertPem)),
            ec);
        if (ec)
        {
            throw std::runtime_error(ec.message());
        }
        ssl_context.use_private_key(
            asio::buffer(kTestKeyPem, std::strlen(kTestKeyPem)),
            ssl::context::pem,
            ec);
        if (ec)
        {
            throw std::runtime_error(ec.message());
        }

        worker = std::thread([this]()
        {
            boost::system::error_code ec;
            ssl::stream<tcp::socket> stream(io, ssl_context);

            acceptor.accept(stream.next_layer(), ec);
            if (ec)
            {
                return;
            }

            stream.handshake(ssl::stream_base::server, ec);
            if (ec)
            {
                return;
            }

            http::request<http::string_body> request;
            beast::flat_buffer buffer;
            http::read(stream, buffer, request, ec);
            if (!ec)
            {
                received_method = std::string(request.method_string());
                received_target = std::string(request.target());
                received_body = request.body();
                received_json = boost::json::parse(received_body).as_object();

                http::response<http::string_body> response{http::status::ok, 11};
                response.set(http::field::content_type, "application/json");
                response.body() = this->response_body;
                response.prepare_payload();
                http::write(stream, response, ec);
            }

            stream.shutdown(ec);
        });
    }

    ~LoopbackHttpsServer()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    ssl::context     ssl_context;
    asio::io_context io;
    tcp::acceptor    acceptor;
    uint16_t         port = 0;
    std::string      response_body;
    std::thread      worker;
    std::string      received_method;
    std::string      received_target;
    std::string      received_body;
    boost::json::object received_json;
};

} // namespace

TEST(RpcHttpTransportTest, PostsJsonRpcRequestAndReturnsBody)
{
    const std::string response =
        R"({"jsonrpc":"2.0","id":7,"result":{"ok":true}})";
    LoopbackHttpServer server(response);

    eth::rpc::RpcHttpTransport transport(
        "http://127.0.0.1:" + std::to_string(server.port) + "/rpc");

    boost::json::object request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["id"] = 7;
    request["params"] = boost::json::array{};

    const auto body = transport.call(request);

    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, response);
    EXPECT_EQ(server.received_method, "POST");
    EXPECT_EQ(server.received_target, "/rpc");
    EXPECT_EQ(server.received_json.at("method").as_string(), "eth_blockNumber");
    EXPECT_EQ(server.received_json.at("id").as_int64(), 7);
}

TEST(RpcHttpTransportTest, RejectsInvalidUrl)
{
    eth::rpc::RpcHttpTransport transport("ftp://127.0.0.1:1/rpc");

    boost::json::object request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["id"] = 1;
    request["params"] = boost::json::array{};

    EXPECT_FALSE(transport.call(request).has_value());
}

TEST(RpcHttpTransportTest, PostsJsonRpcRequestOverHttps)
{
    const std::string response =
        R"({"jsonrpc":"2.0","id":7,"result":{"ok":true}})";
    LoopbackHttpsServer server(response);

    eth::rpc::RpcHttpTransportOptions options;
    options.verify_peer = false;
    eth::rpc::RpcHttpTransport transport(
        "https://127.0.0.1:" + std::to_string(server.port) + "/rpc",
        options);

    boost::json::object request;
    request["jsonrpc"] = "2.0";
    request["method"] = "eth_blockNumber";
    request["id"] = 7;
    request["params"] = boost::json::array{};

    const auto body = transport.call(request);

    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, response);
    EXPECT_EQ(server.received_method, "POST");
    EXPECT_EQ(server.received_target, "/rpc");
    EXPECT_EQ(server.received_json.at("method").as_string(), "eth_blockNumber");
    EXPECT_EQ(server.received_json.at("id").as_int64(), 7);
}
