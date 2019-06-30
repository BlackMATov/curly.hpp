/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <curly.hpp/curly.hpp>

#include <cctype>
#include <cassert>
#include <cstring>

#include <mutex>
#include <queue>
#include <algorithm>
#include <condition_variable>

#ifndef NOMINMAX
#  define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <curl/curl.h>

// -----------------------------------------------------------------------------
//
// types
//
// -----------------------------------------------------------------------------

namespace
{
    using namespace curly_hpp;

    using curlh_t = std::unique_ptr<
        CURL,
        void(*)(CURL*)>;

    using slist_t = std::unique_ptr<
        curl_slist,
        void(*)(curl_slist*)>;

    class default_uploader final : public upload_handler {
    public:
        using data_t = std::vector<char>;

        default_uploader(const data_t* src, std::mutex* m) noexcept
        : data_(*src)
        , mutex_(*m)
        , size_(src->size()) {}

        std::size_t size() const override {
            return size_.load();
        }

        std::size_t read(char* dst, std::size_t size) override {
            std::lock_guard<std::mutex> guard(mutex_);
            assert(size <= data_.size() - uploaded_);
            std::memcpy(dst, data_.data() + uploaded_, size);
            uploaded_ += size;
            return size;
        }
    private:
        const data_t& data_;
        std::mutex& mutex_;
        std::size_t uploaded_{0};
        const std::atomic_size_t size_{0};
    };

    class default_downloader final : public download_handler {
    public:
        using data_t = std::vector<char>;

        default_downloader(data_t* dst, std::mutex* m) noexcept
        : data_(*dst)
        , mutex_(*m) {}

        std::size_t write(const char* src, std::size_t size) override {
            std::lock_guard<std::mutex> guard(mutex_);
            data_.insert(data_.end(), src, src + size);
            return size;
        }
    private:
        data_t& data_;
        std::mutex& mutex_;
    };
}

// -----------------------------------------------------------------------------
//
// utils
//
// -----------------------------------------------------------------------------

namespace
{
    using namespace curly_hpp;

    template < typename T >
    class mt_queue final {
    public:
        void enqueue(T v) {
            std::lock_guard<std::mutex> guard(mutex_);
            queue_.push(std::move(v));
            cvar_.notify_all();
        }

        bool try_dequeue(T& v) {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( queue_.empty() ) {
                return false;
            }
            v = queue_.front();
            queue_.pop();
            return true;
        }

        bool empty() const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return queue_.empty();
        }

        bool wait_content_for(time_ms_t ms) const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            return cvar_.wait_for(lock, ms, [this](){
                return !queue_.empty();
            });
        }
    private:
        std::queue<T> queue_;
    private:
        mutable std::mutex mutex_;
        mutable std::condition_variable cvar_;
    };

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
                curl_slist* tmp_result = curl_slist_append(result, header_builder.c_str());
                if ( !tmp_result ) {
                    throw exception("curly_hpp: failed to curl_slist_append");
                }
                result = tmp_result;
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
// state
//
// -----------------------------------------------------------------------------

namespace
{
    using namespace curly_hpp;

    using req_state_t = std::shared_ptr<request::internal_state>;
    std::vector<req_state_t> active_handles;
    mt_queue<req_state_t> new_handles;

