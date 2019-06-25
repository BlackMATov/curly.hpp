/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <curly.hpp/curly.hpp>

#include <cctype>
#include <cstring>

#include <mutex>
#include <algorithm>
#include <functional>

#include <curl/curl.h>

// -----------------------------------------------------------------------------
//
// utils
//
// -----------------------------------------------------------------------------

namespace
{
    using namespace curly_hpp;

    using slist_t = std::unique_ptr<
        curl_slist,
        void(*)(curl_slist*)>;

    using handle_t = std::unique_ptr<
        CURL,
        void(*)(CURL*)>;

    using req_state_t = std::shared_ptr<request::internal_state>;
    std::map<CURL*, req_state_t> handles;

    slist_t make_header_slist(const headers_t& headers) {
        std::string header_builder;
        curl_slist* result = nullptr;
        for ( const auto& [key,value] : headers ) {
            if ( key.empty() ) {
                continue;
            }
            try {
                header_builder.clear();
                header_builder.append(key);
                if ( !value.empty() ) {
                    header_builder.append(": ");
                    header_builder.append(value);
                } else {
                    header_builder.append(";");
                }
                result = curl_slist_append(result, header_builder.c_str());
            } catch (...) {
                curl_slist_free_all(result);
                throw;
            }
        }
        return {result, &curl_slist_free_all};
    }
}

// -----------------------------------------------------------------------------
//
// curl_state
//
// -----------------------------------------------------------------------------

namespace
{
    using namespace curly_hpp;

    class curl_state {
    public:
        template < typename F >
        static std::invoke_result_t<F, CURLM*> with(F&& f) {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( !self_ ) {
                self_ = std::make_unique<curl_state>();
            }
            return std::forward<F>(f)(self_->curlm_);
        }
    public:
        curl_state() {
            if ( 0 != curl_global_init(CURL_GLOBAL_ALL) ) {
                throw exception("curly_hpp: failed to curl_global_init");
            }
            curlm_ = curl_multi_init();
            if ( !curlm_ ) {
                curl_global_cleanup();
                throw exception("curly_hpp: failed to curl_multi_init");
            }
        }

        ~curl_state() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( curlm_ ) {
                curl_multi_cleanup(curlm_);
                curl_global_cleanup();
                curlm_ = nullptr;
            }
        }
    private:
        CURLM* curlm_{nullptr};
        static std::mutex mutex_;
        static std::unique_ptr<curl_state> self_;
    };

    std::mutex curl_state::mutex_;
    std::unique_ptr<curl_state> curl_state::self_;
}

// -----------------------------------------------------------------------------
//
// exception
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    exception::exception(const char* what)
    : std::runtime_error(what) {}

    exception::exception(const std::string& what)
    : std::runtime_error(what) {}
}

// -----------------------------------------------------------------------------
//
// icase_string_compare
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    bool icase_string_compare::operator()(
        std::string_view l,
        std::string_view r) const noexcept
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(),
            [](const auto lc, const auto rc) noexcept {
                return std::tolower(lc) < std::tolower(rc);
            });
    }
}

// -----------------------------------------------------------------------------
//
// content_t
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    content_t::content_t(std::string_view data)
    : data_(data.cbegin(), data.cend() ) {}

    content_t::content_t(std::vector<char> data) noexcept
    : data_(std::move(data)) {}

    std::size_t content_t::size() const noexcept {
        return data_.size();
    }

    std::vector<char>& content_t::data() noexcept {
        return data_;
    }

    const std::vector<char>& content_t::data() const noexcept {
        return data_;
    }

    std::string content_t::as_string_copy() const {
        return {data_.data(), data_.size()};
    }

    std::string_view content_t::as_string_view() const noexcept {
        return {data_.data(), data_.size()};
    }
}

// -----------------------------------------------------------------------------
//
// response
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    response::response(response_code_t rc) noexcept
    : code_(rc) {}

    response::response(response_code_t rc, headers_t h) noexcept
    : code_(rc)
    , headers_(std::move(h)) {}

    response::response(response_code_t rc, content_t c) noexcept
    : code_(rc)
    , content_(std::move(c))  {}

    response::response(response_code_t rc, headers_t h, content_t c) noexcept
    : code_(rc)
    , headers_(std::move(h))
    , content_(std::move(c)) {}

    response_code_t response::code() const noexcept {
        return code_;
    }

    content_t& response::content() noexcept {
        return content_;
    }

    const content_t& response::content() const noexcept {
        return content_;
    }

    headers_t& response::headers() noexcept {
        return headers_;
    }

    const headers_t& response::headers() const noexcept {
        return headers_;
    }
}

