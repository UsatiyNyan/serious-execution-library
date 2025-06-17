//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

#include <utility>

namespace sl::exec {

template <typename ValueT, typename ErrorT>
struct slot {
    virtual ~slot() = default;

    virtual void set_value(ValueT&&) & = 0;
    virtual void set_error(ErrorT&&) & = 0;
    virtual void set_null() & = 0;
};

template <typename ValueT, typename ErrorT>
struct dummy_slot final : slot<ValueT, ErrorT> {
    explicit dummy_slot() = default;

    void set_value(ValueT&&) & override {}
    void set_error(ErrorT&&) & override {}
    void set_null() & override {}
};

template <typename ValueT, typename ErrorT>
struct slot_node
    : slot<ValueT, ErrorT>
    , meta::intrusive_forward_list_node<slot_node<ValueT, ErrorT>> {

    constexpr explicit slot_node(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}

    void set_value(ValueT&& value) & override { slot_.set_value(std::move(value)); }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void set_null() & override { slot_.set_null(); }

private:
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT>
using slot_list = meta::intrusive_forward_list<slot_node<ValueT, ErrorT>>;

} // namespace sl::exec
