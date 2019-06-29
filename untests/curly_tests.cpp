/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <catch2/catch.hpp>

#include <fstream>
#include <utility>
#include <iostream>

#include <rapidjson/document.h>
namespace json = rapidjson;

#include <curly.hpp/curly.hpp>
namespace net = curly_hpp;

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

    class canceled_uploader : public net::upload_handler {
    public:
        canceled_uploader() = default;

        std::size_t size() const override {
            return 10;
        }

        std::size_t read(char* dst, std::size_t size) override {
            (void)dst;
            (void)size;
            throw std::exception();
        }
    };

    class canceled_downloader : public net::download_handler {
    public:
        canceled_downloader() = default;

        std::size_t write(const char* src, std::size_t size) override {
            (void)src;
            (void)size;
            throw std::exception();
        }
    };
}

TEST_CASE("curly") {
    net::auto_performer performer;
    performer.wait_activity(net::time_ms_t(10));

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

    SECTION("is_ready/is_running") {
        {
            auto req = net::request_builder("https://httpbin.org/delay/1").send();
            REQUIRE(req.is_running());
            REQUIRE_FALSE(req.is_ready());
            REQUIRE(req.cancel());
            REQUIRE_FALSE(req.is_running());
            REQUIRE(req.is_ready());
        }
        {
            auto req = net::request_builder("https://httpbin.org/status/200").send();
            REQUIRE(req.wait() == net::request::statuses::done);
            REQUIRE(req.is_ready());
            REQUIRE_FALSE(req.is_running());
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
            .header("Custom-Header-3", std::string())
            .send();
        const auto resp = req.get();
        const auto content_j = json_parse(resp.content.as_string_view());
        REQUIRE(content_j["headers"]["Custom-Header-1"] == "custom_header_value_1");
        REQUIRE(content_j["headers"]["Custom-Header-2"] == "custom header value 2");
        REQUIRE(content_j["headers"]["Custom-Header-3"] == "");
    }

    SECTION("response_inspection") {
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world&world=hello")
                .method(net::methods::get)
                .send();
            const auto resp = req.get();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["hello"] == "world");
            REQUIRE(content_j["world"] == "hello");
        }
        {
            auto req = net::request_builder()
                .url("https://httpbin.org/response-headers?hello=world&world=hello")
                .method(net::methods::post)
                .send();
            const auto resp = req.get();
            const auto content_j = json_parse(resp.content.as_string_copy());
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
            REQUIRE(resp.content.as_string_view() == "HTTPBIN is awesome");
            REQUIRE(req.error().empty());
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .request_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req0.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req0.error().empty());

            auto req1 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(0))
                .send();
            REQUIRE(req1.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req1.error().empty());
        }
        {
            auto req0 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .request_timeout(net::time_sec_t(1))
                .send();
            REQUIRE(req0.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req0.error().empty());

            auto req1 = net::request_builder()
                .url("https://httpbin.org/delay/10")
                .response_timeout(net::time_sec_t(1))
                .send();
            REQUIRE(req1.wait() == net::request::statuses::timeout);
            REQUIRE_FALSE(req1.error().empty());
        }
    }

    SECTION("binary") {
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/png")
                .method(net::methods::get)
                .send().get();
            REQUIRE(resp.code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.at("Content-Type") == "image/png");
            REQUIRE(untests::png_data_length == resp.content.size());
            REQUIRE(!std::memcmp(
                std::move(resp.content).data().data(),
                untests::png_data, untests::png_data_length));
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .method(net::methods::get)
                .send().get();
            REQUIRE(resp.code() == 200u);
            REQUIRE(resp.headers.count("Content-Type"));
            REQUIRE(resp.headers.at("Content-Type") == "image/jpeg");
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
        {
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::methods::get)
                    .redirections(0)
                    .send();
                REQUIRE(req.get().code() == 302u);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::methods::get)
                    .redirections(1)
                    .send();
                REQUIRE(req.wait() == net::request::statuses::failed);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::methods::get)
                    .redirections(2)
                    .send();
                REQUIRE(req.wait() == net::request::statuses::failed);
            }
            {
                auto req = net::request_builder()
                    .url("https://httpbin.org/redirect/3")
                    .method(net::methods::get)
                    .redirections(3)
                    .send();
                REQUIRE(req.get().code() == 200u);
            }
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
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::methods::post)
                .header("Content-Type", "application/json")
                .content(R"({"hello":"world"})")
                .send().get();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["data"] == R"({"hello":"world"})");
        }
        {
            auto resp = net::request_builder()
                .url("https://httpbin.org/anything")
                .method(net::methods::post)
                .header("Content-Type", "application/x-www-form-urlencoded")
                .content("hello=world&world=hello")
                .send().get();
            const auto content_j = json_parse(resp.content.as_string_view());
            REQUIRE(content_j["form"]["hello"] == "world");
            REQUIRE(content_j["form"]["world"] == "hello");
        }
    }

    SECTION("ssl_verification") {
        {
            auto req0 = net::request_builder("https://expired.badssl.com")
                .method(net::methods::head)
                .verification(true)
                .send();
            REQUIRE(req0.wait() == net::request::statuses::failed);

            auto req1 = net::request_builder("https://wrong.host.badssl.com")
                .method(net::methods::head)
                .verification(true)
                .send();
            REQUIRE(req1.wait() == net::request::statuses::failed);

            auto req2 = net::request_builder("https://self-signed.badssl.com")
                .method(net::methods::head)
                .verification(true)
                .send();
            REQUIRE(req2.wait() == net::request::statuses::failed);

            auto req3 = net::request_builder("https://untrusted-root.badssl.com")
                .method(net::methods::head)
                .verification(true)
                .send();
            REQUIRE(req3.wait() == net::request::statuses::failed);
        }
        {
            auto req0 = net::request_builder("https://expired.badssl.com")
                .method(net::methods::head)
                .verification(false)
                .send();
            REQUIRE(req0.wait() == net::request::statuses::done);

            auto req1 = net::request_builder("https://wrong.host.badssl.com")
                .method(net::methods::head)
                .verification(false)
                .send();
            REQUIRE(req1.wait() == net::request::statuses::done);

            auto req2 = net::request_builder("https://self-signed.badssl.com")
                .method(net::methods::head)
                .verification(false)
                .send();
            REQUIRE(req2.wait() == net::request::statuses::done);

            auto req3 = net::request_builder("https://untrusted-root.badssl.com")
                .method(net::methods::head)
                .verification(false)
                .send();
            REQUIRE(req3.wait() == net::request::statuses::done);
        }
    }

    SECTION("canceled_handlers") {
        {
            auto req = net::request_builder("https://httpbin.org/anything")
                .verbose(true)
                .method(net::methods::post)
                .uploader<canceled_uploader>()
                .send();
            REQUIRE(req.wait() == net::request::statuses::canceled);
        }
        {
            auto req = net::request_builder("https://httpbin.org/anything")
                .verbose(true)
                .method(net::methods::get)
                .downloader<canceled_downloader>()
                .send();
            REQUIRE(req.wait() == net::request::statuses::canceled);
        }
    }
}

