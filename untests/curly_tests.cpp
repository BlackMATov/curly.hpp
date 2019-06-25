/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#define CATCH_CONFIG_FAST_COMPILE
#include <catch2/catch.hpp>

#include <iostream>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <curly.hpp/curly.hpp>
namespace net = curly_hpp;

#include "png_data.h"
#include "jpeg_data.h"

namespace
{
    class verbose_uploader : public net::upload_handler {
    public:
        verbose_uploader() = default;

        std::size_t size() const override {
            return 0;
        }

        std::size_t upload(char* dst, std::size_t size) override {
            (void)dst;
            std::cout << "---------- ** UPLOAD (" << size << ") ** ---------- " << std::endl;
            return size;
        }
    };

    class verbose_downloader : public net::download_handler {
    public:
        verbose_downloader() = default;

        std::size_t download(const char* src, std::size_t size) override {
            (void)src;
            std::cout << "---------- ** DOWNLOAD (" << size << ") ** ---------- " << std::endl;
            return size;
        }
    };
}

TEST_CASE("curly"){
    net::auto_performer performer;

    SECTION("wait") {
        auto req = net::request_builder("https://httpbin.org/delay/1").send();
        REQUIRE(req.status() == net::request::statuses::pending);
        REQUIRE(req.wait() == net::request::statuses::done);
        REQUIRE(req.status() == net::request::statuses::done);
        auto resp = req.get();
        REQUIRE(resp.code() == 200u);
        REQUIRE(req.status() == net::request::statuses::empty);
    }

    SECTION("error") {
        auto req = net::request_builder("|||").send();
        REQUIRE(req.wait() == net::request::statuses::failed);
        REQUIRE(req.status() == net::request::statuses::failed);
        REQUIRE_FALSE(req.error().empty());
    }

    SECTION("cancel") {
        {
            auto req = net::request_builder("https://httpbin.org/delay/1").send();
            REQUIRE(req.cancel());
            REQUIRE(req.status() == net::request::statuses::canceled);
            REQUIRE(req.error().empty());
        }
        {
            auto req = net::request_builder("https://httpbin.org/status/200").send();
            REQUIRE(req.wait() == net::request::statuses::done);
            REQUIRE_FALSE(req.cancel());
            REQUIRE(req.status() == net::request::statuses::done);
            REQUIRE(req.error().empty());
        }
    }

    SECTION("get") {
        {
            auto req = net::request_builder("https://httpbin.org/status/204").send();
            auto resp = req.get();
            REQUIRE(req.status() == net::request::statuses::empty);
            REQUIRE(resp.code() == 204u);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.cancel());
            REQUIRE_THROWS_AS(req.get(), net::exception);
            REQUIRE(req.status() == net::request::statuses::canceled);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2")
                .response_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req.wait() == net::request::statuses::timeout);
            REQUIRE_THROWS_AS(req.get(), net::exception);
            REQUIRE(req.status() == net::request::statuses::timeout);
        }
    }

    SECTION("http_methods") {
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::methods::put)
                .send();
            REQUIRE(req0.get().code() == 200u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::methods::get)
                .send();
            REQUIRE(req1.get().code() == 405u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::methods::head)
                .send();
            REQUIRE(req2.get().code() == 405u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::methods::post)
                .send();
            REQUIRE(req3.get().code() == 405u);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::methods::put)
                .send();
            REQUIRE(req0.get().code() == 405u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::methods::get)
                .send();
            REQUIRE(req1.get().code() == 200u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::methods::head)
                .send();
            REQUIRE(req2.get().code() == 200u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::methods::post)
                .send();
            REQUIRE(req3.get().code() == 405u);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::methods::put)
                .send();
            REQUIRE(req0.get().code() == 405u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::methods::get)
                .send();
            REQUIRE(req1.get().code() == 405u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::methods::head)
                .send();
            REQUIRE(req2.get().code() == 405u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::methods::post)
                .send();
            REQUIRE(req3.get().code() == 200u);
        }
    }

    SECTION("status_codes") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/200")
                .method(net::methods::put)
                .send();
            REQUIRE(req.get().code() == 200u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/201")
                .method(net::methods::get)
                .send();
            REQUIRE(req.get().code() == 201u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/202")
                .method(net::methods::head)
                .send();
            REQUIRE(req.get().code() == 202u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::methods::post)
                .send();
            REQUIRE(req.get().code() == 203u);
        }
    }

    SECTION("request_inspection") {
        auto req = net::request_builder()
            .url("https://httpbin.org/headers")
            .header("Custom-Header-1", "custom_header_value_1")
            .header("Custom-Header-2", "custom header value 2")
            .send();
        const auto resp = req.get();
        const auto content_j = json::parse(resp.content().as_string_view());
        REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
        REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
    }

    SECTION("response_inspection") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world&world=hello")
                .method(net::methods::get)
                .send();
            const auto resp = req.get();
            const auto content_j = json::parse(resp.content().as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world&world=hello")
                .method(net::methods::post)
                .send();
            const auto resp = req.get();
            const auto content_j = json::parse(resp.content().as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
    }

    SECTION("dynamic_data") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/base64/SFRUUEJJTiBpcyBhd2Vzb21l")
                .send();
            const auto resp = req.get();
            REQUIRE(resp.content().as_string_view() == "HTTPBIN is awesome");
            REQUIRE(req.error().empty());
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req.error().empty());
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(1))
                .send();
            REQUIRE(req.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req.error().empty());
        }
    }

    SECTION("binary") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/png")
                .method(net::methods::get)
                .send().get();
            REQUIRE(resp.code() == 200u);
            REQUIRE(resp.headers().count("Content-Type"));
            REQUIRE(resp.headers().at("Content-Type") == "image/png");
            REQUIRE(untests::png_data_length == resp.content().size());
            REQUIRE(!std::memcmp(resp.content().data().data(), untests::png_data, untests::png_data_length));
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .method(net::methods::get)
                .send().get();
            REQUIRE(resp.code() == 200u);
            REQUIRE(resp.headers().count("Content-Type"));
            REQUIRE(resp.headers().at("Content-Type") == "image/jpeg");
            REQUIRE(untests::jpeg_data_length == resp.content().size());
            REQUIRE(!std::memcmp(resp.content().data().data(), untests::jpeg_data, untests::jpeg_data_length));
        }
    }

    SECTION("redirects") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/redirect/2")
                .method(net::methods::get)
                .send();
            REQUIRE(req.get().code() == 200u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/absolute-redirect/2")
                .method(net::methods::get)
                .send();
            REQUIRE(req.get().code() == 200u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/relative-redirect/2")
                .method(net::methods::get)
                .send();
            REQUIRE(req.get().code() == 200u);
        }
    }

    SECTION("request_body") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::methods::put)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().get();
            const auto content_j = json::parse(resp.content().as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::methods::post)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().get();
            const auto content_j = json::parse(resp.content().as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::methods::post)
                .header("Content-Type", "application/x-www-form-urlencoded")
                .content("hello=world&world=hello")
                .send().get();
            const auto content_j = json::parse(resp.content().as_string_view());
            REQUIRE(content_j["form"]["hello"] == "world");
            REQUIRE(content_j["form"]["world"] == "hello");
        }
    }
}
