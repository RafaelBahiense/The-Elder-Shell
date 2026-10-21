#include <doctest/doctest.h>
#include "vm/StateFunctionLayout.h"
#include <array>

using namespace elder_shell;

namespace {
    TEST_CASE("Named-state offsets preserve high bits independently of function count") {
        const auto layout = StateFunctionLayout::Decode(0x79014);
        CHECK(layout.count == 20);
        CHECK(layout.offset == 968);
        for (const std::uint32_t offset : {16u, 504u, 512u, 968u, 8192u, 0x7FFFF8u}) {
            for (const std::uint32_t count : {0u, 1u, 20u, 511u}) {
                const auto decoded = StateFunctionLayout::Decode((offset << 9) | count);
                CHECK(decoded.count == count);
                CHECK(decoded.offset == offset);
            }
        }
    }

    TEST_CASE("Named-state ranges reject truncated offsets into another state record") {
        // 40 state headers precede the function table. The old 9-bit offset
        // turns 968 into 456, which lands in those headers instead of functions.
        const auto valid = StateFunctionLayout::Decode(0x79014);
        CHECK(valid.Position(0, 40, 80) == 968);
        CHECK_FALSE((StateFunctionLayout{20, 456}.Position(0, 40, 80)));
        CHECK_FALSE((StateFunctionLayout{20, 969}.Position(0, 40, 80)));
        CHECK_FALSE((StateFunctionLayout{40, 1200}.Position(0, 40, 80)));
        CHECK_FALSE(valid.Position(40, 40, 80));
    }

    TEST_CASE("Named-state function ranges allow metadata before the function array") {
        // Fixture with ten headers, 36 bytes of additional
        // metadata, then 34 functions. Offsets are relative to each header.
        constexpr std::array counts{3u, 3u, 2u, 2u, 3u, 4u, 9u, 3u, 3u, 2u};
        constexpr std::array offsets{196u, 204u, 212u, 212u, 212u, 220u, 236u, 292u, 300u, 308u};
        std::size_t expected = 196;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            const StateFunctionLayout layout{counts[i], offsets[i]};
            CHECK(layout.Position(i, counts.size(), 34, 196) == expected);
            expected += counts[i] * 8;
        }
        CHECK(expected == 468);
        CHECK_FALSE((StateFunctionLayout{3, 197}.Position(0, 10, 34, 196)));
        CHECK_FALSE((StateFunctionLayout{3, 156}.Position(0, 10, 34, 156)));
        CHECK_FALSE((StateFunctionLayout{3, 308}.Position(9, 10, 34, 196)));
    }

}
