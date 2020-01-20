/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <catch2/catch.hpp>

#include <fstream>
#include <utility>
#include <iostream>
#include <sys/stat.h>

#include <string_view>
using namespace std::literals;

#include <rapidjson/document.h>
namespace json = rapidjson;

#include <curly.hpp/curly.hpp>
namespace net = curly_hpp;

#include <promise.hpp/promise.hpp>
#include <curl/curl.h>

namespace netex = promise_hpp;

#include "png_data.h"
#include "jpeg_data.h"

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

        [[nodiscard]] std::size_t size() const override {
            return 10;
        }

        std::size_t read(char* dst, std::size_t size) override {
            (void)dst;
            (void)size;
            throw std::exception();
        }

        int seek(curl_off_t, int) override
        {
            return CURL_SEEKFUNC_CANTSEEK;
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
            net::request_builder(url)
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

    class file_downloader : public net::download_handler {
    public:
        explicit file_downloader(const char* filename)
                : stream_(filename, std::ofstream::binary) {}

        std::size_t write(const char* src, std::size_t size) override {
            stream_.write(src, size);
            return size;
        }
    private:
        std::ofstream stream_;
    };

    // S_ISREG is not defined on windows, this fixes it.
    #if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
    #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
    #endif

    bool is_regular_file(const char* file)
    {
        struct stat st{};
        stat(file, &st);
        return S_ISREG(st.st_mode);
    }

    size_t file_size(const char* file)
    {
        struct stat st{};
        stat(file, &st);
        return st.st_size;
    }
}