    class curl_state final {
    public:
        template < typename F >
        static std::invoke_result_t<F, CURLM*> with(F&& f)
            noexcept(std::is_nothrow_invocable_v<F, CURLM*>)
        {
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
            curl_multi_cleanup(curlm_);
            curl_global_cleanup();
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
    response::response(http_code_t c) noexcept
    : http_code_(c) {}

    bool response::is_http_error() const noexcept {
        return http_code_ >= 400u;
    }

    http_code_t response::http_code() const noexcept {
        return http_code_;
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
        internal_state(request_builder&& rb)
        : breq_(std::move(rb))
        {
            if ( !breq_.uploader() ) {
                breq_.uploader<default_uploader>(&breq_.content().data(), &mutex_);
            }

            if ( !breq_.downloader() ) {
                breq_.downloader<default_downloader>(&response_content_, &mutex_);
            }
        }

        void enqueue(CURLM* curlm) {
            std::lock_guard<std::mutex> guard(mutex_);

            assert(!curlh_);
            curlh_ = curlh_t{
                curl_easy_init(),
                &curl_easy_cleanup};

            if ( !curlh_ ) {
                throw exception("curly_hpp: failed to curl_easy_init");
            }

            hlist_ = make_header_slist(breq_.headers());

            if ( const auto* vi = curl_version_info(CURLVERSION_NOW); vi && vi->version ) {
                std::string user_agent("cURL/");
                user_agent.append(vi->version);
                curl_easy_setopt(curlh_.get(), CURLOPT_USERAGENT, user_agent.c_str());
            }

            curl_easy_setopt(curlh_.get(), CURLOPT_NOSIGNAL, 1l);
            curl_easy_setopt(curlh_.get(), CURLOPT_PRIVATE, this);
            curl_easy_setopt(curlh_.get(), CURLOPT_TCP_KEEPALIVE, 1l);
            curl_easy_setopt(curlh_.get(), CURLOPT_BUFFERSIZE, 65536l);
            curl_easy_setopt(curlh_.get(), CURLOPT_USE_SSL, CURLUSESSL_ALL);

            curl_easy_setopt(curlh_.get(), CURLOPT_READDATA, this);
            curl_easy_setopt(curlh_.get(), CURLOPT_READFUNCTION, &s_upload_callback_);

            curl_easy_setopt(curlh_.get(), CURLOPT_WRITEDATA, this);
            curl_easy_setopt(curlh_.get(), CURLOPT_WRITEFUNCTION, &s_download_callback_);

            curl_easy_setopt(curlh_.get(), CURLOPT_HEADERDATA, this);
            curl_easy_setopt(curlh_.get(), CURLOPT_HEADERFUNCTION, &s_header_callback_);

            curl_easy_setopt(curlh_.get(), CURLOPT_URL, breq_.url().c_str());
            curl_easy_setopt(curlh_.get(), CURLOPT_HTTPHEADER, hlist_.get());
            curl_easy_setopt(curlh_.get(), CURLOPT_VERBOSE, breq_.verbose() ? 1l : 0l);

            switch ( breq_.method() ) {
            case methods::put:
                curl_easy_setopt(curlh_.get(), CURLOPT_UPLOAD, 1l);
                curl_easy_setopt(curlh_.get(), CURLOPT_INFILESIZE_LARGE,
                    static_cast<curl_off_t>(breq_.uploader()->size()));
                break;
            case methods::get:
                curl_easy_setopt(curlh_.get(), CURLOPT_HTTPGET, 1l);
                break;
            case methods::head:
                curl_easy_setopt(curlh_.get(), CURLOPT_NOBODY, 1l);
                break;
            case methods::post:
                curl_easy_setopt(curlh_.get(), CURLOPT_POST, 1l);
                curl_easy_setopt(curlh_.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                    static_cast<curl_off_t>(breq_.uploader()->size()));
                break;
            default:
                throw exception("curly_hpp: unexpected request method");
            }

            if ( breq_.verification() ) {
                curl_easy_setopt(curlh_.get(), CURLOPT_SSL_VERIFYPEER, 1l);
                curl_easy_setopt(curlh_.get(), CURLOPT_SSL_VERIFYHOST, 2l);
            } else {
                curl_easy_setopt(curlh_.get(), CURLOPT_SSL_VERIFYPEER, 0l);
                curl_easy_setopt(curlh_.get(), CURLOPT_SSL_VERIFYHOST, 0l);
            }

            if ( breq_.redirections() ) {
                curl_easy_setopt(curlh_.get(), CURLOPT_FOLLOWLOCATION, 1l);
                curl_easy_setopt(curlh_.get(), CURLOPT_MAXREDIRS,
                    static_cast<long>(breq_.redirections()));
            } else {
                curl_easy_setopt(curlh_.get(), CURLOPT_FOLLOWLOCATION, 0l);
            }

            curl_easy_setopt(curlh_.get(), CURLOPT_TIMEOUT,
                static_cast<long>(std::max(time_sec_t(1), breq_.request_timeout()).count()));

            curl_easy_setopt(curlh_.get(), CURLOPT_CONNECTTIMEOUT,
                static_cast<long>(std::max(time_sec_t(1), breq_.connection_timeout()).count()));

            last_response_ = time_point_t::clock::now();
            response_timeout_ = std::max(time_sec_t(1), breq_.response_timeout());

            if ( CURLM_OK != curl_multi_add_handle(curlm, curlh_.get()) ) {
                throw exception("curly_hpp: failed to curl_multi_add_handle");
            }
        }

        void dequeue(CURLM* curlm) noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( curlh_ ) {
                curl_multi_remove_handle(curlm, curlh_.get());
                curlh_.reset();
            }
        }

        bool done() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            long http_code = 0;
            if ( CURLE_OK != curl_easy_getinfo(
                curlh_.get(),
                CURLINFO_RESPONSE_CODE,
                &http_code) || !http_code )
            {
                status_ = statuses::failed;
                cvar_.notify_all();
                return false;
            }

            try {
                response_ = response(static_cast<http_code_t>(http_code));
                response_.content = std::move(response_content_);
                response_.headers = std::move(response_headers_);
                response_.uploader = std::move(breq_.uploader());
                response_.downloader = std::move(breq_.downloader());
            } catch (...) {
                status_ = statuses::failed;
                cvar_.notify_all();
                return false;
            }

            status_ = statuses::done;
            error_.clear();

            cvar_.notify_all();
            return true;
        }

