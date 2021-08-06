/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019-2021, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include "doctest/doctest.h"

#include <curly.hpp/curly.hpp>
namespace net = curly_hpp;

#include <promise.hpp/promise.hpp>
namespace netex = promise_hpp;

#include <rapidjson/document.h>
namespace json = rapidjson;

#include "png_data.h"
#include "jpeg_data.h"

#include <fstream>
#include <utility>
#include <iostream>

namespace
{
    json::Document json_parse(std::string_view data) {
        json::Document d;
        if ( d.Parse(data.data(), data.size()).HasParseError() ) {
            throw std::logic_error("untests: failed to parse json");
        }
        return d;
    }

    class cancelled_uploader : public net::upload_handler {
    public:
        cancelled_uploader() = default;

        std::size_t size() const override {
            return 10;
        }

        std::size_t read(char* dst, std::size_t size) override {
            (void)dst;
            (void)size;
            throw std::exception();
        }
    };

    class cancelled_downloader : public net::download_handler {
    public:
        cancelled_downloader() = default;

        std::size_t write(const char* src, std::size_t size) override {
            (void)src;
            (void)size;
            throw std::exception();
        }
    };

    class cancelled_progressor : public net::progress_handler {
    public:
        cancelled_progressor() = default;

        float update(
            std::size_t dnow, std::size_t dtotal,
            std::size_t unow, std::size_t utotal) override
        {
            (void)dnow;
            (void)dtotal;
            (void)unow;
            (void)utotal;
            throw std::exception();
        }
    };

    netex::promise<net::content_t> download(std::string url) {
        return netex::make_promise<net::content_t>([
            url = std::move(url)
        ](auto resolve, auto reject){
            net::request_builder(std::move(url))
                .callback([resolve,reject](net::request request) mutable {
                    if ( !request.is_done() ) {
                        reject(net::exception("network error"));
                        return;
                    }
                    net::response response = request.take();
                    if ( response.is_http_error() ) {
                        reject(net::exception("server error"));
                        return;
                    }
                    resolve(std::move(response.content));
                }).send();
        });
    }
}

