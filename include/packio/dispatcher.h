// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_DISPATCHER_H
#define PACKIO_DISPATCHER_H

//! @file
//! Class @ref packio::dispatcher "dispatcher"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>

#include <msgpack.hpp>

#include "error_code.h"
#include "handler.h"
#include "internal/unique_function.h"
#include "internal/utils.h"
#include "traits.h"

namespace packio {

//! The dispatcher class, used to store and dispatch procedures
//! @tparam Map The container used to associate procedures to their name
//! @tparam Lockable The lockable used to protect accesses to the procedure map
template <template <class...> class Map, typename Lockable>
class dispatcher {
public:
    //! The mutex type used to protect the procedure map
    using mutex_type = Lockable;
    //! The type of function stored in the dispatcher
    using function_type =
        internal::unique_function<void(completion_handler, const msgpack::object&)>;
    //! A shared pointer to @ref function_type
    using function_ptr_type = std::shared_ptr<function_type>;

    //! Add a synchronous procedure to the dispatcher
    //! @param name The name of the procedure
    //! @param fct The procedre itself
    template <typename SyncProcedure>
    bool add(std::string_view name, SyncProcedure&& fct)
    {
        ASSERT_TRAIT(SyncProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(name, wrap_sync(std::forward<SyncProcedure>(fct)))
            .second;
    }

    //! Add an asynchronous procedure to the dispatcher
    //! @param name The name of the procedure
    //! @param fct The procedre itself
    template <typename AsyncProcedure>
    bool add_async(std::string_view name, AsyncProcedure&& fct)
    {
        ASSERT_TRAIT(AsyncProcedure);
        std::unique_lock lock{map_mutex_};
        return function_map_
            .emplace(name, wrap_async(std::forward<AsyncProcedure>(fct)))
            .second;
    }

    //! Remove a procedure from the dispatcher
    //! @param name The name of the procedure to remove
    //! @return True if the procedure was removed, False if it was not found
    bool remove(const std::string& name)
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.erase(name);
    }

    //! Check if a procedure is registered
    //! @param name The name of the procedure to check
    //! @return True if the procedure is known
    bool has(const std::string& name) const
    {
        std::unique_lock lock{map_mutex_};
        return function_map_.find(name) != function_map_.end();
    }

    //! Remove all procedures
    //! @return The number of procedures removed
    size_t clear()
    {
        std::unique_lock lock{map_mutex_};
        size_t size = function_map_.size();
        function_map_.clear();
        return size;
    }

    //! Get the name of all known procedures
    //! @return Vector containing the name of all known procedures
    std::vector<std::string> known() const
    {
        std::unique_lock lock{map_mutex_};
        std::vector<std::string> names;
        names.reserve(function_map_.size());
        std::transform(
            function_map_.begin(),
            function_map_.end(),
            std::back_inserter(names),
            [](const typename decltype(function_map_)::value_type& pair) {
                return pair.first;
            });
        return names;
    }

    function_ptr_type get(const std::string& name) const
    {
        std::unique_lock lock{map_mutex_};
        auto it = function_map_.find(name);
        if (it == function_map_.end()) {
            return {};
        }
        else {
            return it->second;
        }
    }

private:
    using map_type = Map<std::string, function_ptr_type>;

    template <typename F>
    function_ptr_type wrap_sync(F&& fct)
    {
        using value_args =
            internal::decay_tuple_t<typename internal::func_traits<F>::args_type>;
        using result_type = typename internal::func_traits<F>::result_type;

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler handler, const msgpack::object& args) mutable {
                if (args.via.array.size != std::tuple_size_v<value_args>) {
                    // keep this check otherwise msgpack unpacker
                    // may silently drop arguments
                    PACKIO_DEBUG("incompatible argument count");
                    handler.set_error("Incompatible arguments");
                    return;
                }

                try {
                    if constexpr (std::is_void_v<result_type>) {
                        std::apply(fct, args.as<value_args>());
                        handler();
                    }
                    else {
                        handler(std::apply(fct, args.as<value_args>()));
                    }
                }
                catch (msgpack::type_error&) {
                    PACKIO_DEBUG("incompatible arguments");
                    handler.set_error("Incompatible arguments");
                }
            });
    }

    template <typename F>
    function_ptr_type wrap_async(F&& fct)
    {
        using args = typename internal::func_traits<F>::args_type;
        using value_args = internal::decay_tuple_t<internal::shift_tuple_t<args>>;

        return std::make_shared<function_type>(
            [fct = std::forward<F>(fct)](
                completion_handler handler, const msgpack::object& args) mutable {
                if (args.via.array.size != std::tuple_size_v<value_args>) {
                    // keep this check otherwise msgpack unpacker
                    // may silently drop arguments
                    PACKIO_DEBUG("incompatible argument count");
                    handler.set_error("Incompatible arguments");
                    return;
                }

                try {
                    std::apply(
                        [&](auto&&... args) {
                            fct(std::move(handler),
                                std::forward<decltype(args)>(args)...);
                        },
                        args.as<value_args>());
                }
                catch (msgpack::type_error&) {
                    PACKIO_DEBUG("incompatible arguments");
                    handler.set_error("Incompatible arguments");
                }
            });
    }

    mutable mutex_type map_mutex_;
    map_type function_map_;
};

using default_dispatcher = dispatcher<std::unordered_map, std::mutex>;

} // packio

#endif // PACKIO_DISPATCHER_H