TEST_CASE("curly_examples") {
    net::auto_performer performer;

    SECTION("Get Requests") {
        // makes a GET request and async send it
        auto request = net::request_builder()
            .method(net::methods::get)
            .url("http://www.httpbin.org/get")
            .send();

        // synchronous waits and get a response
        auto response = request.get();

        // prints results
        std::cout << "Status code: " << response.code() << std::endl;
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
            .method(net::methods::post)
            .url("http://www.httpbin.org/post")
            .header("Content-Type", "application/json")
            .content(R"({"hello" : "world"})")
            .send();

        auto response = request.get();
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

    SECTION("Error Handling") {
        auto request = net::request_builder()
            .url("http://unavailable.site.com")
            .send();

        if ( request.wait() == net::request::statuses::done ) {
            auto response = request.get();
            std::cout << "Status code: " << response.code() << std::endl;
        } else {
            // throws net::exception because a response is unavailable
            // auto response = request.get();

            std::cout << "Error message: " << request.error() << std::endl;
        }

        // Error message: Couldn't resolve host name
    }

    SECTION("Streamed Requests") {
        {
            class file_dowloader : public net::download_handler {
            public:
                file_dowloader(const char* filename)
                : stream_(filename, std::ofstream::binary) {}

                std::size_t write(const char* src, std::size_t size) override {
                    stream_.write(src, size);
                    return size;
                }
            private:
                std::ofstream stream_;
            };

            net::request_builder()
                .url("https://httpbin.org/image/jpeg")
                .downloader<file_dowloader>("image.jpeg")
                .send().get();
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
            private:
                std::size_t size_{0u};
                std::ifstream stream_;
            };

            net::request_builder()
                .method(net::methods::post)
                .url("https://httpbin.org/anything")
                .uploader<file_uploader>("image.jpeg")
                .send().get();
        }
    }
}
