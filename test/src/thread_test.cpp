//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread.hpp"
#include "sl/exec/thread/detail/multiword.hpp"
#include "sl/exec/thread/detail/multiword_dcss.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/tagged_ptr.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(thread, monolithicThreadPool) {
    monolithic_thread_pool background_executor{ thread_pool_config::with_hw_limit(1u) };
    const tl::optional<meta::result<std::thread::id, meta::undefined>> maybe_result =
        schedule(
            background_executor,
            [] -> meta::result<std::thread::id, meta::undefined> { return std::this_thread::get_id(); }
        )
        | get<default_event>();
    ASSERT_NE(*maybe_result, std::this_thread::get_id());
}

namespace detail {

TEST(threadDetail, taggedPtr) {
    struct test_struct {
        int value;
    };
    alignas(8) test_struct test_value;
    test_struct* ptr = &test_value;

    constexpr std::size_t TagWidth = 3;
    constexpr std::size_t MaxTag = (1 << TagWidth) - 1;

    static_assert(tagged_ptr<test_struct, std::uint8_t, TagWidth>::tag_mask == 0b111);

    // Test all valid tag values
    for (std::uint8_t tag = 0; tag <= MaxTag; ++tag) {
        auto tagged = tagged_ptr<test_struct, std::uint8_t, TagWidth>::make(ptr, tag);
        EXPECT_EQ(tagged.get_ptr(), ptr);
        EXPECT_EQ(tagged.get_tag(), tag);

        // Check raw restore
        auto raw = tagged.get_raw();
        auto restored = tagged_ptr<test_struct, std::uint8_t, TagWidth>::restore(raw);
        EXPECT_EQ(restored.get_ptr(), ptr);
        EXPECT_EQ(restored.get_tag(), tag);
    }

#if !defined(NDEBUG)
    test_struct test_value2;
    test_struct* misaligned = reinterpret_cast<test_struct*>(reinterpret_cast<std::uintptr_t>(&test_value2) | 0x1);
    EXPECT_DEATH({ std::ignore = tagged_ptr<test_struct>::make(misaligned, 1); }, ".*");
#endif
}

struct test_descriptor {
    // concept requirements
    static constexpr mw::pointer_type max_threads = 1;

    template <typename T>
    using atomic_type = detail::atomic<T>;

    struct immutables_type {
        int a;
        int b;
        bool operator==(const immutables_type&) const = default;
    };

    detail::atomic<mw::state_type> state{};
    immutables_type immutables{};

public: // test helpers
    static constexpr mw::state_type mask1 = 0x01;
    static constexpr mw::state_type mask2 = 0x02;
};

template <>
mw::descriptor_pool<test_descriptor>::descriptors_type //
    mw::descriptor_pool<test_descriptor>::descriptors{};

TEST(threadDetailMultiword, create) {
    const test_descriptor::immutables_type imm{ 42, 84 };
    const mw::state_type mut = 0x03; // Both bits set

    const mw::pointer_type dptr = mw::create_new<test_descriptor>(mut, imm);

    const auto [flag, pid, seq] = mw::pointer_traits<test_descriptor>::extract(dptr);
    const auto& desc = mw::descriptor_pool<test_descriptor>::get(pid);
    ASSERT_EQ(desc.immutables, imm);

    const auto opt_mut = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_TRUE(opt_mut.has_value());
    ASSERT_EQ(opt_mut.value(), mut);

    const auto res_imm = mw::read_immutables<test_descriptor>(dptr);
    ASSERT_TRUE(res_imm.has_value());
    ASSERT_EQ(res_imm.value(), imm);
}

TEST(threadDetailMultiword, readMutablesInvalid) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x01, { 1, 2 });

    // invalidate sequence
    std::ignore = mw::create_new<test_descriptor>(0x00, { 42, 84 });

    const auto result = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_FALSE(result.has_value());
}

TEST(threadDetailMultiword, readImmutablesInvalid) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x01, { 42, 99 });

    // invalidate sequence
    std::ignore = mw::create_new<test_descriptor>(0x02, { 11, 22 });

    const auto result = mw::read_immutables<test_descriptor>(dptr);
    ASSERT_FALSE(result.has_value());
}

