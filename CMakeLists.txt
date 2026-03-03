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

# ── Tier 1: Messaging ───────────────────────────────────────────────────
set(TIER1_SOURCES
    src/host/host_queue.c
)

add_library(croft STATIC ${TIER0_SOURCES} ${TIER1_SOURCES})

# ── WIT Schema Codegen ──────────────────────────────────────────────────
set(WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.h")
set(WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.c")

set(TEST_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.h")
set(TEST_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.c")

add_executable(wit_codegen tools/wit_codegen.c)

# Main runtime schema
add_custom_command(
    OUTPUT ${WIT_GENERATED_H} ${WIT_GENERATED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/runtime-schema.wit"
                        --header "${WIT_GENERATED_H}" --source "${WIT_GENERATED_C}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/runtime-schema.wit"
    COMMENT "Generating Schema C Layouts from WIT..."
)
add_custom_target(wit_schema_gen DEPENDS ${WIT_GENERATED_H} ${WIT_GENERATED_C})

# Test fixture schema
add_custom_command(
    OUTPUT ${TEST_WIT_GENERATED_H} ${TEST_WIT_GENERATED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/result-test.wit"
                        --header "${TEST_WIT_GENERATED_H}" --source "${TEST_WIT_GENERATED_C}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/result-test.wit"
    COMMENT "Generating Test Schema C Layouts from WIT..."
)
add_custom_target(test_wit_schema_gen DEPENDS ${TEST_WIT_GENERATED_H} ${TEST_WIT_GENERATED_C})

# ── Library: Sapling ────────────────────────────────────────────────────
file(GLOB SAPLING_SOURCES "src/sapling/*.c" "src/runner/*.c" "src/wasi/*.c")

add_library(sapling STATIC ${SAPLING_SOURCES} ${WIT_GENERATED_C})
add_dependencies(sapling wit_schema_gen)
target_include_directories(sapling PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(croft PUBLIC sapling)
target_compile_definitions(sapling PUBLIC SAPLING_PAGE_SIZE=4096 SAPLING_THREADED)

if(CROFT_BUILD_TESTS)
    target_compile_definitions(sapling PUBLIC SAPLING_HAMT_TESTING)
endif()

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

    add_executable(test_croft tests/test_main.c tests/test_queue.c)
    target_include_directories(test_croft PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    target_link_libraries(test_croft PRIVATE croft)
    add_test(NAME croft_tests COMMAND test_croft)

    # Sapling test suite (each file has its own main)
    file(GLOB SAPLING_UNIT_TESTS "tests/sapling_tests/unit/*.c" "tests/sapling_tests/integration/*.c")
    list(REMOVE_ITEM SAPLING_UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/unit/wasm_guest_example.c")
    foreach(test_src IN LISTS SAPLING_UNIT_TESTS)
        get_filename_component(test_name ${test_src} NAME_WE)
        
        # Avoid creating executable targets for helper files that don't have main
        # But wait, we checked via grep and nearly all of them have main!
        # If any fail to link, we might need to blacklist them.
        add_executable(${test_name} ${test_src})
        target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        target_link_libraries(${test_name} PRIVATE sapling)
        target_compile_definitions(${test_name} PRIVATE SAPLING_HAMT_TESTING SAPLING_THREADED)
        # Provide the generated headers for test_wit_thatch_codegen
        if(test_name STREQUAL "test_wit_thatch_codegen")
            add_dependencies(${test_name} test_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()

        add_test(NAME sapling_${test_name} COMMAND ${test_name})
    endforeach()
endif()
