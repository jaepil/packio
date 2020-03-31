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
