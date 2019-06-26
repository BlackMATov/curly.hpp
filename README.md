# curly.hpp

> Simple cURL C++17 wrapper

[![travis][badge.travis]][travis]
[![appveyor][badge.appveyor]][appveyor]
[![codecov][badge.codecov]][codecov]
[![language][badge.language]][language]
[![license][badge.license]][license]
[![paypal][badge.paypal]][paypal]

[badge.travis]: https://img.shields.io/travis/BlackMATov/curly.hpp/master.svg?logo=travis
[badge.appveyor]: https://img.shields.io/appveyor/ci/BlackMATov/curly-hpp/master.svg?logo=appveyor
[badge.codecov]: https://img.shields.io/codecov/c/github/BlackMATov/curly.hpp/master.svg?logo=codecov
[badge.language]: https://img.shields.io/badge/language-C%2B%2B17-yellow.svg
[badge.license]: https://img.shields.io/badge/license-MIT-blue.svg
[badge.paypal]: https://img.shields.io/badge/donate-PayPal-orange.svg?logo=paypal&colorA=00457C

[travis]: https://travis-ci.org/BlackMATov/curly.hpp
[appveyor]: https://ci.appveyor.com/project/BlackMATov/curly-hpp
[codecov]: https://codecov.io/gh/BlackMATov/curly.hpp
[language]: https://en.wikipedia.org/wiki/C%2B%2B14
[license]: https://en.wikipedia.org/wiki/MIT_License
[paypal]: https://www.paypal.me/matov

[curly]: https://github.com/BlackMATov/curly.hpp

## Installation

Just add the root repository directory to your cmake project:

```cmake
add_subdirectory(external/curly.hpp)
target_link_libraries(your_project_target curly.hpp)
```

## Features

- Custom headers
- Asynchronous requests
- PUT, GET, HEAD, POST methods
- Custom uploading and downloading streams
- Connection and last server response timeouts

## Examples

```cpp
#include <curly.hpp/curly.hpp>
namespace net = curly_hpp;

// creates and hold a separate thread for automatically update async requests
net::auto_performer performer;

// also, you can update requests manually from your favorite thread
net::perform();
```

### GET Requests

```cpp
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
```

### POST Requests

```cpp
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
```

### Error Handling

```cpp
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
```

### Streamed Requests

#### Downloading

```cpp
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
    .send().wait();
```

#### Uploading

```cpp
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

    std::size_t upload(char* dst, std::size_t size) override {
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
    .send().wait();
```

## API

> coming soon!
