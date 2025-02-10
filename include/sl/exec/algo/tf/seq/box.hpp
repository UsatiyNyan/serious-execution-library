//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct box_storage_base {
    virtual ~box_storage_base() = default;

    virtual void subscribe(slot<ValueT, ErrorT>&) & = 0;
    virtual executor& get_executor() & = 0;
    virtual void emit() & = 0;
};

template <
    Signal SignalT,
    typename ValueT = typename SignalT::value_type,
    typename ErrorT = typename SignalT::error_type>
struct box_storage final : box_storage_base<ValueT, ErrorT> {
    explicit box_storage(SignalT&& signal) : signal_{ std::move(signal) } {}

    void subscribe(slot<ValueT, ErrorT>& slot) & override { connection_.emplace(std::move(signal_).subscribe(slot)); }
    executor& get_executor() & override { return signal_.get_executor(); }

private:
    SignalT signal_;
    tl::optional<ConnectionFor<SignalT>> connection_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] box_connection {
    box_connection(std::unique_ptr<detail::box_storage_base<ValueT, ErrorT>> storage, slot<ValueT, ErrorT>& slot)
        : storage_{ std::move(storage) } {
        storage_->subscribe(slot);
    }

    void emit() & { storage_->emit(); }

private:
    std::unique_ptr<detail::box_storage_base<ValueT, ErrorT>> storage_;
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] box_signal {
    using value_type = ValueT;
    using error_type = ErrorT;

    template <Signal SignalT>
    constexpr box_signal(SignalT&& signal)
        : storage_{ std::make_unique<detail::box_storage<SignalT>>(std::move(signal)) } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return box_connection<value_type, error_type>{ std::move(storage_), slot };
    }
    executor& get_executor() { return storage_->get_executor(); }

private:
    std::unique_ptr<detail::box_storage_base<value_type, error_type>> storage_;
};

template <Signal SignalT>
box_signal(SignalT&& signal) -> box_signal<typename SignalT::value_type, typename SignalT::error_type>;

namespace detail {

struct [[nodiscard]] box {
    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return box_signal{ std::move(signal) };
    }
};

} // namespace detail

constexpr auto box() { return detail::box{}; }

} // namespace sl::exec
