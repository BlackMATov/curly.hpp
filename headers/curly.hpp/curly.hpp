/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#pragma once

#include <cctype>
#include <cassert>
#include <cstring>

#include <cstdint>
#include <cstddef>

#include <atomic>
#include <chrono>
#include <thread>

#include <memory>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <functional>

#include <map>
#include <vector>
#include <string>

namespace curly_hpp
{
    class request;
    class response;
    class request_builder;

    using http_code_t = std::uint16_t;

    using time_sec_t = std::chrono::seconds;
    using time_ms_t = std::chrono::milliseconds;
    using time_point_t = std::chrono::steady_clock::time_point;

    enum class http_method {
        PUT,
        GET,
        HEAD,
        POST
    };

    class upload_handler {
    public:
        virtual ~upload_handler() {}
        virtual std::size_t size() const = 0;
        virtual std::size_t read(char* dst, std::size_t size) = 0;
    };

    class download_handler {
    public:
        virtual ~download_handler() {}
        virtual std::size_t write(const char* src, std::size_t size) = 0;
    };

    using callback_t = std::function<void(request)>;
    using uploader_uptr = std::unique_ptr<upload_handler>;
    using downloader_uptr = std::unique_ptr<download_handler>;
}

namespace curly_hpp
{
    class exception final : public std::runtime_error {
    public:
        explicit exception(const char* what)
        : std::runtime_error(what) {}

        explicit exception(const std::string& what)
        : std::runtime_error(what) {}
    };
}

namespace curly_hpp
{
    namespace detail
    {
        struct icase_string_compare final {
            using is_transparent = void;
            bool operator()(std::string_view l, std::string_view r) const noexcept {
                return std::lexicographical_compare(
                    l.begin(), l.end(), r.begin(), r.end(),
                    [](const char lc, const char rc) noexcept {
                        return std::tolower(lc) < std::tolower(rc);
                    });
            }
        };
    }

    using headers_t = std::map<
        std::string, std::string,
        detail::icase_string_compare>;
}

namespace curly_hpp
{
    class content_t final {
    public:
        content_t() = default;

        content_t(content_t&&) = default;
        content_t& operator=(content_t&&) = default;

        content_t(const content_t&) = default;
        content_t& operator=(const content_t&) = default;

        content_t(std::string_view data)
        : data_(data.cbegin(), data.cend() ) {}

        content_t(std::vector<char> data) noexcept
        : data_(std::move(data)) {}

        std::size_t size() const noexcept {
            return data_.size();
        }

        std::vector<char>& data() noexcept {
            return data_;
        }

        const std::vector<char>& data() const noexcept {
            return data_;
        }

        std::string as_string_copy() const {
            return {data_.data(), data_.size()};
        }

        std::string_view as_string_view() const noexcept {
            return {data_.data(), data_.size()};
        }
    private:
        std::vector<char> data_;
    };
}

namespace curly_hpp
{
    class response final {
    public:
        response() = default;

        response(response&&) = default;
        response& operator=(response&&) = default;

        response(const response&) = delete;
        response& operator=(const response&) = delete;

        explicit response(http_code_t c) noexcept
        : http_code_(c) {}

        bool is_http_error() const noexcept {
            return http_code_ >= 400u;
        }

        http_code_t http_code() const noexcept {
            return http_code_;
        }
    public:
        content_t content;
        headers_t headers;
        uploader_uptr uploader;
        downloader_uptr downloader;
    private:
        http_code_t http_code_{0u};
    };
}

namespace curly_hpp
{
    enum class req_status {
        done,
        empty,
        failed,
        timeout,
        pending,
        canceled
    };

    class request final {
    public:
        class internal_state;
        using internal_state_ptr = std::shared_ptr<internal_state>;
    public:
        request(internal_state_ptr);

        bool cancel() noexcept;
        req_status status() const noexcept;

        bool is_done() const noexcept;
        bool is_pending() const noexcept;

        req_status wait() const noexcept;
        req_status wait_for(time_ms_t ms) const noexcept;
        req_status wait_until(time_point_t tp) const noexcept;

