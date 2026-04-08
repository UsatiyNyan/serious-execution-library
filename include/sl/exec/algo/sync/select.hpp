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
#include "sl/exec/algo/sync/detail/parallel.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/type/unit.hpp>

#include <memory>

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
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct select_connection final {
private:
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
        void set_null() & override { self_.set_null_impl(); }

        Atomic<std::size_t>& get_done() & override { return self_.done_; }
        void set_value_skip_done(value_type&& value) & override {
            self_.set_value_impl</*CheckDone=*/false>(std::move(value), std::move(functor_), index_);
        }

    private:
        functor_type functor_;
        select_connection& self_;
        std::size_t index_;
    };

    struct select_delete_this {
        select_connection* this_;
        void operator()() { delete this_; }
    };

    template <typename SelectCaseT>
    using Connection = subscribe_connection<typename SelectCaseT::signal_type, case_slot_type<SelectCaseT>>;
    static constexpr std::size_t N = sizeof...(SelectCaseTs);

    template <std::size_t... Indexes>
    static auto make_connections(
        select_connection& self,
        std::tuple<SelectCaseTs...>&& cases,
        std::index_sequence<Indexes...>
    ) {
        return std::make_tuple(meta::lazy_eval{ [a_case = std::move(std::get<Indexes>(cases)), &self]() mutable {
            return Connection<SelectCaseTs>{
                std::move(a_case.signal),
                [functor = std::move(a_case.functor), &self]() mutable {
                    return case_slot_type<SelectCaseTs>{ std::move(functor), self, Indexes };
                },
            };
        } }...);
    }

public:
    select_connection(std::tuple<SelectCaseTs...>&& cases, executor& an_executor, slot<ValueT, meta::unit>& slot)
        : parallel_{ make_connections(*this, std::move(cases), std::make_index_sequence<N>()),
                     an_executor,
                     select_delete_this{ this } },
          slot_{ slot } {}

public: // connection
    cancel_handle& emit() && {
        constexpr bool connections_are_ordered =
            (std::derived_from<ConnectionFor<typename SelectCaseTs::signal_type>, ordered_connection> && ... && true);

        if constexpr (connections_are_ordered) {
            // if all connections are ordered, then we don't need to sort them
            return std::move(parallel_).emit();
        } else {
            // otherwise, ordered connections take precedence in their declared order, before non-ordered
            // which is basically an implementation of safe "TrySelect" (see "Dining philosophers problem")
            return std::move(parallel_).emit_ordered();
        }
    }

private:
    [[nodiscard]] bool check_done() { return kcas(kcas_arg<std::size_t>{ .a = &done_, .e = 0, .n = 1 }); }

    template <bool CheckDone, typename CaseValueT, typename CaseF>
    void set_value_impl(CaseValueT&& case_value, CaseF&& case_functor, std::size_t index) {
        if (!CheckDone || !check_done()) {
            ValueT value = std::move(case_functor)(std::move(case_value));
            slot_.set_value(std::move(value));
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

    void set_error_impl() {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!check_done()) {
            slot_.set_error(meta::unit{});
        }

        parallel_.schedule_delete_this();
    }

    void set_null_impl() {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!check_done()) {
            slot_.set_null();
        }

        parallel_.schedule_delete_this();
    }

private:
    parallel_connection<select_delete_this, Atomic, Connection<SelectCaseTs>...> parallel_;
    slot<ValueT, meta::unit>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<std::size_t> done_{ 0 }; // word-size for CAS2
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct select_connection_box final : connection {
    using connection_type = select_connection<Atomic, ValueT, SelectCaseTs...>;

public:
    select_connection_box(std::tuple<SelectCaseTs...>&& cases, executor& an_executor, slot<ValueT, meta::unit>& slot)
        : connection_{ std::make_unique<connection_type>(std::move(cases), an_executor, slot) } {}

    cancel_handle& emit() && override {
        auto& a_connection = *DEBUG_ASSERT_VAL(connection_.release());
        return std::move(a_connection).emit();
    }

private:
    std::unique_ptr<connection_type> connection_;
};

template <template <typename> typename Atomic, typename ValueT, typename... SelectCaseTs>
struct [[nodiscard]] select final {
    using value_type = ValueT;
    using error_type = meta::unit;
    using default_case_signal = result_signal<meta::unit, meta::unit>;

public:
    template <typename SelectCaseT>
    explicit constexpr select(SelectCaseT&& a_case, executor& an_executor)
        : cases_{ std::move(a_case) }, executor_{ an_executor } {}

    template <typename PrevSelectCasesTuple, typename SelectCaseT>
    explicit constexpr select(PrevSelectCasesTuple&& prev_cases, SelectCaseT&& a_case, executor& an_executor)
        : cases_{ std::tuple_cat(std::move(prev_cases), std::make_tuple(std::move(a_case))) },
          executor_{ an_executor } {}

    template <SomeSignal NextSignalT, typename NextF, typename NextCaseT = select_case<NextSignalT, NextF>>
        requires std::same_as<typename NextCaseT::value_type, value_type>
    constexpr auto case_(NextSignalT&& signal, NextF&& functor) && {
        return select<Atomic, ValueT, SelectCaseTs..., NextCaseT>{
            std::move(cases_),
            NextCaseT{ std::move(signal), std::move(functor) },
            executor_,
        };
    }

    template <SelectFunctorFor<default_case_signal> NextF>
    constexpr auto default_(NextF&& functor) && {
        return std::move(*this).case_(default_case_signal{ meta::unit{} }, std::move(functor));
    }

public: // SomeSignal
    select_connection_box<Atomic, ValueT, SelectCaseTs...> subscribe(slot<value_type, error_type>& slot) && {
        return select_connection_box<Atomic, ValueT, SelectCaseTs...>{ std::move(cases_), executor_, slot };
    }

    executor& get_executor() & { return executor_; }

private:
    std::tuple<SelectCaseTs...> cases_;
    executor& executor_;
};

template <template <typename> typename Atomic>
struct [[nodiscard]] select_start final {
    explicit constexpr select_start(executor& an_executor) : executor_{ an_executor } {}

    template <SomeSignal SomeSignalT, SelectFunctorFor<SomeSignalT> F>
    auto case_(SomeSignalT&& signal, F&& functor) {
        using case_type = select_case<SomeSignalT, F>;
        return select<Atomic, typename case_type::value_type, case_type>{
            case_type{ std::move(signal), std::move(functor) },
            executor_,
        };
    }

private:
    executor& executor_;
};

} // namespace detail

template <template <typename> typename Atomic>
constexpr auto select_(executor& an_executor = inline_executor()) {
    return detail::select_start<Atomic>{ an_executor };
}

constexpr auto select(executor& an_executor = inline_executor()) { return select_<detail::atomic>(an_executor); }

} // namespace sl::exec
