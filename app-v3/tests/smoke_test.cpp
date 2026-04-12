// Phase 0 smoke test. Proves Catch2 is wired into the build and CI runs
// tests. Real test suites arrive in Phase 1 (engine) and Phase 2 (session).

#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: Catch2 is linked and CTest discovers us", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}
