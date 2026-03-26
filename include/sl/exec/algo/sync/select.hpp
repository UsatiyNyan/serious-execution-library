//
// Created by usatiynyan.
// locking implementation for now
//
// TODO: lock-free implementation
//
// "something something... consensus"
//
// const int branch = co_await select()
//   .case_(chan1.send("hehe"), [](meta::unit) -> int { fmt::println("sent hehe"); return 0; }
//   .case_(chan2.receive(), [](std::string message) -> int { fmt::println("received {}", message); return 1; }
//   .case_(signal, [](meta::unit) -> int { fmt::println("some signal has finished"); return 2; }
//   .default_([](meta::unit) -> int { fmt::println("none were ready"); return -1; }
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/connection.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <sl/meta/match/overloaded.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/unit.hpp>

#include <cstdint>
#include <limits>

namespace sl::exec {
namespace detail {

template <typename F, typename SomeSignalT>
concept SelectFunctorFor = std::invocable<F, typename SomeSignalT::value_type>;

template <typename SomeSignalT, SelectFunctorFor<SomeSignalT> F>
    requires std::same_as<typename SomeSignalT::error_type, meta::unit>
struct select_case {
    using signal_type = SomeSignalT;
    using functor_type = F;
    using value_type = std::invoke_result_t<functor_type, typename signal_type::value_type>;

public:
    signal_type signal;
    functor_type functor;
};

template <template <typename> typename Atomic, typename ValueT>
struct select_slot : slot<ValueT, meta::unit> {
    virtual ~select_slot() = default;
    [[nodiscard]] virtual Atomic<std::size_t>& get_done() & = 0;
    virtual void set_value_skip_done(ValueT&& value) & = 0;
    virtual void set_null_skip_done() & = 0;
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct select_connection final
    : connection
    , cancel_handle {
    using value_type = ValueT;
    using error_type = meta::unit;

    template <typename SelectCase>
    struct case_slot_type final : select_slot<Atomic, typename SelectCase::signal_type::value_type> {
        using value_type = typename SelectCase::signal_type::value_type;
        using functor_type = typename SelectCase::functor_type;

    public:
        case_slot_type(functor_type&& functor, select_connection& self, std::size_t index)
            : functor_{ std::move(functor) }, self_{ self }, index_{ index } {}

        void set_value(value_type&& value) & override {
            self_.set_value_impl</*CheckDone=*/true>(std::move(value), std::move(functor_), index_);
        }
        void set_error(meta::unit&&) & override { self_.set_error_impl(); }
        void set_null() & override { self_.set_null_impl</*CheckDone=*/true>(); }

        Atomic<std::size_t>& get_done() & override { return self_.done_; }
        void set_value_skip_done(value_type&& value) & override {
            self_.set_value_impl</*CheckDone=*/false>(std::move(value), std::move(functor_), index_);
        }
        void set_null_skip_done() & override { self_.set_null_impl</*CheckDone=*/false>(); }

    private:
        functor_type functor_;
        select_connection& self_;
        std::size_t index_;
    };

private:
    static constexpr std::size_t cases_count = sizeof...(SelectCaseTs);
    template <typename SelectCaseT>
    using connection_type = subscribe_connection<typename SelectCaseT::signal_type, case_slot_type<SelectCaseT>>;
    using connections_type = std::tuple<connection_type<SelectCaseTs>...>;

    template <std::size_t... Indexes>
    static connections_type make_connections(
        select_connection& self,
        std::tuple<SelectCaseTs...>&& cases,
        std::index_sequence<Indexes...>
    ) {
        return std::make_tuple(meta::lazy_eval{ [a_case = std::move(std::get<Indexes>(cases)), &self]() mutable {
            return connection_type<SelectCaseTs>{
                std::move(a_case.signal),
                [functor = std::move(a_case.functor), &self]() mutable {
                    return case_slot_type<SelectCaseTs>{ std::move(functor), self, Indexes };
                },
            };
        } }...);
    }

public:
    select_connection(std::tuple<SelectCaseTs...>&& cases, slot<value_type, error_type>& slot)
        : connections_{ make_connections(*this, std::move(cases), std::make_index_sequence<cases_count>()) },
          slot_{ slot } {}

public: // connection
    cancel_handle& emit() && override {
        constexpr bool connections_are_ordered =
            (std::derived_from<ConnectionFor<typename SelectCaseTs::signal_type>, ordered_connection> && ... && true);

        if constexpr (connections_are_ordered) {
            // if all connections are ordered, then we don't need to sort them
            std::size_t i = 0;
            meta::for_each(
                [&](connection&& a_connection) { cancel_handles_[i++] = &std::move(a_connection).emit(); },
                std::move(connections_)
            );
            DEBUG_ASSERT(i == cases_count);
        } else {
            // otherwise, ordered connections take precedence in their declared order, before non-ordered
            // which is basically an implementation of safe "TrySelect" (see "Dining philosophers problem")
            std::array<std::pair<connection*, std::uintptr_t>, cases_count> ordered_connections{};
            constexpr meta::overloaded get_ordering{
                [](const ordered_connection& an_ordered_connection) { return an_ordered_connection.get_ordering(); },
                [](const connection&) { return std::numeric_limits<std::uintptr_t>::max(); },
            };
            {
                std::size_t i = 0;
                meta::for_each(
                    [&](auto& a_subscribe_connection) {
                        ordered_connections[i++] = std::pair<connection*, std::uintptr_t>{
                            &a_subscribe_connection,
                            get_ordering(a_subscribe_connection.get_inner()),
                        };
                    },
                    connections_
                );
                DEBUG_ASSERT(i == cases_count);
            }

            std::stable_sort(ordered_connections.begin(), ordered_connections.end(), [](const auto& x, const auto& y) {
                return x.second < y.second;
            });

            {
                std::size_t i = 0;
                for (auto [a_connection_ptr, _] : ordered_connections) {
                    cancel_handles_[i++] = &std::move(*a_connection_ptr).emit();
                }
                DEBUG_ASSERT(i == cases_count);
            }
        }
        return *this;
    }

public: // cancel_handle
    bool try_cancel() & override {
        if (try_check_done()) {
            return false;
        }

        std::uint32_t cancel_counter = 0;

        for (std::size_t i = 0; i != cancel_handles_.size(); ++i) {
            cancel_handle& a_cancel_handle = *cancel_handles_[i];
            const bool is_cancelled = a_cancel_handle.try_cancel();
            cancel_counter += static_cast<std::uint32_t>(is_cancelled);
        }

        const bool is_last = increment_and_check(cancel_counter);
        if (is_last) {
            delete this;
        }

        return true;
    }

private:
    [[nodiscard]] bool increment_and_check(std::uint32_t diff = 1) {
        const std::uint32_t current_count = diff + counter_.fetch_add(diff, std::memory_order::relaxed);
        const bool is_last = current_count == cases_count;
        return is_last;
    }

    [[nodiscard]] bool try_check_done() { return !kcas(kcas_arg<std::size_t>{ .a = &done_, .e = 0, .n = 1 }); }

    template <bool CheckDone, typename CaseValueT, typename CaseF>
    void set_value_impl(CaseValueT&& case_value, CaseF&& case_functor, std::size_t index) & {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if constexpr (CheckDone) {
            if (try_check_done()) {
                return;
            }
        }

        value_type value = std::move(case_functor)(std::move(case_value));
        slot_.set_value(std::move(value));
        try_cancel_beside(index);
    }

    void set_error_impl() & {
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }
        meta::defer cleanup{ [this] { delete this; } };

        if (try_check_done()) {
            return;
        }
        slot_.set_error(meta::unit{});
    }

