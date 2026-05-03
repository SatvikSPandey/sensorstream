# ── Test: Ring Buffer ──────────────────────────────────────────────────────
add_executable(test_ring_buffer
    test_ring_buffer.cpp
)

target_include_directories(test_ring_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(test_ring_buffer PRIVATE
    GTest::gtest_main
    Threads::Threads
)

if(WIN32)
    target_link_libraries(test_ring_buffer PRIVATE ws2_32)
endif()

target_compile_options(test_ring_buffer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ── Test: Statistics ──────────────────────────────────────────────────────
add_executable(test_statistics
    test_statistics.cpp
)

target_include_directories(test_statistics PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_statistics PRIVATE
    GTest::gtest_main
    Threads::Threads
)

target_compile_options(test_statistics PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ── Test: Data Store ──────────────────────────────────────────────────────
add_executable(test_data_store
    test_data_store.cpp
    ${CMAKE_SOURCE_DIR}/src/data_store.cpp
)

target_include_directories(test_data_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(test_data_store PRIVATE
    GTest::gtest_main
    Threads::Threads
)

if(WIN32)
    target_link_libraries(test_data_store PRIVATE ws2_32)
endif()

target_compile_options(test_data_store PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ── Register with CTest ────────────────────────────────────────────────────
include(GoogleTest)
gtest_discover_tests(test_ring_buffer)
gtest_discover_tests(test_statistics)
gtest_discover_tests(test_data_store)
