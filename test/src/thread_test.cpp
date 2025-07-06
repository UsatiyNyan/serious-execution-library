//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread.hpp"
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

TEST(threadDetail, taggedPtr) {
    struct test_struct {
        int value;
    };
    alignas(8) test_struct test_value;
    test_struct* ptr = &test_value;

    constexpr std::size_t TagWidth = 3;
    constexpr std::size_t MaxTag = (1 << TagWidth) - 1;

    static_assert(detail::tagged_ptr<test_struct, std::uint8_t, TagWidth>::tag_mask == 0b111);

    // Test all valid tag values
    for (std::uint8_t tag = 0; tag <= MaxTag; ++tag) {
        auto tagged = detail::tagged_ptr<test_struct, std::uint8_t, TagWidth>::make(ptr, tag);
        EXPECT_EQ(tagged.get_ptr(), ptr);
        EXPECT_EQ(tagged.get_tag(), tag);

        // Check raw restore
        auto raw = tagged.get_raw();
        auto restored = detail::tagged_ptr<test_struct, std::uint8_t, TagWidth>::restore(raw);
        EXPECT_EQ(restored.get_ptr(), ptr);
        EXPECT_EQ(restored.get_tag(), tag);
    }

#if !defined(NDEBUG)
    test_struct test_value2;
    test_struct* misaligned = reinterpret_cast<test_struct*>(reinterpret_cast<std::uintptr_t>(&test_value2) | 0x1);
    EXPECT_DEATH({ std::ignore = detail::tagged_ptr<test_struct>::make(misaligned, 1); }, ".*");
#endif
}

} // namespace sl::exec
