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

    struct case_insensitive_compare {
        bool operator()(const std::string& l, const std::string& r) const noexcept;
    };

    enum class methods {
        put,
        get,
        head,
        post
    };

    using headers_t = std::map<
        std::string, std::string,
        case_insensitive_compare>;

    using body_t = std::vector<char>;
    using response_code_t = std::uint16_t;

    using sec_t = std::chrono::seconds;
    using ms_t = std::chrono::milliseconds;

    class upload_handler {
    public:
        virtual ~upload_handler() {}
        virtual std::size_t size() const = 0;
        virtual std::size_t upload(void* dst, std::size_t size) = 0;
    };

    class download_handler {
    public:
        virtual ~download_handler() {}
        virtual std::size_t download(const void* src, std::size_t size) = 0;
    };

    using uploader_uptr = std::unique_ptr<upload_handler>;
    using downloader_uptr = std::unique_ptr<download_handler>;

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
        request_builder& response_timeout(sec_t t) noexcept;
        request_builder& connection_timeout(sec_t t) noexcept;
        request_builder& redirections(std::uint32_t r) noexcept;

        request_builder& body(body_t b) noexcept;
        request_builder& body(std::string_view b);
        request_builder& uploader(uploader_uptr u) noexcept;
        request_builder& downloader(downloader_uptr d) noexcept;

        [[nodiscard]] const std::string& url() const noexcept;
        [[nodiscard]] methods method() const noexcept;
        [[nodiscard]] const headers_t& headers() const noexcept;

        [[nodiscard]] bool verbose() const noexcept;
        [[nodiscard]] bool verification() const noexcept;
        [[nodiscard]] sec_t response_timeout() const noexcept;
        [[nodiscard]] sec_t connection_timeout() const noexcept;
        [[nodiscard]] std::uint32_t redirections() const noexcept;

        [[nodiscard]] body_t& body() noexcept;
        [[nodiscard]] const body_t& body() const noexcept;

        [[nodiscard]] uploader_uptr& uploader() noexcept;
        [[nodiscard]] const uploader_uptr& uploader() const noexcept;

        [[nodiscard]] downloader_uptr& downloader() noexcept;
        [[nodiscard]] const downloader_uptr& downloader() const noexcept;
    private:
        std::string url_;
        methods method_{methods::get};
        headers_t headers_;
        bool verbose_{false};
        bool verification_{true};
        sec_t response_timeout_{60u};
        sec_t connection_timeout_{20u};
        std::uint32_t redirections_{~0u};
    private:
        body_t body_;
        uploader_uptr uploader_;
        downloader_uptr downloader_;
    };

    class response {
    public:
        response() = default;
        response(response_code_t c, body_t b, headers_t h);

        response_code_t code() const noexcept;
        const body_t& body() const noexcept;
        const headers_t& headers() const noexcept;
        std::string_view body_as_string() const noexcept;
    private:
        response_code_t code_{0u};
        body_t body_;
        headers_t headers_;
    };

    class request {
    public:
        enum class statuses {
            done,
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

        const response& get() const;
        const std::string& error() const noexcept;
    private:
        internal_state_ptr state_;
    };

    class auto_updater {
    public:
        auto_updater();
        ~auto_updater() noexcept;

        ms_t wait_activity() const noexcept;
        void wait_activity(ms_t ms) noexcept;
    private:
        std::thread thread_;
        ms_t wait_activity_{100};
        std::atomic<bool> done_{false};
    };

    void update();
    void wait_activity(ms_t ms);

    request perform(request_builder& rb);
    request perform(request_builder&& rb);
}