TEST_CASE("curly") {
    net::performer performer;
    performer.wait_activity(net::time_ms_t(10));

    SUBCASE("wait") {
        {
            auto req = net::request_builder("https://httpbin.org/delay/1").send();
            REQUIRE(req.status() == net::req_status::pending);
            REQUIRE(req.wait() == net::req_status::done);
            REQUIRE(req.status() == net::req_status::done);
            auto resp = req.take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(req.status() == net::req_status::empty);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.wait_for(net::time_sec_t(1)) == net::req_status::pending);
            REQUIRE(req.wait_for(net::time_sec_t(5)) == net::req_status::done);
            REQUIRE(req.take().http_code() == 200u);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.wait_until(net::time_point_t::clock::now() + net::time_sec_t(1))
                == net::req_status::pending);
            REQUIRE(req.wait_until(net::time_point_t::clock::now() + net::time_sec_t(5))
                == net::req_status::done);
            REQUIRE(req.take().http_code() == 200u);
        }
    }

    SUBCASE("error") {
        auto req = net::request_builder("|||").send();
        REQUIRE(req.wait() == net::req_status::failed);
        REQUIRE(req.status() == net::req_status::failed);
        REQUIRE_FALSE(req.get_error().empty());
    }

    SUBCASE("cancel") {
        {
            auto req = net::request_builder("https://httpbin.org/delay/1").send();
            REQUIRE(req.cancel());
            REQUIRE(req.status() == net::req_status::cancelled);
            REQUIRE_FALSE(req.get_error().empty());
        }
        {
            auto req = net::request_builder("https://httpbin.org/status/200").send();
            REQUIRE(req.wait() == net::req_status::done);
            REQUIRE_FALSE(req.cancel());
            REQUIRE(req.status() == net::req_status::done);
            REQUIRE(req.get_error().empty());
        }
    }

    SUBCASE("is_done/is_pending") {
        {
            auto req = net::request_builder(net::http_method::GET)
                .url("https://httpbin.org/delay/1")
                .send();
            REQUIRE_FALSE(req.is_done());
            REQUIRE(req.is_pending());
            req.wait();
            REQUIRE(req.is_done());
            REQUIRE_FALSE(req.is_pending());
        }
        {
            auto req = net::request_builder(net::http_method::POST, "http://www.httpbin.org/post")
                .url("https://httpbin.org/delay/2")
                .request_timeout(net::time_sec_t(1))
                .send();
            REQUIRE_FALSE(req.is_done());
            REQUIRE(req.is_pending());
            req.wait();
            REQUIRE_FALSE(req.is_done());
            REQUIRE_FALSE(req.is_pending());
            REQUIRE(!req.get_error().empty());
        }
    }

    SUBCASE("get") {
        {
            auto req = net::request_builder("https://httpbin.org/status/204").send();
            auto resp = req.take();
            REQUIRE(req.status() == net::req_status::empty);
            REQUIRE(resp.http_code() == 204u);
            REQUIRE(resp.last_url() == "https://httpbin.org/status/204");
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.cancel());
            REQUIRE_THROWS_AS(req.take(), net::exception);
            REQUIRE(req.status() == net::req_status::cancelled);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2")
                .response_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req.wait() == net::req_status::timeout);
            REQUIRE_THROWS_AS(req.take(), net::exception);
            REQUIRE(req.status() == net::req_status::timeout);
        }
    }

    SUBCASE("http_methods") {
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == 200u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == 405u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == 405u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == 405u);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == 405u);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == 405u);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == 405u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == 200u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == 200u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == 405u);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == 405u);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == 405u);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == 405u);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == 405u);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == 405u);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == 200u);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == 405u);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == 405u);
        }
        {
            auto req1 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::OPTIONS)
                .send();
            const auto allow1 = req1.take().headers.at("Allow");
            REQUIRE((allow1 == "PUT, OPTIONS" || allow1 == "OPTIONS, PUT"));

            auto req2 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::OPTIONS)
                .send();
            const auto allow2 = req2.take().headers.at("Allow");
            REQUIRE((allow2 == "POST, OPTIONS" || allow2 == "OPTIONS, POST"));
        }
    }

    SUBCASE("status_codes") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/200")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req.take().http_code() == 200u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/201")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req.take().http_code() == 201u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/202")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req.take().http_code() == 202u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req.take().http_code() == 203u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req.take().http_code() == 203u);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req.take().http_code() == 203u);
        }
    }

    SUBCASE("request_inspection") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/headers")
                .header("Custom-Header-1", "custom_header_value_1")
                .header("Custom-Header-2", "custom header value 2")
                .header("Custom-Header-3", std::string())
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
            REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
            REQUIRE(content_j["headers"]["Custom-Header-3"] == "");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/headers")
                .headers({
                    {"Custom-Header-1", "custom_header_value_1"},
                    {"Custom-Header-2", "custom header value 2"},
                    {"Custom-Header-3", ""}})
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
            REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
            REQUIRE(content_j["headers"]["Custom-Header-3"] == "");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/headers")
                .headers({
                    {"Custom-Header-1", "custom_header_value_1"},
                    {"Custom-Header-2", "custom header value 2"},
                    {"Custom-Header-3", ""}})
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
            REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
            REQUIRE(content_j["headers"]["Custom-Header-3"] == "");
        }
        {
            std::map<std::string, std::string> headers{
                {"Custom-Header-1", "custom_header_value_1"},
                {"Custom-Header-2", "custom header value 2"},
                {"Custom-Header-3", ""}};
            auto resp = net::request_builder()
                .url("https://httpbin.org/headers")
                .headers(headers.begin(), headers.end())
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
            REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
            REQUIRE(content_j["headers"]["Custom-Header-3"] == "");
        }
    }

    SUBCASE("response_inspection") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world&world=hello")
                .method(net::http_method::GET)
                .send();
            const auto resp = req.take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world")
                .method(net::http_method::POST)
                .qparam("world", "hello")
                .send();
            const auto resp = req.take();
            const auto content_j = json_parse(resp.content.as_string_copy());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers")
                .method(net::http_method::GET)
                .qparam("hello", "world")
                .qparam("world", "hello")
                .send();
            const auto resp = req.take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers")
                .method(net::http_method::GET)
                .qparams({
                    {"", "hello"},
                    {"world", ""}
                })
                .send();
            const auto resp = req.take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["hello"] == "");
            REQUIRE(content_j["world"] == "");
        }
        {
            std::map<std::string,std::string> qparams{
                {"hello", "world"},
                {"world", "hello"}
            };
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers")
                .method(net::http_method::GET)
                .qparams(qparams.begin(), qparams.end())
                .send();
            const auto resp = req.take();
            REQUIRE(resp.last_url() == "https://httpbin.org/response-headers?hello=world&world=hello");
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
    }

    SUBCASE("dynamic_data") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/base64/SFRUUEJJTiBpcyBhd2Vzb21l")
                .send();
            const auto resp = req.take();
            REQUIRE(resp.content.as_string_view() == "HTTPBIN is awesome");
            REQUIRE(req.get_error().empty());
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .request_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req0.wait() == net::req_status::timeout);
            REQUIRE_FALSE(req0.get_error().empty());

            auto req1 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req1.wait() == net::req_status::timeout);
            REQUIRE_FALSE(req1.get_error().empty());
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .request_timeout(net::time_sec_t(1))
                .send();
            REQUIRE(req0.wait() == net::req_status::timeout);
            REQUIRE_FALSE(req0.get_error().empty());

            auto req1 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(1))
                .send();
            REQUIRE(req1.wait() == net::req_status::timeout);
            REQUIRE_FALSE(req1.get_error().empty());
        }
    }

    SUBCASE("binary") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/bytes/5")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.content.size() == 5u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "application/octet-stream");
            REQUIRE(resp.headers.at("Content-Length") == "5");
        }
        {
            auto resp = net::request_builder()
                .url("http://httpbin.org/drip?duration=2&numbytes=5&code=200&delay=1")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.content.size() == 5u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "application/octet-stream");
            REQUIRE(resp.headers.at("Content-Length") == "5");
        }
        {
            auto req = net::request_builder()
                .url("http://httpbin.org/drip?duration=15&numbytes=5&code=200&delay=1")
                .method(net::http_method::GET)
                .response_timeout(net::time_sec_t(2))
                .send();
            REQUIRE(req.wait_for(net::time_sec_t(1)) == net::req_status::pending);
            REQUIRE(req.wait_for(net::time_sec_t(5)) == net::req_status::timeout);
        }
        {
            auto resp = net::request_builder()
                .url("http://httpbin.org/base64/SFRUUEJJTiBpcyBhd2Vzb21l")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.content.as_string_view() == "HTTPBIN is awesome");
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "text/html; charset=utf-8");
            REQUIRE(resp.headers.at("Content-Length") == "18");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/png")
                .method(net::http_method::HEAD)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "image/png");
            REQUIRE(resp.headers.at("Content-Length") == std::to_string(untests::png_data_length));
            REQUIRE_FALSE(resp.content.size());
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/png")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "image/png");
            REQUIRE(resp.headers.at("Content-Length") == std::to_string(untests::png_data_length));
            REQUIRE(untests::png_data_length == resp.content.size());
            REQUIRE(!std::memcmp(
                std::move(resp.content).data().data(),
                untests::png_data, untests::png_data_length));
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .method(net::http_method::HEAD)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "image/jpeg");
            REQUIRE(resp.headers.at("Content-Length") == std::to_string(untests::jpeg_data_length));
            REQUIRE_FALSE(resp.content.size());
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.count("Content-Length"));
            REQUIRE(resp.headers.at("Content-Type") == "image/jpeg");
            REQUIRE(resp.headers.at("Content-Length") == std::to_string(untests::jpeg_data_length));
            REQUIRE(untests::jpeg_data_length == resp.content.size());
            REQUIRE(!std::memcmp(
                std::as_const(resp.content).data().data(),
                untests::jpeg_data, untests::jpeg_data_length));
        }
    }

    SUBCASE("redirects") {
        {
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == 200u);
            }
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/absolute-redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == 200u);
            }
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/relative-redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == 200u);
            }
        }
        {
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(0)
                    .send();
                REQUIRE(req.take().http_code() == 302u);
            }
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(1)
                    .send();
                REQUIRE(req.wait() == net::req_status::failed);
            }
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(2)
                    .send();
                REQUIRE(req.wait() == net::req_status::failed);
            }
            {
                auto req = net::request_builder()
                    .url("http://httpbingo.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(3)
                    .send();
                REQUIRE(req.take().http_code() == 200u);
            }
        }
    }

    SUBCASE("request_body") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::PUT)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::PATCH)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::DEL)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::POST)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::POST)
                .header("Content-Type", "application/x-www-form-urlencoded")
                .content("hello=world&world=hello")
                .send().take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["form"]["hello"] == "world");
            REQUIRE(content_j["form"]["world"] == "hello");
        }
    }

    SUBCASE("ssl_verification") {
        {
            auto req0 = net::request_builder("https://expired.badssl.com")
                .method(net::http_method::HEAD)
                .verification(true)
                .send();
            REQUIRE(req0.wait() == net::req_status::failed);

            auto req1 = net::request_builder("https://wrong.host.badssl.com")
                .method(net::http_method::HEAD)
                .verification(true)
                .send();
            REQUIRE(req1.wait() == net::req_status::failed);

            auto req2 = net::request_builder("https://self-signed.badssl.com")
                .method(net::http_method::HEAD)
                .verification(true)
                .send();
            REQUIRE(req2.wait() == net::req_status::failed);

            auto req3 = net::request_builder("https://untrusted-root.badssl.com")
                .method(net::http_method::HEAD)
                .verification(true)
                .send();
            REQUIRE(req3.wait() == net::req_status::failed);
        }
        {
            auto req0 = net::request_builder("https://expired.badssl.com")
                .method(net::http_method::HEAD)
                .verification(false)
                .send();
            REQUIRE(req0.wait() == net::req_status::done);

            auto req1 = net::request_builder("https://wrong.host.badssl.com")
                .method(net::http_method::HEAD)
                .verification(false)
                .send();
            REQUIRE(req1.wait() == net::req_status::done);

            auto req2 = net::request_builder("https://self-signed.badssl.com")
                .method(net::http_method::HEAD)
                .verification(false)
                .send();
            REQUIRE(req2.wait() == net::req_status::done);

            auto req3 = net::request_builder("https://untrusted-root.badssl.com")
                .method(net::http_method::HEAD)
                .verification(false)
                .send();
            REQUIRE(req3.wait() == net::req_status::done);
        }
    }

    SUBCASE("cancelled_handlers") {
        {
            auto req = net::request_builder("https://httpbin.org/anything")
                .verbose(true)
                .method(net::http_method::POST)
                .uploader<cancelled_uploader>()
                .send();
            REQUIRE(req.wait() == net::req_status::cancelled);
        }
        {
            auto req = net::request_builder("https://httpbin.org/anything")
                .verbose(true)
                .method(net::http_method::GET)
                .downloader<cancelled_downloader>()
                .send();
            REQUIRE(req.wait() == net::req_status::cancelled);
        }
        {
            auto req = net::request_builder("https://httpbin.org/anything")
                .verbose(true)
                .method(net::http_method::GET)
                .progressor<cancelled_progressor>()
                .send();
            REQUIRE(req.wait() == net::req_status::cancelled);
        }
    }

    SUBCASE("callback") {
        {
            std::atomic_size_t call_once{0u};
            auto req = net::request_builder("http://www.httpbin.org/get")
                .callback([&call_once](net::request request){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++call_once;
                    REQUIRE(request.is_done());
                    REQUIRE(request.status() == net::req_status::done);
                    REQUIRE(request.take().http_code() == 200u);
                }).send();
            REQUIRE(req.wait_callback() == net::req_status::empty);
            REQUIRE_FALSE(req.get_callback_exception());
            REQUIRE(call_once.load() == 1u);
        }
        {
            std::atomic_size_t call_once{0u};
            auto req = net::request_builder("|||")
                .callback([&call_once](net::request request){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++call_once;
                    REQUIRE_FALSE(request.is_done());
                    REQUIRE(request.status() == net::req_status::failed);
                    REQUIRE_FALSE(request.get_error().empty());
                }).send();
            REQUIRE(req.wait_callback() == net::req_status::failed);
            REQUIRE_FALSE(req.get_callback_exception());
            REQUIRE(call_once.load() == 1u);
        }
        {
            std::atomic_size_t call_once{0u};
            auto req = net::request_builder("http://www.httpbin.org/delay/2")
                .response_timeout(net::time_sec_t(0))
                .callback([&call_once](net::request request){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++call_once;
                    REQUIRE_FALSE(request.is_done());
                    REQUIRE(request.status() == net::req_status::timeout);
                    REQUIRE_FALSE(request.get_error().empty());
                }).send();
            REQUIRE(req.wait_callback() == net::req_status::timeout);
            REQUIRE_FALSE(req.get_callback_exception());
            REQUIRE(call_once.load() == 1u);
        }
        {
            std::atomic_size_t call_once{0u};
            auto req = net::request_builder("http://www.httpbin.org/delay/2")
                .callback([&call_once](net::request request){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++call_once;
                    REQUIRE_FALSE(request.is_done());
                    REQUIRE(request.status() == net::req_status::cancelled);
                    REQUIRE_FALSE(request.get_error().empty());
                }).send();
            REQUIRE(req.cancel());
            REQUIRE(req.wait_callback() == net::req_status::cancelled);
            REQUIRE_FALSE(req.get_callback_exception());
            REQUIRE(call_once.load() == 1u);
        }
    }

    SUBCASE("callback_exception") {
        auto req = net::request_builder("http://www.httpbin.org/post")
            .callback([](net::request request){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if ( request.take().is_http_error() ) {
                    throw std::logic_error("my_logic_error");
                }
            }).send();
        REQUIRE(req.wait_callback() == net::req_status::empty);
        REQUIRE(req.get_callback_exception());
        try {
            std::rethrow_exception(req.get_callback_exception());
        } catch (const std::logic_error& e) {
            REQUIRE(std::string_view("my_logic_error") == e.what());
        }
    }
}

