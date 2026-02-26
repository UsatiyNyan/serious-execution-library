//
// Created by usatiynyan.
//

#include "sl/exec/coro/coroutine.hpp"
#include "sl/exec/model/task.hpp"
#include "sl/exec/sim.hpp"

#include <gtest/gtest.h>
#include <sl/meta/lifetime/defer.hpp>
#include <utility>

namespace sl::exec::sim {

TEST(stack, allocateZeroBytesReturnsInvalidArgument) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 0 });
    ASSERT_FALSE(stack_allocate_result);
    EXPECT_EQ(stack_allocate_result.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(stack, deallocateSucceeds) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto err = stack_allocate_result->deallocate();
    EXPECT_FALSE(err);
}

TEST(stack, userPageIsReadWrite) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto& a_stack = *stack_allocate_result;
    meta::defer dealloc{ [&] { std::ignore = a_stack.deallocate(); } };
    auto user_page = a_stack.user_page();
    user_page[0] = std::byte{ 0xAB };
    EXPECT_EQ(user_page[0], std::byte{ 0xAB });
}

TEST(stack, allocateSizeIsPageAligned) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    const auto page_size = platform_result->page_size;
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto& a_stack = *stack_allocate_result;
    meta::defer dealloc{ [&] { std::ignore = a_stack.deallocate(); } };
    EXPECT_EQ(a_stack.user_page().size_bytes() % page_size, 0);
}

TEST(stack, allocateRoundsUpToPageSize) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    const auto page_size = platform_result->page_size;
    // request 1 byte more than a page — should round up to 2 user pages + 1 guard
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = page_size + 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto& a_stack = *stack_allocate_result;
    meta::defer dealloc{ [&] { std::ignore = a_stack.deallocate(); } };
    EXPECT_EQ(a_stack.user_page().size_bytes(), page_size * 2);
#ifdef SL_EXEC_SIM_STACK_IS_mmap
    EXPECT_EQ(a_stack.unsafe_prot_page().size_bytes(), page_size);
#else
    EXPECT_EQ(a_stack.unsafe_prot_page().size_bytes(), 0);
#endif
}

TEST(stack, doubleDeallocateReturnsInvalidArgument) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    EXPECT_FALSE(stack_allocate_result->deallocate());
    EXPECT_EQ(stack_allocate_result->deallocate(), std::make_error_code(std::errc::invalid_argument));
}

TEST(stack, allocateDeallocateAllocateSucceeds) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto first = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(first);
    ASSERT_FALSE(first->deallocate());
    auto second = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    EXPECT_TRUE(second);
    ASSERT_FALSE(second->deallocate());
}

TEST(stack, writtenDataPersistsBeforeDeallocate) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());
    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto& a_stack = *stack_allocate_result;
    meta::defer dealloc{ [&] { std::ignore = a_stack.deallocate(); } };
    auto user_page = a_stack.user_page();
    for (std::size_t i = 0; i < user_page.size(); ++i) {
        user_page[i] = std::byte{ static_cast<uint8_t>(i & 0xFF) };
    }
    for (std::size_t i = 0; i < user_page.size(); ++i) {
        EXPECT_EQ(user_page[i], std::byte{ static_cast<uint8_t>(i & 0xFF) });
    }
}

#ifdef SL_EXEC_SIM_STACK_IS_mmap
TEST(stack, guardPageCausesSegfault) {
    auto platform_result = platform::make();
    ASSERT_TRUE(platform_result.has_value());

    auto stack_allocate_result = stack::allocate(*platform_result, { .at_least_bytes = 1 });
    ASSERT_TRUE(stack_allocate_result);
    auto& a_stack = *stack_allocate_result;
    meta::defer dealloc{ [&] { std::ignore = a_stack.deallocate(); } };
    EXPECT_DEATH({ a_stack.unsafe_prot_page()[0] = std::byte{ 0 }; }, "");
}
#endif

template <typename F>
struct coroutine : task {
    void resume() {
        ASSERT(!is_done_);
        caller_context_.switch_to(callee_context_);
    }
    bool is_done() const { return is_done_; }

public:
    struct handle {
        void suspend() { self->suspend_impl(); }

    public:
        coroutine* self;
    };

private:
    void suspend_impl() { callee_context_.switch_to(caller_context_); }

public: // task
    void execute() noexcept override {
        try {
            std::cout << "f" << std::endl;
            f_(handle{ this });
        } catch (...) {
            PANIC("exception");
        }

        is_done_ = true;
        suspend_impl();
        std::unreachable();
    }
    void cancel() noexcept override { PANIC("cancel never should be called"); }

public:
    coroutine(stack& s, F f) : f_{ std::move(f) }, callee_context_{ machine_context::setup(s, *this) } {}

private:
    F f_;
    machine_context callee_context_{};
    machine_context caller_context_{};
    bool is_done_ = false;
};

TEST(context, coroutineInitAndSwitch) {
    std::cout << "platform" << std::endl;
    auto plat = *ASSERT_VAL(platform::make());

    std::cout << "stack" << std::endl;
    auto s = *ASSERT_VAL(stack::allocate(plat, { .at_least_bytes = 64 * 1024 }));
    meta::defer s_dealloc{ [&s] { ASSERT(s.deallocate() == std::error_code{}); } };

    std::cout << "coroutine" << std::endl;
    std::size_t counter = 0;
    coroutine coro{
        s,
        [&counter](auto h) {
            ++counter;
            h.suspend();
            ++counter;
        },
    };
    EXPECT_EQ(counter, 0);
    EXPECT_FALSE(coro.is_done());

    std::cout << "resume 1" << std::endl;
    coro.resume();
    EXPECT_EQ(counter, 1);
    EXPECT_FALSE(coro.is_done());

    std::cout << "resume 2" << std::endl;
    coro.resume();
    EXPECT_EQ(counter, 2);
    EXPECT_TRUE(coro.is_done());
}

} // namespace sl::exec::sim
