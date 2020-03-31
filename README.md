
## Header-only | msgpack-RPC | Boost.Asio

This library requires C++17 and is designed as an extension to Boost.Asio. It will let you build asynchronous servers or client for msgpack-RPC.

The project is hosted on [GitHub](https://github.com/qchateau/packio/) and available on [Conan Center](https://conan.io/center/). Documentation is available on [GitHub Pages](https://qchateau.github.io/packio/).

## Primer

```cpp
// Declare a server and a client, sharing the same io_context
boost::asio::io_context io;
ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
auto server = std::make_shared<packio::server<ip::tcp>>(ip::tcp::acceptor{io, bind_ep});
auto client = std::make_shared<packio::client<ip::tcp>>(ip::tcp::socket{io});

// Declare a synchronous callback
server->dispatcher()->add("add", [](int a, int b) { return a + b; });
// Declare an asynchronous callback
server->dispatcher()->add_async(
    "multiply", [](packio::completion_handler complete, int a, int b) {
        // Call the completion handler later
        boost::asio::post(
            io, [a, b, complete = std::move(complete)]() mutable {
                complete(a * b);
            });
    });

// Accept connections forever
server->async_serve_forever();
// Connect the client
client->socket().connect(server.acceptor().local_endpoint());

// Make an asynchronous call
client->async_call("add", std::make_tuple(42, 24),
    [&](boost::system::error_code, msgpack::object r) {
        std::cout << "The result is: " << r.as<int>() << std::endl;
    });

// Use boost::asio::use_future
std::future<msgpack::object_handle> add_future = client->async_call(
    "add", std::tuple{12, 23}, boost::asio::use_future);
std::cout << "The result is: " << add_future.get()->as<int>() << std::endl;

// Use auto result type conversion
client->async_call(
    "multiply",
    std::make_tuple(42, 24),
    [&](boost::system::error_code, std::optional<int> r) {
        std::cout << "The result is: " << *r << std::endl;
    });
```

## Requirements

- C++17
- Boost.Asio >= 1.72.0
- msgpack >= 3.2.1

## Tested compilers

- gcc-7
- gcc-8
- gcc-9
- clang-5
- clang-6
- clang-7
- clang-8
- clang-9
- Apple clang-10
- Apple clang-11
- Visual Studio 2019 Version 16

## Conan

```bash
conan install packio/1.1.0
```

## Bonus

Let's compute fibonacci's numbers recursively using packio on a single thread.

```cpp
#include <iostream>
#include <boost/asio.hpp>
#include <packio/packio.h>

namespace ip = boost::asio::ip;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    boost::asio::io_context io;
    ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
    auto server = std::make_shared<packio::server<ip::tcp>>(
        ip::tcp::acceptor{io, bind_ep});
    auto client = std::make_shared<packio::client<ip::tcp>>(ip::tcp::socket{io});

    server->dispatcher()->add_async(
        "fibonacci", [&](packio::completion_handler complete, int n) {
            if (n <= 1) {
                complete(n);
                return;
            }

            client->async_call(
                "fibonacci",
                std::tuple{n - 1},
                [n, &client, complete = std::move(complete)](
                    boost::system::error_code, std::optional<int> r1) mutable {
                    client->async_call(
                        "fibonacci",
                        std::tuple{n - 2},
                        [r1, complete = std::move(complete)](
                            boost::system::error_code,
                            std::optional<int> r2) mutable {
                            complete(*r1 + *r2);
                        });
                });
        });

    client->socket().connect(server->acceptor().local_endpoint());
    server->async_serve_forever();

    int result = 0;

    client->async_call(
        "fibonacci",
        std::tuple{n},
        [&](boost::system::error_code, std::optional<int> r) {
            result = *r;
            io.stop();
        });

    io.run();

    std::cout << "F{" << n << "} = " << result << std::endl;

    return 0;
}
```