TEST_CASE("curly/cancel_all_pending_requests") {
    SUBCASE("cancel new requests") {
        std::atomic_size_t call_count{0u};

        auto req1 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        auto req2 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        net::cancel_all_pending_requests();

        REQUIRE(call_count == 2u);
        REQUIRE(req1.status() == net::req_status::cancelled);
        REQUIRE(req2.status() == net::req_status::cancelled);
    }

    SUBCASE("cancel active requests") {
        std::atomic_size_t call_count{0u};

        auto req1 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        auto req2 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        net::perform();
        net::cancel_all_pending_requests();

        REQUIRE(call_count == 2u);
        REQUIRE(req1.status() == net::req_status::cancelled);
        REQUIRE(req2.status() == net::req_status::cancelled);
    }
}

TEST_CASE("curly/get_all_pending_requests") {
    SUBCASE("get new requests") {
        std::atomic_size_t call_count{0u};

        auto req1 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        auto req2 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        std::vector<net::request> requests = net::get_all_pending_requests();
        REQUIRE(requests.size() == 2u);

        for ( net::request& req : requests ) {
            req.cancel();
        }

        net::perform();

        REQUIRE(call_count == 2u);
        REQUIRE(req1.status() == net::req_status::cancelled);
        REQUIRE(req2.status() == net::req_status::cancelled);
    }

    SUBCASE("get active requests") {
        std::atomic_size_t call_count{0u};

        auto req1 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        auto req2 = net::request_builder("https://httpbin.org/delay/2")
            .callback([&call_count](net::request request){
                REQUIRE(request.status() == net::req_status::cancelled);
                ++call_count;
            }).send();

        net::perform();

        std::vector<net::request> requests = net::get_all_pending_requests();
        REQUIRE(requests.size() == 2u);

        for ( net::request& req : requests ) {
            req.cancel();
        }

        net::perform();

        REQUIRE(call_count == 2u);
        REQUIRE(req1.status() == net::req_status::cancelled);
        REQUIRE(req2.status() == net::req_status::cancelled);
    }
}