// -----------------------------------------------------------------------------
//
// request
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    class request::internal_state final {
    public:
        internal_state(handle_t handle, request_builder&& rb)
        : hlist_(make_header_slist(rb.headers()))
        , handle_(std::move(handle))
        , content_(std::move(rb.content()))
        , uploader_(std::move(rb.uploader()))
        , downloader_(std::move(rb.downloader()))
        {
            if ( const auto* vi = curl_version_info(CURLVERSION_NOW); vi && vi->version ) {
                std::string user_agent("cURL/");
                user_agent.append(vi->version);
                curl_easy_setopt(handle_.get(), CURLOPT_USERAGENT, user_agent.c_str());
            }

            curl_easy_setopt(handle_.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(handle_.get(), CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(handle_.get(), CURLOPT_BUFFERSIZE, 65536L);
            curl_easy_setopt(handle_.get(), CURLOPT_USE_SSL, CURLUSESSL_ALL);

            curl_easy_setopt(handle_.get(), CURLOPT_READDATA, this);
            curl_easy_setopt(handle_.get(), CURLOPT_READFUNCTION, &s_upload_callback_);

            curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, this);
            curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, &s_download_callback_);

            curl_easy_setopt(handle_.get(), CURLOPT_HEADERDATA, this);
            curl_easy_setopt(handle_.get(), CURLOPT_HEADERFUNCTION, &s_header_callback_);

            curl_easy_setopt(handle_.get(), CURLOPT_URL, rb.url().c_str());
            curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, hlist_.get());
            curl_easy_setopt(handle_.get(), CURLOPT_VERBOSE, rb.verbose() ? 1L : 0L);

            switch ( rb.method() ) {
            case methods::put:
                curl_easy_setopt(handle_.get(), CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(handle_.get(), CURLOPT_INFILESIZE_LARGE, uploader_
                    ? static_cast<curl_off_t>(uploader_->size())
                    : static_cast<curl_off_t>(content_.size()));
                break;
            case methods::get:
                curl_easy_setopt(handle_.get(), CURLOPT_HTTPGET, 1L);
                break;
            case methods::head:
                curl_easy_setopt(handle_.get(), CURLOPT_NOBODY, 1L);
                break;
            case methods::post:
                curl_easy_setopt(handle_.get(), CURLOPT_POST, 1L);
                curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDSIZE_LARGE, uploader_
                    ? static_cast<curl_off_t>(uploader_->size())
                    : static_cast<curl_off_t>(content_.size()));
                break;
            default:
                throw exception("curly_hpp: unexpected request method");
            }

            if ( rb.verification() ) {
                curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            } else {
                curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 0L);
            }

            if ( rb.redirections() ) {
                curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(handle_.get(), CURLOPT_MAXREDIRS, rb.redirections());
            } else {
                curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 0L);
            }

            curl_easy_setopt(handle_.get(), CURLOPT_CONNECTTIMEOUT,
                std::max(time_sec_t(1), rb.connection_timeout()).count());

            last_response_ = time_point_t::clock::now();
            response_timeout_ = std::max(time_sec_t(1), rb.response_timeout());
        }

        bool done() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            long response_code = 0;
            curl_easy_getinfo(
                handle_.get(),
                CURLINFO_RESPONSE_CODE,
                &response_code);

            response_ = response(
                static_cast<response_code_t>(response_code),
                std::move(response_headers_),
                std::move(response_content_));

            status_ = statuses::done;
            error_.clear();

            cond_var_.notify_all();
            return true;
        }

        bool fail(CURLcode err) noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            status_ = (err == CURLE_OPERATION_TIMEDOUT)
                ? statuses::timeout
                : statuses::failed;

            try {
                error_ = curl_easy_strerror(err);
            } catch (...) {
                // nothing
            }

            cond_var_.notify_all();
            return true;
        }

        bool cancel() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            status_ = statuses::canceled;
            error_.clear();

            cond_var_.notify_all();
            return true;
        }

        statuses wait() const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_var_.wait(lock, [this](){
                return status_ != statuses::pending;
            });
            return status_;
        }

        statuses status() const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return status_;
        }

        const response& get() const {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_var_.wait(lock, [this](){
                return status_ != statuses::pending;
            });
            if ( status_ != statuses::done ) {
                throw exception("curly_hpp: response is unavailable");
            }
            return response_;
        }

        const std::string& error() const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_var_.wait(lock, [this](){
                return status_ != statuses::pending;
            });
            return error_;
        }

        const handle_t& handle() const noexcept {
            return handle_;
        }

        bool check_response_timeout(time_point_t now) const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return now - last_response_ >= response_timeout_;
        }
    private:
        static std::size_t s_upload_callback_(
            char* buffer, std::size_t size, std::size_t nitems, void* userdata) noexcept
        {
            auto* self = static_cast<internal_state*>(userdata);
            return self->upload_callback_(buffer, size * nitems);
        }

        static std::size_t s_download_callback_(
            char* ptr, std::size_t size, std::size_t nmemb, void* userdata) noexcept
        {
            auto* self = static_cast<internal_state*>(userdata);
            return self->download_callback_(ptr, size * nmemb);
        }

        static std::size_t s_header_callback_(
            char* buffer, std::size_t size, std::size_t nitems, void* userdata) noexcept
        {
            auto* self = static_cast<internal_state*>(userdata);
            return self->header_callback_(buffer, size * nitems);
        }
    private:
        void response_callback_() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            last_response_ = time_point_t::clock::now();
        }

        std::size_t upload_callback_(char* dst, std::size_t size) noexcept {
            try {
                if ( uploader_ ) {
                    std::size_t bytes = uploader_->upload(dst, size);
                    uploaded_.fetch_add(bytes);
                    return bytes;
                }
                std::lock_guard<std::mutex> guard(mutex_);
                size = std::min(size, content_.size() - uploaded_.load());
                std::memcpy(dst, content_.data().data() + uploaded_.load(), size);
                uploaded_.fetch_add(size);
                return size;
            } catch (...) {
                return CURL_READFUNC_ABORT;
            }
        }

        std::size_t download_callback_(const char* src, std::size_t size) noexcept {
            try {
                if ( downloader_ ) {
                    std::size_t bytes = downloader_->download(src, size);
                    downloaded_.fetch_add(bytes);
                    return bytes;
                }
                response_content_.insert(response_content_.end(), src, src + size);
                downloaded_.fetch_add(size);
                return size;
            } catch (...) {
                return 0u;
            }
        }

        std::size_t header_callback_(const char* src, std::size_t size) noexcept {
            try {
                const std::string_view header(src, size);
                if ( !header.compare(0u, 5u, "HTTP/") ) {
                    std::lock_guard<std::mutex> guard(mutex_);
                    response_headers_.clear();
                } else if ( const auto sep_idx = header.find(':'); sep_idx != std::string_view::npos ) {
                    if ( const auto key = header.substr(0, sep_idx); !key.empty() ) {
                        auto val = header.substr(sep_idx + 1);
                        const auto val_f = val.find_first_not_of("\t ");
                        const auto val_l = val.find_last_not_of("\r\n\t ");
                        val = (val_f != std::string_view::npos && val_l != std::string_view::npos)
                            ? val.substr(val_f, val_l)
                            : std::string_view();
                        std::lock_guard<std::mutex> guard(mutex_);
                        response_headers_.emplace(key, val);
                    }
                }
                return header.size();
            } catch (...) {
                return 0;
            }
        }
    private:
        slist_t hlist_;
        handle_t handle_;
        content_t content_;
        uploader_uptr uploader_;
        downloader_uptr downloader_;
    private:
        time_point_t last_response_;
        time_point_t::duration response_timeout_;
    private:
        response response_;
        headers_t response_headers_;
        std::vector<char> response_content_;
    private:
        std::atomic_size_t uploaded_{0u};
        std::atomic_size_t downloaded_{0u};
        statuses status_{statuses::pending};
        std::string error_{"Unknown error"};
    private:
        mutable std::mutex mutex_;
        mutable std::condition_variable cond_var_;
    };
}