TEST_CASE("curly") {
    net::performer performer;
    performer.wait_activity(net::time_ms_t(10));

    SECTION("wait") {
        {
            auto req = net::request_builder("https://httpbin.org/delay/1").send();
            REQUIRE(req.status() == net::req_status::pending);
            REQUIRE(req.wait() == net::req_status::done);
            REQUIRE(req.status() == net::req_status::done);
            auto resp = req.take();
            REQUIRE(resp.http_code() == net::response_code::OK);
            REQUIRE(req.status() == net::req_status::empty);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.wait_for(net::time_sec_t(1)) == net::req_status::pending);
            REQUIRE(req.wait_for(net::time_sec_t(5)) == net::req_status::done);
            REQUIRE(req.take().http_code() == net::response_code::OK);
        }
        {
            auto req = net::request_builder("https://httpbin.org/delay/2").send();
            REQUIRE(req.wait_until(net::time_point_t::clock::now() + net::time_sec_t(1))
                == net::req_status::pending);
            REQUIRE(req.wait_until(net::time_point_t::clock::now() + net::time_sec_t(5))
                == net::req_status::done);
            REQUIRE(req.take().http_code() == net::response_code::OK);
        }
    }

    SECTION("error") {
        auto req = net::request_builder("|||").send();
        REQUIRE(req.wait() == net::req_status::failed);
        REQUIRE(req.status() == net::req_status::failed);
        REQUIRE_FALSE(req.get_error().empty());
    }

    SECTION("cancel") {
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

    SECTION("is_done/is_pending") {
        {
            auto req = net::request_builder(net::http_method::GET)
                .url("https://httpbin.org/delay/1")
                .send();
            REQUIRE_FALSE(req.is_done());
            REQUIRE(req.is_pending());
            REQUIRE(req.wait() == net::req_status::done);
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
            REQUIRE(req.wait() == net::req_status::timeout);
            REQUIRE_FALSE(req.is_done());
            REQUIRE_FALSE(req.is_pending());
            REQUIRE(!req.get_error().empty());
        }
    }

    SECTION("get") {
        {
            auto req = net::request_builder("https://httpbin.org/status/204").send();
            auto resp = req.take();
            REQUIRE(req.status() == net::req_status::empty);
            REQUIRE(resp.http_code() == net::response_code::No_Content);
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
        {
            auto req = net::request_builder(net::http_method::GET)
                    .url("http://www.google.com:81")
                    .connection_timeout(net::time_ms_t(100))
                    .send();
            REQUIRE(req.wait() == net::req_status::timeout);
            REQUIRE_THROWS_AS(req.take(), net::exception);
            REQUIRE(req.status() == net::req_status::timeout);
        }
    }

    SECTION("http_methods") {
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == net::response_code::OK);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/put")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == net::response_code::Method_Not_Allowed);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == net::response_code::OK);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == net::response_code::OK);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/get")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == net::response_code::Method_Not_Allowed);
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req0.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req1 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req1.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req2 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req2.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req3 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req3.take().http_code() == net::response_code::OK);

            auto req4 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req4.take().http_code() == net::response_code::Method_Not_Allowed);

            auto req5 = net::request_builder()
                .url("https://httpbin.org/post")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req5.take().http_code() == net::response_code::Method_Not_Allowed);
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

    SECTION("status_codes") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/200")
                .method(net::http_method::PUT)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::OK);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/201")
                .method(net::http_method::GET)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::Created);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/202")
                .method(net::http_method::HEAD)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::Accepted);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::POST)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::Non_authoritativeInformation);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::PATCH)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::Non_authoritativeInformation);
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/status/203")
                .method(net::http_method::DEL)
                .send();
            REQUIRE(req.take().http_code() == net::response_code::Non_authoritativeInformation);
        }
    }

    SECTION("request_inspection") {
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

    SECTION("response_inspection") {
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

    SECTION("dynamic_data") {
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

    SECTION("binary") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/bytes/5")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == net::response_code::OK);
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
            REQUIRE(resp.http_code() == net::response_code::OK);
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
                .response_timeout(net::time_sec_t(3))
                .send();
            REQUIRE(req.wait_for(net::time_sec_t(1)) == net::req_status::pending);
            REQUIRE(req.wait_for(net::time_sec_t(5)) == net::req_status::timeout);
        }
        {
            auto resp = net::request_builder()
                .url("http://httpbin.org/base64/SFRUUEJJTiBpcyBhd2Vzb21l")
                .method(net::http_method::GET)
                .send().take();
            REQUIRE(resp.http_code() == net::response_code::OK);
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
            REQUIRE(resp.http_code() == net::response_code::OK);
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
            REQUIRE(resp.http_code() == net::response_code::OK);
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
            REQUIRE(resp.http_code() == net::response_code::OK);
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
            REQUIRE(resp.http_code() == net::response_code::OK);
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

    SECTION("redirects") {
        {
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == net::response_code::OK);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/absolute-redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == net::response_code::OK);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/relative-redirect/2")
                    .method(net::http_method::GET)
                    .send();
                REQUIRE(req.take().http_code() == net::response_code::OK);
            }
        }
        {
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(0)
                    .send();
                REQUIRE(req.take().http_code() == net::response_code::Found);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(1)
                    .send();
                REQUIRE(req.wait() == net::req_status::failed);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(2)
                    .send();
                REQUIRE(req.wait() == net::req_status::failed);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::http_method::GET)
                    .redirections(3)
                    .send();
                REQUIRE(req.take().http_code() == net::response_code::OK);
            }
        }
    }

    SECTION("request_body") {
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
        {
            auto content = net::content_t{R"({"hello":"world"})"};
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::POST)
                .header("Content-Type", "application/json")
                .content(content)
                .send()
                .take();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
    }

    SECTION("escaped request body")
    {
        auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::http_method::POST)
                .header("Content-Type", "application/x-www-form-urlencoded")
                .content(curly_hpp::qparams_t{{{"data", "%&="}}} )
                .send().take();

        const auto content_j = json_parse(resp.content.as_string_view());
        REQUIRE(content_j["form"]["data"] == "%&=");
    }

    SECTION("ssl_verification") {
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

    SECTION("cancelled_handlers") {
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

    SECTION("callback") {
        {
            std::atomic_size_t call_once{0u};
            auto req = net::request_builder("http://www.httpbin.org/get")
                .callback([&call_once](net::request request){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++call_once;
                    REQUIRE(request.is_done());
                    REQUIRE(request.status() == net::req_status::done);
                    REQUIRE(request.take().http_code() == net::response_code::OK);
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

    SECTION("callback_exception") {
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

TEST_CASE("curly_examples") {
    net::performer performer;

    SECTION("Get Requests") {
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

    SECTION("Post Requests") {
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

    SECTION("Query Parameters") {
        auto request = net::request_builder()
            .url("http://httpbin.org/anything")
            .qparam("hello", "world")
            .send();

        auto response = request.take();
        std::cout << "Last URL: " << response.last_url() << std::endl;

        // Last URL: http://httpbin.org/anything?hello=world
    }

    SECTION("Error Handling") {
        auto request = net::request_builder()
            .url("http://unavailable.site.com")
            .send();

        REQUIRE(request.wait() == net::req_status::failed);

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

    SECTION("Request Callbacks") {
        auto request = net::request_builder("http://www.httpbin.org/get")
            .callback([](net::request request){
                if ( request.is_done() ) {
                    auto response = request.take();
                    std::cout << "Status code: " << response.http_code() << std::endl;
                } else {
                    std::cout << "Error message: " << request.get_error() << std::endl;
                }
            }).send();

        REQUIRE(request.wait_callback() == net::req_status::empty);
        // Status code: 200
    }

    SECTION("Streamed Requests") {
        {
            net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .downloader<file_downloader>("image.jpeg")
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
                    stream_.read(dst, size);
                    return size;
                }

                int seek(curl_off_t offset, int origin) override
                {
                    auto res = CURL_SEEKFUNC_CANTSEEK;

                    if(origin == SEEK_SET)
                    {
                        stream_.seekg(offset, std::ios::beg);
                        res = CURL_SEEKFUNC_OK;
                    }

                    return res;
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

    SECTION("Promised Requests") {
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

TEST_CASE("proxy", "[!mayfail]") {
    net::performer performer;

    {
        try {
            auto ipinfo_req = net::request_builder()
                    .method(net::http_method::GET)
                    .url("https://api.ipify.org?format=json")
                    .send();

            // synchronous waits and get a response
            const char* default_ip = R"({"ip":"0.0.0.0"})";
            json::Document ipinfo;
            ipinfo.Parse(default_ip);
            try {
                auto ipinfo_resp = ipinfo_req.take();
                ipinfo = json_parse(ipinfo_resp.content.as_string_view());
                REQUIRE(ipinfo_resp.http_code() == net::response_code::OK);
            }
            catch (net::exception& e)
            {
                const auto& error = ipinfo_req.get_error();
                FAIL("Request failed with: " + error);
            }

            SECTION("Without proxy")
            {
                auto url = std::string("https://proxycheck.io/v2/") + ipinfo["ip"].GetString();
                auto req = net::request_builder()
                        .url(url)
                        .method(net::http_method::GET)
                        .verification(true)
                        .send();
                try {
                    const auto resp = req.take();
                    const auto resp_content = json_parse(resp.content.as_string_view());

                    REQUIRE(resp.http_code() == net::response_code::OK);
                    REQUIRE(resp_content[ipinfo["ip"].GetString()]["proxy"] == "no");
                }
                catch (net::exception& e)
                {
                    const auto& error = req.get_error();
                    FAIL("Request failed with: " + error);
                }
            }
            SECTION("With proxy")
            {
                auto proxy_req = net::request_builder()
                        .url("http://pubproxy.com/api/proxy?format=json&type=http&speed=15&limit=5")
                        .method(net::http_method::GET)
                        .send();
                auto proxy_resp = proxy_req.take();
                REQUIRE(proxy_resp.http_code() == net::response_code::OK);

                // Try each proxy until we succeed.
                auto proxy_content = json_parse(proxy_resp.content.as_string_view());

                auto& data = proxy_content["data"];

                auto success = false;
                auto i = -1;

                while(!success && ++i < 5)
                {
                    const auto& proxy = data[i];
                    auto proxy_address = std::string("http://") + proxy["ipPort"].GetString();
                    std::cout << "Trying proxy: " << proxy_address;

                    auto url = std::string("https://proxycheck.io/v2/") + ipinfo["ip"].GetString();
                    auto req = net::request_builder()
                            .url(url)
                            .method(net::http_method::GET)
                            .proxy({proxy_address, "user", "password"})
                            .verification(true)
                            .send();

                    try
                    {
                        const auto resp = req.take();
                        const auto resp_content = json_parse(resp.content.as_string_view());
                        success = resp.http_code() == net::response_code::OK && resp_content["status"] == "denied";
                    }
                    catch (net::exception& e)
                    {
                        const auto& error = req.get_error();
                        std::cout << "Request using proxy " << proxy_address << " failed with: " << error;
                    }
                }

                REQUIRE(success);
            }
        }
        catch (net::exception& e)
        {
            std::cout << "exception: " << e.what();
            FAIL(e.what());
        }
    }
}


// This test will fail on macOS and windows as the ssl backend requires the client-certificate to be installed in the keychain
SCENARIO("Public key authentication", "[!mayfail]")
{
    net::performer performer;
    WHEN("Downloading P12 client cert")
    {
        try
        {
            net::request_builder()
                    .url("https://badssl.com/certs/badssl.com-client.p12")
                    .downloader<file_downloader>("./badssl.com-client.p12")
                    .send()
                    .take();

            auto builder = net::request_builder();

            builder.url("https://client.badssl.com")
            .method(net::http_method::GET)
            .client_certificate({"./badssl.com-client.p12", net::ssl_cert::P12, "badssl.com"})
            // Depending on where curl is built, the location of certificates are different.
            // Ubuntu uses /etc/ssl/certs. On macOS no path is required since DarwinSSL uses the cert store.
            // As it is impossible to setup this test to work on all machines with TLS-chain verification,
            // verification must be disabled.
            .verification(false);

            REQUIRE(builder.client_certificate().password() == "badssl.com");
            REQUIRE(std::string{builder.client_certificate().type()} == net::ssl_cert::P12.type());

            auto req = builder.send();

            try
            {
                auto resp = req.take();
                REQUIRE(resp.http_code() == net::response_code::OK);
            }
            catch (net::exception& e)
            {
#if defined(__linux__)
                const auto& error = req.get_error();
                FAIL("Request failed with: " + error);
#endif
            }
        }
        catch(curly_hpp::exception& ex)
        {
            FAIL(ex.what());
        }
    }
}

SCENARIO("Allowing requests to be created in a context that goes out of scope")
{
    class Scope
    {
        public:
            explicit Scope(std::string url)
            : url(std::move(url)), request()
            {
            }

            void send()
            {
                request = curly_hpp::request_builder().url(url).send();
            }

            curly_hpp::response_code get_result()
            {
                auto response = request.take();
                return response.http_code();
            }
        private:
            curly_hpp::performer performer{};
            std::string url;
            curly_hpp::request request;
    };

    GIVEN("A helper class for curly that has the request as a member")
    {
        Scope scope("https://httpbin.org/delay/1");
        THEN("We initiate the request from a method")
        {
            scope.send();
            REQUIRE(scope.get_result() == net::response_code::OK);
        }
    }
}

SCENARIO("Translating response codes")
{
    REQUIRE("OK" == net::as_string(net::response_code::OK));
    REQUIRE("Invalid response code" == net::as_string(static_cast<net::response_code>(9999)));
}

SCENARIO("Escaped content")
{
    net::performer performer{};

    net::qparams_t params{};
    const auto content_1 =  R"!!(%&/()= )!!"sv;
    const auto content_2 = R"!!(!"#¤%&/()=? )!!"sv;

    params.emplace("1",content_1 );
    params.emplace("2", content_2);

    auto resp = net::request_builder(net::http_method::POST)
            .url("http://httpbin.org/anything")
            .header("accept", "application/json")
            .content(params)
            .send().take();

    REQUIRE(resp.http_code() == net::response_code::OK);

    auto data = json_parse(resp.content.as_string_view());

    REQUIRE(data["form"]["1"].GetString() == content_1);
    REQUIRE(data["form"]["2"].GetString() == content_2);
}

SCENARIO("Resume download")
{
    const char* url = "https://cdimage.debian.org/debian-cd/current/amd64/iso-dvd/SHA256SUMS";
    const char* local_file = "test-file";
    net::performer performer{};

    try
    {
        if (is_regular_file(local_file))
        {
            remove(local_file);
        }

        {
            // First download the entire file
            auto resp = net::request_builder()
                    .url(url)
                    .downloader<file_downloader>(local_file)
                    .send().take();

            REQUIRE(resp.http_code() == net::response_code::OK);
        }

        // Get file size
        auto size = file_size(local_file);
        REQUIRE(size > 0);
        remove(local_file);

        // Now download a part of it
        const auto offset = size - size / 2;
        const auto expected_size = size - offset;

        {
            auto resp = net::request_builder()
                    .url(url)
                    .downloader<file_downloader>(local_file)
                    .resume_offset(offset)
                    .send().take();

            REQUIRE(resp.http_code() == net::response_code::Partial_Content);
        }
        REQUIRE(file_size(local_file) == expected_size);
    }
    catch(std::exception& ex)
    {
        FAIL(ex.what());
    }

    // Always cleanup downloaded file
    if (is_regular_file(local_file))
    {
        remove(local_file);
    }
}

SCENARIO("Custom CABundle")
{
    const char *url = "https://curl.haxx.se/ca/cacert.pem";
    const char *local_file = "cacert.pem";
    net::performer performer{};

    try
    {
        WHEN("CA-bundle doesn't exist")
        {
            THEN("Request fails")
            {
                auto req = net::request_builder("https://sha256.badssl.com")
                        .method(net::http_method::HEAD)
                        .verification(true, std::nullopt, local_file)
                        .send();
                REQUIRE(req.wait() == net::req_status::failed);
            }
        }

        AND_WHEN("CA-bundle exists")
        {
            if (is_regular_file(local_file))
            {
                remove(local_file);
            }
            {
                // First download the entire file
                auto resp = net::request_builder()
                        .url(url)
                        .downloader<file_downloader>(local_file)
                        .send().take();

                REQUIRE(resp.http_code() == net::response_code::OK);
            }
            THEN("Request is successful")
            {
                auto req = net::request_builder("https://sha256.badssl.com")
                        .method(net::http_method::HEAD)
                        .verification(true, std::nullopt, local_file)
                        .send();
                REQUIRE(req.wait() == net::req_status::done);
            }
        }
    }
    catch (std::exception &ex)
    {
        FAIL(ex.what());
    }

    // Always cleanup downloaded file
    if (is_regular_file(local_file))
    {
        remove(local_file);
    }
}

SCENARIO("Public key pinning")
{
    try
    {
        net::performer performer{};
        constexpr const char* url = "https://httpbin.org/status/200";
        WHEN("Using a mismatching public key signature")
        {
            THEN("Request fails with 'Pinned pubkey mismatch'")
            {
                auto req1 = net::request_builder(url)
                        .method(net::http_method::GET)
                        .verification(true)
                        .pinned_public_key("sha256//9SLklscvzMYj8f+52lp5ze/hY0CFHyLSPQzSpYYIBm8=")
                        .send();
                REQUIRE(req1.get_error() == "Pinned pubkey mismatch");
                REQUIRE(req1.status() == net::req_status::failed);
            }
        }
        AND_WHEN("Using a matching public key signature")
        {
            THEN("Request is successful")
            {
                auto req2 = net::request_builder(url)
                        .method(net::http_method::GET)
                        .verification(true)
                        .pinned_public_key("sha256//Yvh6l+lXgqrBJrCtxwr9r/vbERE37/5/p6AaRRsiboQ=")
                        .send();
                REQUIRE(req2.take().http_code() == net::response_code::OK);
            }
        }
    }
    catch (std::exception &ex)
    {
        FAIL(ex.what());
    }
}