TEST_CASE("curly_examples") {
    net::performer performer;

    SUBCASE("Get Requests") {
        // makes a GET request and async send it
        auto request = net::request_builder()
            .method(net::http_method::GET)
            .url("http://www.httpbin.org/get")
            .send();

        // synchronous waits and take a response
        auto response = request.take();

        // prints results
        std::cout << "Status code: " << response.http_code() << std::endl;
        std::cout << "Content type: " << response.headers["content-type"] << std::endl;
        std::cout << "Body content: " << response.content.as_string_view() << std::endl;

        // Status code: 200
        // Content type: application/json
        // Body content: {
        //     "args": {},
        //     "headers": {
        //         "Accept": "*/*",
        //         "Host": "www.httpbin.org",
        //         "User-Agent": "cURL/7.54.0"
        //     },
        //     "origin": "37.195.66.134, 37.195.66.134",
        //     "url": "https://www.httpbin.org/get"
        // }
    }

    SUBCASE("Post Requests") {
        auto request = net::request_builder()
            .method(net::http_method::POST)
            .url("http://www.httpbin.org/post")
            .header("Content-Type", "application/json")
            .content(R"({"hello" : "world"})")
            .send();

        auto response = request.take();
        std::cout << "Body content: " << response.content.as_string_view() << std::endl;
        std::cout << "Content Length: " << response.headers["content-length"] << std::endl;

        // Body content: {
        //     "args": {},
        //     "data": "{\"hello\" : \"world\"}",
        //     "files": {},
        //     "form": {},
        //     "headers": {
        //         "Accept": "*/*",
        //         "Content-Length": "19",
        //         "Content-Type": "application/json",
        //         "Host": "www.httpbin.org",
        //         "User-Agent": "cURL/7.54.0"
        //     },
        //     "json": {
        //         "hello": "world"
        //     },
        //     "origin": "37.195.66.134, 37.195.66.134",
        //     "url": "https://www.httpbin.org/post"
        // }
        // Content Length: 389
    }

    SUBCASE("Query Parameters") {
        auto request = net::request_builder()
            .url("http://httpbin.org/anything")
            .qparam("hello", "world")
            .send();

        auto response = request.take();
        std::cout << "Last URL: " << response.last_url() << std::endl;

        // Last URL: http://httpbin.org/anything?hello=world
    }

    SUBCASE("Error Handling") {
        auto request = net::request_builder()
            .url("http://unavailable.site.com")
            .send();

        request.wait();

        if ( request.is_done() ) {
            auto response = request.take();
            std::cout << "Status code: " << response.http_code() << std::endl;
        } else {
            // throws net::exception because a response is unavailable
            // auto response = request.take();

            std::cout << "Error message: " << request.get_error() << std::endl;
        }

        // Error message: Could not resolve host: unavailable.site.com
    }

    SUBCASE("Request Callbacks") {
        auto req = net::request_builder("http://www.httpbin.org/get")
            .callback([](net::request request){
                if ( request.is_done() ) {
                    auto response = request.take();
                    std::cout << "Status code: " << response.http_code() << std::endl;
                } else {
                    std::cout << "Error message: " << request.get_error() << std::endl;
                }
            }).send();

        req.wait_callback();
        // Status code: 200
    }

    SUBCASE("Streamed Requests") {
        {
            class file_dowloader : public net::download_handler {
            public:
                file_dowloader(const char* filename)
                : stream_(filename, std::ofstream::binary) {}

                std::size_t write(const char* src, std::size_t size) override {
                    stream_.write(src, static_cast<std::streamsize>(size));
                    return size;
                }
            private:
                std::ofstream stream_;
            };

            net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .downloader<file_dowloader>("image.jpeg")
                .send().take();
        }
        {
            class file_uploader : public net::upload_handler {
            public:
                file_uploader(const char* filename)
                : stream_(filename, std::ifstream::binary) {
                    stream_.seekg(0, std::ios::end);
                    size_ = static_cast<std::size_t>(stream_.tellg());
                    stream_.seekg(0, std::ios::beg);
                }

                std::size_t size() const override {
                    return size_;
                }

                std::size_t read(char* dst, std::size_t size) override {
                    stream_.read(dst, static_cast<std::streamsize>(size));
                    return size;
                }
            private:
                std::size_t size_{0u};
                std::ifstream stream_;
            };

            net::request_builder()
                .method(net::http_method::POST)
                .url("https://httpbin.org/anything")
                .uploader<file_uploader>("image.jpeg")
                .send().take();
        }
    }

    SUBCASE("Promised Requests") {
        auto promise = download("https://httpbin.org/image/png")
            .then([](const net::content_t& content){
                std::cout << content.size() << " bytes downloaded" << std::endl;
            }).except([](std::exception_ptr e){
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception& ee) {
                    std::cerr << "Failed to download: " << ee.what() << std::endl;
                }
            });

        promise.wait();
        // 8090 bytes downloaded
    }
}
