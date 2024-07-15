//
// Created by usatiynyan.
//

#include "sl/exec/st/future.hpp"

#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/tuple/for_each_meta_enumerate.hpp>
#include <tl/optional.hpp>
#include <tuple>

namespace sl::exec::st {
namespace detail {

template <typename T>
struct first_shared_state {
    promise<T> p;
    std::uint32_t count;
    bool is_fulfilled = false;

    void set_value(T&& value) {
        if (!std::exchange(is_fulfilled, true)) {
            std::move(p).set_value(std::move(value));
        }
        if (--count == 0) {
            delete this;
        }
    }
};

} // namespace detail

template <typename T, typename... Ts>
    requires((std::is_same_v<T, Ts> && ...))
auto first(generic_executor& executor, future<T> x, future<Ts>... xs) noexcept {
    auto [f, p] = make_contract<T>();

    auto* first_shared_state = new detail::first_shared_state<T>{
        .p = std::move(p),
        .count = 1 + sizeof...(Ts),
    };
    auto callback = [first_shared_state](T&& value) noexcept { first_shared_state->set_value(std::move(value)); };
    std::move(x).set_callback(executor, callback);
    (std::move(xs).set_callback(executor, callback), ...);

    return std::move(f);
}

namespace detail {

template <typename... Ts>
struct all_shared_state {
    promise<std::tuple<Ts...>> p;
    std::tuple<tl::optional<Ts>...> maybe_tmp{};
    std::uint32_t count;

    template <std::size_t i, typename T>
    void set_value(T&& value) {
        {
            auto& maybe_ith_tmp = std::get<i>(maybe_tmp);
            ASSERT(!maybe_ith_tmp.has_value());
            maybe_ith_tmp.emplace(std::move(value));
        }

        if (--count != 0) {
            return;
        }

        auto result = meta::for_each(
            [](auto&& maybe_ith_tmp) {
                ASSERT(maybe_ith_tmp.has_value());
                return std::move(maybe_ith_tmp).value();
            },
            std::move(maybe_tmp)
        );

        std::move(p).set_value(std::move(result));

        delete this;
    }

    template <typename FutureTuple, std::size_t... is>
    void set_callbacks(generic_executor& executor, FutureTuple&& xs, std::index_sequence<is...>) {
        (std::move(std::get<is>(xs))
             .set_callback(executor, [this](auto&& value) noexcept { this->set_value<is>(std::move(value)); }),
         ...);
    }
};

} // namespace detail

template <typename... Ts>
auto all(generic_executor& executor, future<Ts>... xs) noexcept {
    auto [f, p] = make_contract<std::tuple<Ts...>>();

    auto* all_shared_state = new detail::all_shared_state<Ts...>{
        .p = std::move(p),
        .count = sizeof...(Ts),
    };

    all_shared_state->set_callbacks(executor, std::make_tuple(std::move(xs)...), std::index_sequence_for<Ts...>());

    return std::move(f);
}
} // namespace sl::exec::st
