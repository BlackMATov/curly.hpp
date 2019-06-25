/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <cstddef>

#include <atomic>
#include <thread>

#include <map>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace curly_hpp
{
    class exception : public std::runtime_error {
    public:
        explicit exception(const char* what);
        explicit exception(const std::string& what);
    };

    struct icase_string_compare {
        using is_transparent = void;
        bool operator()(
            std::string_view l,
            std::string_view r) const noexcept;
    };

    enum class methods {
        put,
        get,
        head,
        post
    };

    using response_code_t = std::uint16_t;
    using headers_t = std::map<std::string, std::string, icase_string_compare>;

    using time_sec_t = std::chrono::seconds;
    using time_ms_t = std::chrono::milliseconds;
    using time_point_t = std::chrono::steady_clock::time_point;

    class upload_handler {
    public:
        virtual ~upload_handler() {}
        virtual std::size_t size() const = 0;
        virtual std::size_t upload(char* dst, std::size_t size) = 0;
    };

    class download_handler {
    public:
        virtual ~download_handler() {}
        virtual std::size_t download(const char* src, std::size_t size) = 0;
    };

    using uploader_uptr = std::unique_ptr<upload_handler>;
    using downloader_uptr = std::unique_ptr<download_handler>;
}

namespace curly_hpp
{
    class content_t {
    public:
        content_t() = default;

        content_t(content_t&&) = default;
        content_t& operator=(content_t&&) = default;

        content_t(const content_t&) = default;
        content_t& operator=(const content_t&) = default;

        content_t(std::string_view data);
        content_t(std::vector<char> data) noexcept;

        std::size_t size() const noexcept;
        std::vector<char>& data() noexcept;
        const std::vector<char>& data() const noexcept;

        std::string as_string_copy() const;
        std::string_view as_string_view() const noexcept;
    private:
        std::vector<char> data_;
    };
}

namespace curly_hpp
{
    class response {
    public:
        response() = default;

        response(response&&) = default;
        response& operator=(response&&) = default;

        response(const response&) = delete;
        response& operator=(const response&) = delete;

        explicit response(response_code_t rc) noexcept;
        response(response_code_t rc, headers_t h) noexcept;
        response(response_code_t rc, content_t c) noexcept;
        response(response_code_t rc, headers_t h, content_t c) noexcept;

        response_code_t code() const noexcept;

        content_t& content() noexcept;
        const content_t& content() const noexcept;

        headers_t& headers() noexcept;
        const headers_t& headers() const noexcept;

        uploader_uptr& uploader() noexcept;
        const uploader_uptr& uploader() const noexcept;

        downloader_uptr& downloader() noexcept;
        const downloader_uptr& downloader() const noexcept;
    private:
        response_code_t code_{0u};
        content_t content_;
        headers_t headers_;
        uploader_uptr uploader_;
        downloader_uptr downloader_;
    };
}

namespace curly_hpp
{
    class request {
    public:
        enum class statuses {
            done,
            empty,
            failed,
            timeout,
            pending,
            canceled
        };
    public:
        class internal_state;
        using internal_state_ptr = std::shared_ptr<internal_state>;
    public:
        request(internal_state_ptr);

        bool cancel() noexcept;
        statuses wait() const noexcept;
        statuses status() const noexcept;

        response get();
        const std::string& error() const noexcept;
    private:
        internal_state_ptr state_;
    };
}

namespace curly_hpp
{
    class request_builder {
    public:
        request_builder() = default;

        request_builder(request_builder&&) = default;
        request_builder& operator=(request_builder&&) = default;

        request_builder(const request_builder&) = delete;
        request_builder& operator=(const request_builder&) = delete;

        explicit request_builder(methods m) noexcept;
        explicit request_builder(std::string u) noexcept;
        explicit request_builder(methods m, std::string u) noexcept;

        request_builder& url(std::string u) noexcept;
        request_builder& method(methods m) noexcept;
        request_builder& header(std::string key, std::string value);

        request_builder& verbose(bool v) noexcept;
        request_builder& verification(bool v) noexcept;
        request_builder& redirections(std::uint32_t r) noexcept;
        request_builder& response_timeout(time_sec_t t) noexcept;
        request_builder& connection_timeout(time_sec_t t) noexcept;

        request_builder& content(std::string_view b);
        request_builder& content(content_t b) noexcept;
        request_builder& uploader(uploader_uptr u) noexcept;
        request_builder& downloader(downloader_uptr d) noexcept;

        const std::string& url() const noexcept;
        methods method() const noexcept;
        const headers_t& headers() const noexcept;

        bool verbose() const noexcept;
        bool verification() const noexcept;
        std::uint32_t redirections() const noexcept;
        time_sec_t response_timeout() const noexcept;
        time_sec_t connection_timeout() const noexcept;

        content_t& content() noexcept;
        const content_t& content() const noexcept;

        uploader_uptr& uploader() noexcept;
        const uploader_uptr& uploader() const noexcept;

        downloader_uptr& downloader() noexcept;
        const downloader_uptr& downloader() const noexcept;

        request perform();

        template < typename Uploader, typename... Args >
        request_builder& uploader(Args&&... args) {
            return uploader(std::make_unique<Uploader>(std::forward<Args>(args)...));
        }

        template < typename Downloader, typename... Args >
        request_builder& downloader(Args&&... args) {
            return downloader(std::make_unique<Downloader>(std::forward<Args>(args)...));
        }
    private:
        std::string url_;
        methods method_{methods::get};
        headers_t headers_;
        bool verbose_{false};
        bool verification_{true};
        std::uint32_t redirections_{~0u};
        time_sec_t response_timeout_{60u};
        time_sec_t connection_timeout_{20u};
    private:
        content_t content_;
        uploader_uptr uploader_;
        downloader_uptr downloader_;
    };
}

namespace curly_hpp
{
    class auto_updater {
    public:
        auto_updater();
        ~auto_updater() noexcept;

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
    void update();
    void wait_activity(time_ms_t ms);

    request perform(request_builder& rb);
    request perform(request_builder&& rb);
}