        bool fail(CURLcode err) noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            switch ( err ) {
            case CURLE_OPERATION_TIMEDOUT:
                status_ = statuses::timeout;
                break;
            case CURLE_READ_ERROR:
            case CURLE_WRITE_ERROR:
            case CURLE_ABORTED_BY_CALLBACK:
                status_ = statuses::canceled;
                break;
            default:
                status_ = statuses::failed;
                break;
            }

            try {
                error_ = curl_easy_strerror(err);
            } catch (...) {
                // nothing
            }

            cvar_.notify_all();
            return true;
        }

        bool cancel() noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            if ( status_ != statuses::pending ) {
                return false;
            }

            status_ = statuses::canceled;
            error_.clear();

            cvar_.notify_all();
            return true;
        }

        statuses status() const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return status_;
        }

        bool is_done() const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return status_ == statuses::done;
        }

        bool is_pending() const noexcept {
            std::lock_guard<std::mutex> guard(mutex_);
            return status_ == statuses::pending;
        }

        statuses wait(bool wait_callback) const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait(lock, [this, wait_callback](){
                return (status_ != statuses::pending)
                    && (!wait_callback || callbacked_);
            });
            return status_;
        }

        statuses wait_for(time_ms_t ms, bool wait_callback) const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait_for(lock, ms, [this, wait_callback](){
                return (status_ != statuses::pending)
                    && (!wait_callback || callbacked_);
            });
            return status_;
        }

        statuses wait_until(time_point_t tp, bool wait_callback) const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait_until(lock, tp, [this, wait_callback](){
                return (status_ != statuses::pending)
                    && (!wait_callback || callbacked_);
            });
            return status_;
        }

        response get() {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait(lock, [this](){
                return status_ != statuses::pending;
            });
            if ( status_ != statuses::done ) {
                throw exception("curly_hpp: response is unavailable");
            }
            status_ = statuses::empty;
            return std::move(response_);
        }

        const std::string& get_error() const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait(lock, [this](){
                return status_ != statuses::pending;
            });
            return error_;
        }

        std::exception_ptr get_callback_exception() const noexcept {
            std::unique_lock<std::mutex> lock(mutex_);
            cvar_.wait(lock, [this](){
                return callbacked_;
            });
            return callback_exception_;
        }

        template < typename... Args >
        void call_callback(Args&&... args) noexcept {
            try {
                if ( breq_.callback() ) {
                    breq_.callback()(std::forward<Args>(args)...);
                }
            } catch (...) {
                std::lock_guard<std::mutex> guard(mutex_);
                callback_exception_ = std::current_exception();
            }
            std::lock_guard<std::mutex> guard(mutex_);
            assert(!callbacked_ && status_ != statuses::pending);
            callbacked_ = true;
            cvar_.notify_all();
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
                size = std::min(size, breq_.uploader()->size() - uploaded_.load());
                const std::size_t read_bytes = breq_.uploader()->read(dst, size);
                uploaded_.fetch_add(read_bytes);
                return read_bytes;
            } catch (...) {
                return CURL_READFUNC_ABORT;
            }
        }

        std::size_t download_callback_(const char* src, std::size_t size) noexcept {
            try {
                const std::size_t written_bytes = breq_.downloader()->write(src, size);
                downloaded_.fetch_add(written_bytes);
                return written_bytes;
            } catch (...) {
                return 0u;
            }
        }

        std::size_t header_callback_(const char* src, std::size_t size) noexcept {
            try {
                std::lock_guard<std::mutex> guard(mutex_);
                const std::string_view header(src, size);
                if ( !header.compare(0u, 5u, "HTTP/") ) {
                    response_headers_.clear();
                } else if ( const auto sep_idx = header.find(':'); sep_idx != std::string_view::npos ) {
                    if ( const auto key = header.substr(0, sep_idx); !key.empty() ) {
                        auto val = header.substr(sep_idx + 1);
                        const auto val_f = val.find_first_not_of("\t ");
                        const auto val_l = val.find_last_not_of("\r\n\t ");
                        val = (val_f != std::string_view::npos && val_l != std::string_view::npos)
                            ? val.substr(val_f, val_l)
                            : std::string_view();
                        response_headers_.emplace(key, val);
                    }
                }
                return header.size();
            } catch (...) {
                return 0;
            }
        }
    private:
        request_builder breq_;
        curlh_t curlh_{nullptr, &curl_easy_cleanup};
        slist_t hlist_{nullptr, &curl_slist_free_all};
        time_point_t last_response_;
        time_point_t::duration response_timeout_;
    private:
        response response_;
        headers_t response_headers_;
        std::vector<char> response_content_;
    private:
        std::atomic_size_t uploaded_{0u};
        std::atomic_size_t downloaded_{0u};
    private:
        bool callbacked_{false};
        std::exception_ptr callback_exception_{nullptr};
    private:
        statuses status_{statuses::pending};
        std::string error_{"Unknown error"};
    private:
        mutable std::mutex mutex_;
        mutable std::condition_variable cvar_;
    };
}

