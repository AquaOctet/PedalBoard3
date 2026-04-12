# Phase 0: one sanity-check test that ensures Catch2 is wired up.
# Phase 1+ replaces this with real engine / session / control tests.

add_executable(pb3_smoke_tests
    smoke_test.cpp
)

target_link_libraries(pb3_smoke_tests PRIVATE
    Catch2::Catch2WithMain
    pb3_warnings
)

target_include_directories(pb3_smoke_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

catch_discover_tests(pb3_smoke_tests)