TEST(threadDetailMultiword, writeMutableOne) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x00, { 0, 0 });

    const int expected_value = 0x01;
    mw::write_mutable<test_descriptor, test_descriptor::mask1>(dptr, expected_value);
    const auto result = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), expected_value);
}

TEST(threadDetailMultiword, writeMutableMany) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x00, { 10, 20 });

    mw::write_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x01);
    mw::write_mutable<test_descriptor, test_descriptor::mask2>(dptr, 0x02);

    const auto result = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0x03); // 0x01 | 0x02
}

TEST(threadDetailMultiword, writeMutableOverwrite) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x00, { 10, 20 });

    mw::write_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x01);
    mw::write_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x00);

    const auto result = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0x00);
}

TEST(threadDetailMultiword, casMutableSuccess) {
    const int expected_value = 0x00;
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(expected_value, { 0, 0 });

    const int new_value = 0x01;
    const auto result = mw::cas_mutable<test_descriptor, test_descriptor::mask1>(dptr, expected_value, new_value);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0x01);
}

TEST(threadDetailMultiword, casMutableFailExpected) {
    const int old_value = 0x01;
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(old_value, { 0, 0 });

    const int expected_value = 0x00;
    const int new_value = 0x02;
    const auto result = mw::cas_mutable < test_descriptor,
               test_descriptor::mask1 | test_descriptor::mask2 > (dptr, expected_value, new_value);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), old_value) << dptr;
}

TEST(threadDetailMultiword, casMutableFailSequence) {
    const int expected_value = 0x00;
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(expected_value, { 0, 0 });

    // invalidate sequence
    std::ignore = mw::create_new<test_descriptor>(expected_value, { 0, 0 });

    const int new_value = 0x01;
    const auto result = mw::cas_mutable<test_descriptor, test_descriptor::mask1>(dptr, expected_value, new_value);

    ASSERT_FALSE(result.has_value());
}

TEST(threadDetailMultiword, casMutableOverwrite) {
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x00, { 0, 0 });

    ASSERT_TRUE((mw::cas_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x00, 0x01).has_value()));
    ASSERT_TRUE((mw::cas_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x01, 0x00).has_value()));

    const auto result = mw::read_mutables<test_descriptor>(dptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), 0x00);
}

TEST(threadDetailMultiword, immutablesSurviveWrites) {
    test_descriptor::immutables_type imm = { 111, 222 };
    const mw::pointer_type dptr = mw::create_new<test_descriptor>(0x00, imm);

    mw::write_mutable<test_descriptor, test_descriptor::mask1>(dptr, 0x01);
    mw::write_mutable<test_descriptor, test_descriptor::mask2>(dptr, 0x02);

    const auto result = mw::read_immutables<test_descriptor>(dptr);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), imm);
}

TEST(threadDetailMultiwordPointer, combineExtractRoundTrip) {
    using traits = mw::pointer_traits<test_descriptor>;

    // Get the bit widths
    constexpr std::size_t flag_bits = traits::flag_width;
    constexpr std::size_t pid_bits = traits::pid_width;
    constexpr std::size_t seq_bits = traits::sequence_width;

    // Create values within valid bit ranges
    const mw::pointer_type flag = (1ULL << (flag_bits - 1)) | 0x5A5A;
    const mw::pointer_type pid = (1ULL << (pid_bits - 1)) | 0x1234;
    const mw::pointer_type seq = (1ULL << (seq_bits - 1)) | 0xDEADBEEF;

    const mw::pointer_type packed = traits::combine(flag, pid, seq);
    const auto [out_flag, out_pid, out_seq] = traits::extract(packed);

    // Round-trip should match input values after masking
    ASSERT_EQ(out_flag, flag & traits::flag_mask);
    ASSERT_EQ(out_pid, pid & traits::pid_mask);
    ASSERT_EQ(out_seq, seq & traits::sequence_mask);
}

