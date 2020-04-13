// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_UTILS_H
#define PACKIO_UTILS_H

#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include <msgpack.hpp>

#include "../error_code.h"
#include "config.h"
#include "log.h"

namespace packio {
namespace internal {

template <typename, typename = void>
struct func_traits : std::false_type {
};

template <typename T>
struct func_traits<T, std::void_t<decltype(&std::decay_t<T>::operator())>>
    : func_traits<decltype(&std::decay_t<T>::operator())> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...)> : func_traits<R (*)(Args...)> {
};

template <typename C, typename R, typename... Args>
struct func_traits<R (C::*)(Args...) const> : func_traits<R (*)(Args...)> {
};

template <typename R, typename... Args>
struct func_traits<R (*)(Args...)> : std::true_type {
    using result_type = R;
    using args_type = std::tuple<Args...>;
    static constexpr auto args_count = std::tuple_size_v<args_type>;
};

template <typename T>
constexpr bool func_traits_v = func_traits<T>::value;

#if defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_DOCUMENTATION)
template <typename>
struct is_awaitable : std::false_type {
};

template <typename... Args>
struct is_awaitable<packio::asio::awaitable<Args...>> : std::true_type {
};

template <typename, typename = void>
struct is_coroutine : std::false_type {
};

template <typename T>
struct is_coroutine<T, std::enable_if_t<func_traits_v<T>>>
    : is_awaitable<typename func_traits<T>::result_type> {
};

template <typename T>
constexpr bool is_coroutine_v = is_coroutine<T>::value;
#endif // defined(PACKIO_HAS_CO_AWAIT) || defined(PACKIO_DOCUMENTATION)

template <typename T>
struct shift_tuple;

template <typename A, typename... Bs>
struct shift_tuple<std::tuple<A, Bs...>> {
    using type = std::tuple<Bs...>;
};

template <typename T>
using shift_tuple_t = typename shift_tuple<T>::type;

template <typename T>
struct decay_tuple;

template <typename... Args>
struct decay_tuple<std::tuple<Args...>> {
    using type = std::tuple<std::decay_t<Args>...>;
};

template <typename T>
using decay_tuple_t = typename decay_tuple<T>::type;

inline packio::asio::const_buffer buffer(const msgpack::sbuffer& buf)
{
    return packio::asio::const_buffer(buf.data(), buf.size());
}

inline std::vector<packio::asio::const_buffer> buffer(const msgpack::vrefbuffer& buf)
{
    std::vector<packio::asio::const_buffer> vec;
    vec.reserve(buf.vector_size());
    const struct iovec* iov = buf.vector();
    for (std::size_t i = 0; i < buf.vector_size(); ++i) {
        vec.push_back(packio::asio::const_buffer(iov->iov_base, iov->iov_len));
        ++iov;
    }
    return vec;
}

template <typename T>
msgpack::object_handle make_msgpack_object(T&& value)
{
    msgpack::object_handle oh({}, std::make_unique<msgpack::zone>());
    oh.set(msgpack::object(std::forward<T>(value), *oh.zone()));
    return oh;
}

template <typename T>
void set_no_delay(T&)
{
}

template <>
inline void set_no_delay(packio::asio::ip::tcp::socket& socket)
{
    socket.set_option(packio::asio::ip::tcp::no_delay{true});
}

struct incompatible_handler_t {
};

template <typename Handler>
auto wrap_call_handler(Handler&& handler)
{
    if constexpr (std::is_invocable_v<Handler, packio::err::error_code, msgpack::object_handle>) {
        // handler takes the object handler as argument
        return std::forward<Handler>(handler);
    }
    else if constexpr (std::is_invocable_v<Handler, packio::err::error_code, std::nullopt_t>) {
        // handler takes an optional to a type, try to convert
        using args = typename func_traits<Handler>::args_type;
        using opt_result_type = std::tuple_element_t<1, args>;
        using result_type = typename opt_result_type::value_type;
        return [handler = std::forward<Handler>(handler)](
                   packio::err::error_code ec,
                   msgpack::object_handle result) mutable {
            if (ec) {
                handler(ec, std::nullopt);
            }
            else {
                try {
                    handler(ec, result->as<result_type>());
                }
                catch (msgpack::type_error&) {
                    ec = make_error_code(error::bad_result_type);
                    handler(ec, std::nullopt);
                }
            }
        };
    }
    else if constexpr (std::is_invocable_v<Handler, packio::err::error_code>) {
        // handler takes no argument, just call with the error code
        return [handler = std::forward<Handler>(handler)](
                   packio::err::error_code ec, msgpack::object_handle) mutable {
            handler(ec);
        };
    }
    else {
        // handler cannot be used as a call handler
        // return a special type to identify this using traits
        return incompatible_handler_t{};
    }
}

} // internal
} // packio

#endif // PACKIO_UTILS_H