namespace curly_hpp
{
    request::request(internal_state_ptr state)
    : state_(state) {}

    bool request::cancel() noexcept {
        return state_->cancel();
    }

    request::statuses request::wait() const noexcept {
        return state_->wait();
    }

    request::statuses request::status() const noexcept {
        return state_->status();
    }

    const response& request::get() const {
        return state_->get();
    }

    const std::string& request::error() const noexcept {
        return state_->error();
    }
}

// -----------------------------------------------------------------------------
//
// request_builder
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    request_builder::request_builder(methods m) noexcept
    : method_(m) {}

    request_builder::request_builder(std::string u) noexcept
    : url_(std::move(u)) {}

    request_builder::request_builder(methods m, std::string u) noexcept
    : url_(std::move(u))
    , method_(m) {}

    request_builder& request_builder::url(std::string u) noexcept {
        url_ = std::move(u);
        return *this;
    }

    request_builder& request_builder::method(methods m) noexcept {
        method_ = m;
        return *this;
    }

    request_builder& request_builder::header(std::string key, std::string value) {
        headers_.insert_or_assign(std::move(key), std::move(value));
        return *this;
    }

    request_builder& request_builder::verbose(bool v) noexcept {
        verbose_ = v;
        return *this;
    }

    request_builder& request_builder::verification(bool v) noexcept {
        verification_ = v;
        return *this;
    }

    request_builder& request_builder::redirections(std::uint32_t r) noexcept {
        redirections_ = r;
        return *this;
    }

    request_builder& request_builder::response_timeout(time_sec_t t) noexcept {
        response_timeout_ = t;
        return *this;
    }

    request_builder& request_builder::connection_timeout(time_sec_t t) noexcept {
        connection_timeout_ = t;
        return *this;
    }

    request_builder& request_builder::content(std::string_view c) {
        content_ = content_t(c);
        return *this;
    }

    request_builder& request_builder::content(content_t c) noexcept {
        content_ = std::move(c);
        return *this;
    }

    request_builder& request_builder::uploader(uploader_uptr u) noexcept {
        uploader_ = std::move(u);
        return *this;
    }

    request_builder& request_builder::downloader(downloader_uptr d) noexcept {
        downloader_ = std::move(d);
        return *this;
    }

    const std::string& request_builder::url() const noexcept {
        return url_;
    }

    methods request_builder::method() const noexcept {
        return method_;
    }

    const headers_t& request_builder::headers() const noexcept {
        return headers_;
    }

    bool request_builder::verbose() const noexcept {
        return verbose_;
    }

    bool request_builder::verification() const noexcept {
        return verification_;
    }

    std::uint32_t request_builder::redirections() const noexcept {
        return redirections_;
    }

    time_sec_t request_builder::response_timeout() const noexcept {
        return response_timeout_;
    }

    time_sec_t request_builder::connection_timeout() const noexcept {
        return connection_timeout_;
    }

    content_t& request_builder::content() noexcept {
        return content_;
    }

    const content_t& request_builder::content() const noexcept {
        return content_;
    }

    uploader_uptr& request_builder::uploader() noexcept {
        return uploader_;
    }

    const uploader_uptr& request_builder::uploader() const noexcept {
        return uploader_;
    }

    downloader_uptr& request_builder::downloader() noexcept {
        return downloader_;
    }

    const downloader_uptr& request_builder::downloader() const noexcept {
        return downloader_;
    }
}