TEST(threadDetailMultiwordPointer, maskOverflowBits) {
    using traits = mw::pointer_traits<test_descriptor>;

    // Overflow all fields by 1 bit
    const mw::pointer_type flag = traits::flag_mask + 1;
    const mw::pointer_type pid = traits::pid_mask + 1;
    const mw::pointer_type seq = traits::sequence_mask + 1;

    const mw::pointer_type packed = traits::combine(flag, pid, seq);
    const auto [out_flag, out_pid, out_seq] = traits::extract(packed);

    // Should be masked down
    ASSERT_EQ(out_flag, 0);
    ASSERT_EQ(out_pid, 0);
    ASSERT_EQ(out_seq, 0);
}

TEST(threadDetailMultiwordPointer, allZeros) {
    using traits = mw::pointer_traits<test_descriptor>;

    const mw::pointer_type packed = traits::combine(0, 0, 0);
    const auto [flag, pid, seq] = traits::extract(packed);

    ASSERT_EQ(flag, 0);
    ASSERT_EQ(pid, 0);
    ASSERT_EQ(seq, 0);
}

TEST(threadDetailMultiwordPointer, allOnesMaskedProperly) {
    using traits = mw::pointer_traits<test_descriptor>;

    const mw::pointer_type all_ones = ~static_cast<mw::pointer_type>(0);

    const mw::pointer_type packed = traits::combine(all_ones, all_ones, all_ones);
    const auto [flag, pid, seq] = traits::extract(packed);

    ASSERT_EQ(flag, traits::flag_mask);
    ASSERT_EQ(pid, traits::pid_mask);
    ASSERT_EQ(seq, traits::sequence_mask);
}

TEST(threadDetailMultiwordDcss, succeedsWhenBothExpectedMatch) {
    atomic<std::uintptr_t> a1{ 0x1111 };
    atomic<std::uintptr_t> a2{ 0x2222 };

    const std::uintptr_t result = dcss<std::uintptr_t>(a1, 0x1111, a2, 0x2222, 0xDEAD);

    ASSERT_EQ(result, 0x2222); // returns old a2
    ASSERT_EQ(a2.load(), 0xDEAD); // a2 is updated
}

TEST(threadDetailMultiwordDcss, failsWhenA1DoesNotMatch) {
    atomic<std::uintptr_t> a1{ 0xAAAA }; // != e1
    atomic<std::uintptr_t> a2{ 0x2222 }; // == e2

    const std::uintptr_t result = dcss<std::uintptr_t>(a1, 0x1111, a2, 0x2222, 0xDEAD);

    ASSERT_EQ(result, 0x2222); // returns unchanged a2
    ASSERT_EQ(a2.load(), 0x2222); // a2 remains the same
}

TEST(threadDetailMultiwordDcss, failsWhenA2DoesNotMatch) {
    atomic<std::uintptr_t> a1{ 0x1111 }; // == e1
    atomic<std::uintptr_t> a2{ 0x9999 }; // != e2

    const std::uintptr_t result = dcss<std::uintptr_t>(a1, 0x1111, a2, 0x2222, 0xDEAD);

    ASSERT_EQ(result, 0x9999); // returns actual a2
    ASSERT_EQ(a2.load(), 0x9999); // a2 unchanged
}

TEST(threadDetailMultiwordDcss, flagSetAndCheckConsistency) {
    using traits = mw::pointer_traits<dcss_descriptor>;

    constexpr mw::pointer_type pid = 0x1234;
    constexpr mw::pointer_type seq = 0xBEEF;
    constexpr mw::pointer_type base_flag = 0x0;

    mw::pointer_type des = traits::combine(base_flag, pid, seq);
    ASSERT_FALSE(mw::has_flag<dcss_descriptor>(des));

    mw::pointer_type flagged = mw::set_flag<dcss_descriptor>(des);
    ASSERT_TRUE(mw::has_flag<dcss_descriptor>(flagged));

    mw::pointer_type cleared = mw::unset_flag<dcss_descriptor>(flagged);
    ASSERT_FALSE(mw::has_flag<dcss_descriptor>(cleared));
}

