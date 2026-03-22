//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/traits/unique.hpp>

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

} // namespace sl::exec