        req_status wait_callback() const noexcept;
        req_status wait_callback_for(time_ms_t ms) const noexcept;
        req_status wait_callback_until(time_point_t tp) const noexcept;

        response take();
        const std::string& get_error() const noexcept;
        std::exception_ptr get_callback_exception() const noexcept;
    private:
        internal_state_ptr state_;
    };
}

namespace curly_hpp
{
    class request_builder final {
    public:
        request_builder() = default;

        request_builder(request_builder&&) = default;
        request_builder& operator=(request_builder&&) = default;

        request_builder(const request_builder&) = delete;
        request_builder& operator=(const request_builder&) = delete;

        explicit request_builder(http_method m) noexcept;
        explicit request_builder(std::string u) noexcept;
        explicit request_builder(http_method m, std::string u) noexcept;

        request_builder& url(std::string u) noexcept;
        request_builder& method(http_method m) noexcept;
        request_builder& header(std::string key, std::string value);

        request_builder& verbose(bool v) noexcept;
        request_builder& verification(bool v) noexcept;
        request_builder& redirections(std::uint32_t r) noexcept;
        request_builder& request_timeout(time_ms_t t) noexcept;
        request_builder& response_timeout(time_ms_t t) noexcept;
        request_builder& connection_timeout(time_ms_t t) noexcept;

        request_builder& content(std::string_view b);
        request_builder& content(content_t b) noexcept;
        request_builder& callback(callback_t c) noexcept;
        request_builder& uploader(uploader_uptr u) noexcept;
        request_builder& downloader(downloader_uptr d) noexcept;

        const std::string& url() const noexcept;
        http_method method() const noexcept;
        const headers_t& headers() const noexcept;

        bool verbose() const noexcept;
        bool verification() const noexcept;
        std::uint32_t redirections() const noexcept;
        time_ms_t request_timeout() const noexcept;
        time_ms_t response_timeout() const noexcept;
        time_ms_t connection_timeout() const noexcept;

        content_t& content() noexcept;
        const content_t& content() const noexcept;

        callback_t& callback() noexcept;
        const callback_t& callback() const noexcept;

        uploader_uptr& uploader() noexcept;
        const uploader_uptr& uploader() const noexcept;

        downloader_uptr& downloader() noexcept;
        const downloader_uptr& downloader() const noexcept;

        request send();

        template < typename Callback >
        request_builder& callback(Callback&& f) {
            static_assert(std::is_convertible_v<Callback, callback_t>);
            return callback(callback_t(std::forward<Callback>(f)));
        }

        template < typename Uploader, typename... Args >
        request_builder& uploader(Args&&... args) {
            static_assert(std::is_base_of_v<upload_handler, Uploader>);
            return uploader(std::make_unique<Uploader>(std::forward<Args>(args)...));
        }

        template < typename Downloader, typename... Args >
        request_builder& downloader(Args&&... args) {
            static_assert(std::is_base_of_v<download_handler, Downloader>);
            return downloader(std::make_unique<Downloader>(std::forward<Args>(args)...));
        }
    private:
        std::string url_;
        http_method method_{http_method::GET};
        headers_t headers_;
        bool verbose_{false};
        bool verification_{false};
        std::uint32_t redirections_{10u};
        time_ms_t request_timeout_{time_sec_t{~0u}};
        time_ms_t response_timeout_{time_sec_t{60u}};
        time_ms_t connection_timeout_{time_sec_t{20u}};
    private:
        content_t content_;
        callback_t callback_;
        uploader_uptr uploader_;
        downloader_uptr downloader_;
    };
}

namespace curly_hpp
{
    class performer final {
    public:
        performer();
        ~performer() noexcept;

        time_ms_t wait_activity() const noexcept;
        void wait_activity(time_ms_t ms) noexcept;
    private:
        std::thread thread_;
        time_ms_t wait_activity_{100};
        std::atomic<bool> done_{false};
    };
}

namespace curly_hpp
{
    void perform();
    void wait_activity(time_ms_t ms);
}