// -----------------------------------------------------------------------------
//
// auto_updater
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    auto_updater::auto_updater() {
        thread_ = std::thread([this](){
            while ( !done_ ) {
                ::update();
                ::wait_activity(wait_activity());
            }
        });
    }

    auto_updater::~auto_updater() noexcept {
        done_.store(true);
        if ( thread_.joinable() ) {
            thread_.join();
        }
    }

    time_ms_t auto_updater::wait_activity() const noexcept {
        return wait_activity_;
    }

    void auto_updater::wait_activity(time_ms_t ms) noexcept {
        wait_activity_ = ms;
    }
}

// -----------------------------------------------------------------------------
//
// perform
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    void update() {
        curl_state::with([](CURLM* curlm){
            int running_handles = 0;
            curl_multi_perform(curlm, &running_handles);
            if ( !running_handles || static_cast<std::size_t>(running_handles) != handles.size() ) {
                while ( true ) {
                    int msgs_in_queue = 0;
                    CURLMsg* msg = curl_multi_info_read(curlm, &msgs_in_queue);
                    if ( !msg ) {
                        break;
                    }
                    if ( msg->msg == CURLMSG_DONE ) {
                        const auto iter = handles.find(msg->easy_handle);
                        if ( iter != handles.end() ) {
                            if ( msg->data.result == CURLE_OK ) {
                                iter->second->done();
                            } else {
                                iter->second->fail(msg->data.result);
                            }
                        }
                    }
                }
            }
            const auto now = time_point_t::clock::now();
            for ( const auto& [curl, sreq] : handles ) {
                if ( sreq->check_response_timeout(now) ) {
                    sreq->fail(CURLE_OPERATION_TIMEDOUT);
                }
            }
            for ( auto iter = handles.begin(); iter != handles.end(); ) {
                if ( iter->second->status() != request::statuses::pending ) {
                    curl_multi_remove_handle(curlm, iter->first);
                    iter = handles.erase(iter);
                } else {
                    ++iter;
                }
            }
        });
    }

    void wait_activity(time_ms_t ms) {
        curl_state::with([ms](CURLM* curlm){
            curl_multi_wait(curlm, nullptr, 0, static_cast<int>(ms.count()), nullptr);
        });
    }

    request perform(request_builder& rb) {
        return perform(std::move(rb));
    }

    request perform(request_builder&& rb) {
        return curl_state::with([&rb](CURLM* curlm){
            handle_t handle{curl_easy_init(), &curl_easy_cleanup};
            if ( !handle ) {
                throw exception("curly_hpp: failed to curl_easy_init");
            }
            auto sreq = std::make_shared<request::internal_state>(std::move(handle), std::move(rb));
            curl_multi_add_handle(curlm, sreq->handle().get());
            try {
                handles.emplace(sreq->handle().get(), sreq);
            } catch (...) {
                curl_multi_remove_handle(curlm, sreq->handle().get());
                throw;
            }
            return request(sreq);
        });
    }
}
