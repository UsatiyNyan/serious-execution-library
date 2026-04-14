//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/force.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/traits/unique.hpp>

namespace sl::exec {
namespace detail {

template <typename V, typename E>
struct [[nodiscard]] promise_signal final {
    template <SlotCtor<V, E> SlotCtorT>
    struct promise_callback final : slot_callback<V, E> {
        explicit promise_callback(SlotCtorT slot_ctor) : slot_(std::move(slot_ctor)()) {}

        void set_result(meta::maybe<meta::result<V, E>>&& maybe_result) && noexcept override {
            fulfill_slot(std::move(slot_), std::move(maybe_result));
        }

    private:
        SlotFrom<SlotCtorT> slot_;
    };

    using value_type = V;
    using error_type = E;

public:
    slot_callback<V, E>** callback;

public:
    template <SlotCtor<V, E> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        *callback = new promise_callback<SlotCtorT>{ std::move(slot_ctor) };
        return dummy_connection{};
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

} // namespace detail

template <typename V, typename E>
struct [[nodiscard]] promise : meta::finalizer<promise<V, E>> {
    using result_type = meta::result<V, E>;

    explicit promise(slot_callback<V, E>* a_callback)
        : meta::finalizer<promise>{ [](promise& self) noexcept {
              if (auto* const a_callback = std::exchange(self.callback_, nullptr); a_callback != nullptr) {
                  std::move(*a_callback).set_result(meta::null);
                  delete a_callback;
              }
          } },
          callback_{ a_callback } {
        DEBUG_ASSERT(callback_ != nullptr);
    }

    void set_value(V&& value) && noexcept {
        std::move(*this).set_result(result_type{ meta::ok_tag, std::move(value) });
    }
    void set_error(E&& error) && noexcept {
        std::move(*this).set_result(result_type{ meta::err_tag, std::move(error) });
    }
    void set_null() && noexcept { std::move(*this).set_result(meta::null); }
    void set_result(meta::maybe<result_type>&& maybe_result) && noexcept {
        auto* const callback = std::exchange(callback_, nullptr);
        if (ASSERT_VAL(callback != nullptr)) {
            std::move(*callback).set_result(std::move(maybe_result));
            delete callback;
        }
    }

private:
    slot_callback<V, E>* callback_;
};

template <typename V, typename E, template <typename> typename Atomic = detail::atomic>
struct contract {
    using future_type = detail::force_signal<detail::promise_signal<V, E>, Atomic>;
    using promise_type = promise<V, E>;

    future_type f;
    promise_type p;
};

template <typename V, typename E, template <typename> typename Atomic = detail::atomic>
contract<V, E, Atomic> make_contract() {
    slot_callback<V, E>* promise_callback = nullptr;
    auto signal = force<Atomic>()(detail::promise_signal<V, E>{ .callback = &promise_callback });
    return contract<V, E, Atomic>{
        .f = std::move(signal),
        .p{ promise_callback },
    };
}

} // namespace sl::exec