TEST(threadDetailMultiwordDcss, descriptorInvariantPreserved) {
    using traits = mw::pointer_traits<dcss_descriptor>;

    constexpr mw::pointer_type flag = 0x5A;
    constexpr mw::pointer_type pid = 0x4321;
    constexpr mw::pointer_type seq = 0xCAFEBABE;

    mw::pointer_type combined = traits::combine(flag, pid, seq);
    auto [f, p, s] = traits::extract(combined);

    ASSERT_EQ(f, flag & traits::flag_mask);
    ASSERT_EQ(p, pid & traits::pid_mask);
    ASSERT_EQ(s, seq & traits::sequence_mask);
}

TEST(threadDetailMultiwordDcss, readSimple) {
    int value = 0x4343;
    detail::atomic<int*> ptr{ &value };
    int* ptr_read = dcss_read<int*>(ptr);
    ASSERT_EQ(ptr_read, &value);
    ASSERT_EQ(*ptr_read, 0x4343);
    ASSERT_EQ(value, 0x4343);
}

TEST(threadDetailMultiwordDcss, readHelps) {
    std::uintptr_t e1{ 0xAAAA }, e2{ 0xBBBB }, n2{ 0xCCCC };
    detail::atomic<std::uintptr_t> a1{ e1 }, a2{ e2 };

    const mw::pointer_type des =
        dcss_create_new(std::bit_cast<std::uintptr_t>(&a1), dcss_a1_load_default<std::uintptr_t>, e1, &a2, e2, n2);
    const mw::pointer_type fdes = mw::set_flag<dcss_descriptor>(des);
    a2.store(fdes, std::memory_order::relaxed);

    const auto result = dcss_read<std::uintptr_t>(a2);
    ASSERT_FALSE(mw::has_flag<dcss_descriptor>(result));
    ASSERT_EQ(result, n2);
}

TEST(threadDetailKcas, readSimplePointer) {
    int value = 1234;
    detail::atomic<int*> a{ &value };
    int* const result = kcas_read(a);
    ASSERT_EQ(result, &value);
    ASSERT_EQ(*result, 1234);
}

TEST(threadDetailKcas, singleSuccessfulKcas) {
    int old_value = 1234;
    int new_value = 4321;
    detail::atomic<int*> a{ &old_value };
    const bool success = kcas(kcas_arg{ a, &old_value, &new_value });
    ASSERT_TRUE(success);
    ASSERT_EQ(a.load(), &new_value);
    ASSERT_EQ(4321, new_value);
}

TEST(threadDetailKcas, singleFailedKcas) {
    int old_value = 1234;
    int incorrect = 9999;
    int new_value = 4321;
    detail::atomic<int*> a{ &old_value };
    const bool success = kcas(kcas_arg{ a, &incorrect, &new_value });
    ASSERT_FALSE(success);
    ASSERT_EQ(a.load(), &old_value);
    ASSERT_EQ(1234, old_value);
}


TEST(threadDetailKcas, multiKcasSuccess) {
    int a_val = 100, b_val = 200;
    int a_new = 101, b_new = 201;
    detail::atomic<int*> a{ &a_val };
    detail::atomic<int*> b{ &b_val };
    const bool success = kcas(kcas_arg{ a, &a_val, &a_new }, kcas_arg{ b, &b_val, &b_new });
    ASSERT_TRUE(success);
    ASSERT_EQ(a.load(), &a_new);
    ASSERT_EQ(b.load(), &b_new);
    ASSERT_EQ(*a.load(), 101);
    ASSERT_EQ(*b.load(), 201);
}

TEST(threadDetailKcas, multiKcasFailure) {
    int a_val = 300, b_val = 400;
    int a_new = 301, b_new = 401;
    int incorrect = 999;
    detail::atomic<int*> a{ &a_val };
    detail::atomic<int*> b{ &b_val };
    const bool success = kcas(kcas_arg{ a, &a_val, &a_new }, kcas_arg{ b, &incorrect, &b_new });
    ASSERT_FALSE(success);
    ASSERT_EQ(a.load(), &a_val);
    ASSERT_EQ(b.load(), &b_val);
    ASSERT_EQ(*a.load(), 300);
    ASSERT_EQ(*b.load(), 400);
}

} // namespace detail
} // namespace sl::exec