namespace curly_hpp
{
    request::request(internal_state_ptr state)
    : state_(state) {
        assert(state);
    }

    bool request::cancel() noexcept {
        return state_->cancel();
    }

    request::statuses request::status() const noexcept {
        return state_->status();
    }

    bool request::is_done() const noexcept {
        return state_->is_done();
    }

    bool request::is_pending() const noexcept {
        return state_->is_pending();
    }

    request::statuses request::wait() const noexcept {
        return state_->wait(false);
    }

    request::statuses request::wait_for(time_ms_t ms) const noexcept {
        return state_->wait_for(ms, false);
    }

    request::statuses request::wait_until(time_point_t tp) const noexcept {
        return state_->wait_until(tp, false);
    }

    request::statuses request::wait_callback() const noexcept {
        return state_->wait(true);
    }

    request::statuses request::wait_callback_for(time_ms_t ms) const noexcept {
        return state_->wait_for(ms, true);
    }

    request::statuses request::wait_callback_until(time_point_t tp) const noexcept {
        return state_->wait_until(tp, true);
    }

    response request::get() {
        return state_->get();
    }

    const std::string& request::get_error() const noexcept {
        return state_->get_error();
    }

    std::exception_ptr request::get_callback_exception() const noexcept {
        return state_->get_callback_exception();
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

    request_builder& request_builder::request_timeout(time_sec_t t) noexcept {
        request_timeout_ = t;
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

    request_builder& request_builder::callback(callback_t c) noexcept {
        callback_ = std::move(c);
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

    time_sec_t request_builder::request_timeout() const noexcept {
        return request_timeout_;
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

    callback_t& request_builder::callback() noexcept {
        return callback_;
    }

    const callback_t& request_builder::callback() const noexcept {
        return callback_;
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

    request request_builder::send() {
        auto sreq = std::make_shared<request::internal_state>(std::move(*this));
        new_handles.enqueue(sreq);
        return request(sreq);
    }
}

// -----------------------------------------------------------------------------
//
// auto_performer
//
// -----------------------------------------------------------------------------

namespace curly_hpp
{
    auto_performer::auto_performer() {
        thread_ = std::thread([this](){
            while ( !done_ ) {
                curly_hpp::perform();
                curly_hpp::wait_activity(wait_activity());
            }
        });
    }

    auto_performer::~auto_performer() noexcept {
        done_.store(true);
        if ( thread_.joinable() ) {
            thread_.join();
        }
    }

    time_ms_t auto_performer::wait_activity() const noexcept {
        return wait_activity_;
    }

    void auto_performer::wait_activity(time_ms_t ms) noexcept {
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
    void perform() {
        curl_state::with([](CURLM* curlm){
            req_state_t sreq;
            while ( new_handles.try_dequeue(sreq) ) {
                try {
                    sreq->enqueue(curlm);
                    active_handles.emplace_back(sreq);
                } catch (...) {
                    sreq->fail(CURLcode::CURLE_FAILED_INIT);
                    sreq->dequeue(curlm);
                    sreq->call_callback(sreq);
                }
            }
        });

        curl_state::with([](CURLM* curlm){
            int running_handles = 0;
            if ( CURLM_OK != curl_multi_perform(curlm, &running_handles) ) {
                throw exception("curly_hpp: failed to curl_multi_perform");
            }

            while ( true ) {
                int msgs_in_queue = 0;
                CURLMsg* msg = curl_multi_info_read(curlm, &msgs_in_queue);
                if ( !msg ) {
                    break;
                }
                if ( msg->msg != CURLMSG_DONE ) {
                    continue;
                }
                void* priv_ptr = nullptr;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv_ptr);
                if ( auto sreq = static_cast<req_state_t::element_type*>(priv_ptr); sreq ) {
                    if ( msg->data.result == CURLcode::CURLE_OK ) {
                        sreq->done();
                    } else {
                        sreq->fail(msg->data.result);
                    }
                }
            }

            const auto now = time_point_t::clock::now();
            for ( const auto& sreq : active_handles ) {
                if ( sreq->check_response_timeout(now) ) {
                    sreq->fail(CURLE_OPERATION_TIMEDOUT);
                }
            }
        });

        curl_state::with([](CURLM* curlm){
            for ( auto iter = active_handles.begin(); iter != active_handles.end(); ) {
                if ( !(*iter)->is_pending() ) {
                    (*iter)->dequeue(curlm);
                    (*iter)->call_callback(*iter);
                    iter = active_handles.erase(iter);
                } else {
                    ++iter;
                }
            }
        });
    }

    void wait_activity(time_ms_t ms) {
        curl_state::with([ms](CURLM* curlm){
            if ( active_handles.empty() ) {
                new_handles.wait_content_for(ms);
            } else if ( new_handles.empty() ) {
                const int timeout_ms = static_cast<int>(ms.count());
                if ( CURLM_OK != curl_multi_wait(curlm, nullptr, 0, timeout_ms, nullptr) ) {
                    throw exception("curly_hpp: failed to curl_multi_wait");
                }
            }
        });
    }
}
