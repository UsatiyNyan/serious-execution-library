//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

namespace sl::exec {

struct cancel_mixin : meta::intrusive_forward_list_node<cancel_mixin> {
    virtual ~cancel_mixin() = default;

    virtual void setup_cancellation() & {}

    [[nodiscard]] virtual bool try_cancel() & {
        if (nullptr == intrusive_next) {
            return false;
        }
        return intrusive_next->downcast()->try_cancel();
    }
};

template <typename ValueT, typename ErrorT>
struct slot : cancel_mixin {
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