    template <bool CheckDone>
    void set_null_impl() & {
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }
        meta::defer cleanup{ [this] { delete this; } };

        if constexpr (CheckDone) {
            if (try_check_done()) {
                return;
            }
            slot_.set_null();
        }
        // if !CheckDone, done_ is already set, just delete via defer
    }

    void try_cancel_beside(std::size_t excluded_index) {
        std::uint32_t cancel_counter = 0;

        for (std::size_t i = 0; i != cancel_handles_.size(); ++i) {
            if (i == excluded_index) {
                continue;
            }
            cancel_handle& a_cancel_handle = *cancel_handles_[i];
            const bool is_cancelled = a_cancel_handle.try_cancel();
            cancel_counter += static_cast<std::uint32_t>(is_cancelled);
        }

        const bool should_not_be_last = increment_and_check(cancel_counter);
        ASSERT(!should_not_be_last);
    }

private:
    connections_type connections_;
    std::array<cancel_handle*, cases_count> cancel_handles_;
    slot<value_type, error_type>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<std::size_t> done_{ 0 }; // word-size for CAS2
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct select_connection_box final : connection {
    using inner_connection_type = select_connection<Atomic, ValueT, SelectCaseTs...>;

public:
    select_connection_box(std::tuple<SelectCaseTs...>&& cases, slot<ValueT, meta::unit>& slot)
        : connection_{ std::make_unique<inner_connection_type>(std::move(cases), slot) } {}

    cancel_handle& emit() && override {
        auto& a_connection = *DEBUG_ASSERT_VAL(connection_.release());
        return std::move(a_connection).emit();
    }

private:
    std::unique_ptr<inner_connection_type> connection_;
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct [[nodiscard]] select final {
    using value_type = ValueT;
    using error_type = meta::unit;
    using default_case_signal = result_signal<meta::unit, meta::unit>;

public:
    template <typename SelectCaseT>
    constexpr select(SelectCaseT&& a_case) : cases_{ std::move(a_case) } {}

    template <typename PrevSelectCasesTuple, typename SelectCaseT>
    constexpr select(PrevSelectCasesTuple&& prev_cases, SelectCaseT&& a_case)
        : cases_{ std::tuple_cat(std::move(prev_cases), std::make_tuple(std::move(a_case))) } {}

    template <SomeSignal NextSignalT, typename NextF, typename NextCaseT = select_case<NextSignalT, NextF>>
        requires std::same_as<typename NextCaseT::value_type, value_type>
    constexpr auto case_(NextSignalT&& signal, NextF&& functor) && {
        return select<Atomic, ValueT, SelectCaseTs..., NextCaseT>{
            std::move(cases_),
            NextCaseT{ std::move(signal), std::move(functor) },
        };
    }

    template <SelectFunctorFor<default_case_signal> NextF>
    constexpr auto default_(NextF&& functor) && {
        return std::move(*this).case_(default_case_signal{ meta::unit{} }, std::move(functor));
    }

public: // SomeSignal
    select_connection_box<Atomic, ValueT, SelectCaseTs...> subscribe(slot<value_type, error_type>& slot) && {
        return select_connection_box<Atomic, ValueT, SelectCaseTs...>{ std::move(cases_), slot };
    }

    executor& get_executor() & { return inline_executor(); }

private:
    std::tuple<SelectCaseTs...> cases_;
};

template <template <typename> typename Atomic>
struct [[nodiscard]] select_start final {
    template <SomeSignal SomeSignalT, SelectFunctorFor<SomeSignalT> F>
    auto case_(SomeSignalT&& signal, F&& functor) {
        using case_type = select_case<SomeSignalT, F>;
        return select<Atomic, typename case_type::value_type, case_type>{
            case_type{ std::move(signal), std::move(functor) },
        };
    }
};

} // namespace detail

template <template <typename> typename Atomic>
constexpr auto select_() {
    return detail::select_start<Atomic>{};
}

constexpr auto select() { return select_<detail::atomic>(); }

} // namespace sl::exec
