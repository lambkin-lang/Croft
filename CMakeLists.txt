cmake_minimum_required(VERSION 3.15)
project(croft C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Feature flags (future tiers default OFF until implemented) ──────────
option(CROFT_ENABLE_UI            "Tier 5-7: Windowing, rendering, scene graph" OFF)
option(CROFT_ENABLE_WASM          "Tier 4: Wasm embedding"                      OFF)
option(CROFT_ENABLE_AUDIO         "Tier 8: Audio (miniaudio)"                   OFF)
option(CROFT_ENABLE_ACCESSIBILITY "Tier 9: OS accessibility bridges"            OFF)
option(CROFT_BUILD_TESTS          "Build the test suite"                        ON)

# ── Tier 0: Foundation (always built) ───────────────────────────────────
set(TIER0_SOURCES
    src/host/host_log.c
    src/host/host_time.c
    src/host/host_thread.c
)

add_library(croft STATIC ${TIER0_SOURCES})

target_include_directories(croft PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(croft PUBLIC Threads::Threads)

# ── Tests ───────────────────────────────────────────────────────────────
if(CROFT_BUILD_TESTS)
    enable_testing()

    add_executable(test_croft tests/test_main.c)
    target_link_libraries(test_croft PRIVATE croft)
    add_test(NAME croft_tests COMMAND test_croft)
endif()
