cmake_minimum_required(VERSION 3.15)
project(croft C CXX OBJC OBJCXX)

include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CROFT_BUILD_TESTS "Build the test suite" ON)
option(CROFT_FAIL_ON_MISSING_OPTIONAL_DEPS
       "Treat unresolved optional subsystem dependencies as configure errors" OFF)
option(CROFT_FETCH_WASM3 "Fetch wasm3 with FetchContent when no local/source install is provided" ON)
option(CROFT_FETCH_MINIAUDIO
       "Fetch miniaudio with FetchContent when no local header path is provided" ON)

set(CROFT_GLFW_SOURCE_DIR "" CACHE PATH "Path to a local GLFW checkout")
set(CROFT_GLFW_ROOT "/opt/homebrew/Cellar/glfw/3.4" CACHE PATH
    "Path to an installed GLFW prefix")
set(CROFT_TGFX_SOURCE_DIR "/Users/mshonle/Projects/Tencent/tgfx" CACHE PATH
    "Path to a local tgfx checkout")
set(CROFT_WASM3_SOURCE_DIR "" CACHE PATH "Path to a local wasm3 checkout")
set(CROFT_WASM3_GIT_REPOSITORY "https://github.com/wasm3/wasm3.git" CACHE STRING
    "Git repository used when FetchContent downloads wasm3")
set(CROFT_WASM3_GIT_TAG "main" CACHE STRING
    "Git ref used when FetchContent downloads wasm3")
set(CROFT_WABT_SOURCE_DIR "" CACHE PATH "Path to a local WABT checkout")
set(CROFT_MINIAUDIO_SOURCE_DIR "" CACHE PATH "Path to the directory containing miniaudio.h")
set(CROFT_MINIAUDIO_GIT_REPOSITORY "https://github.com/mackron/miniaudio.git" CACHE STRING
    "Git repository used when FetchContent downloads miniaudio")
set(CROFT_MINIAUDIO_GIT_TAG "master" CACHE STRING
    "Git ref used when FetchContent downloads miniaudio")

function(croft_optional_missing label reason)
    if(CROFT_FAIL_ON_MISSING_OPTIONAL_DEPS)
        message(FATAL_ERROR "${label}: ${reason}")
    endif()
    message(STATUS "Croft optional artifact skipped: ${label} (${reason})")
endfunction()

function(croft_apply_public_includes target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endfunction()

function(croft_json_array out_var)
    set(items ${ARGN})
    if(NOT items)
        set(${out_var} "[]" PARENT_SCOPE)
        return()
    endif()

    set(parts "")
    foreach(item IN LISTS items)
        string(REPLACE "\\" "\\\\" escaped "${item}")
        string(REPLACE "\"" "\\\"" escaped "${escaped}")
        list(APPEND parts "\"${escaped}\"")
    endforeach()
    string(JOIN ", " joined ${parts})
    set(${out_var} "[${joined}]" PARENT_SCOPE)
endfunction()

function(croft_register_artifact target kind)
    cmake_parse_arguments(ART "" "" "TAGS;DEPENDS" ${ARGN})
    croft_json_array(tags_json ${ART_TAGS})
    croft_json_array(depends_json ${ART_DEPENDS})
    set(entry "    {\n      \"name\": \"${target}\",\n      \"kind\": \"${kind}\",\n      \"tags\": ${tags_json},\n      \"depends\": ${depends_json}\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_ARTIFACT_ENTRIES "${entry}")
    set_property(GLOBAL APPEND PROPERTY CROFT_INSTALL_TARGETS "${target}")
endfunction()

# pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ── WIT Schema Codegen ──────────────────────────────────────────────────
set(WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.h")
set(WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.c")

set(TEST_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.h")
set(TEST_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.c")

set(MENU_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_menu_schema.h")
set(MENU_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_menu_schema.c")

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

# Menu Schema
add_custom_command(
    OUTPUT ${MENU_WIT_GENERATED_H} ${MENU_WIT_GENERATED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/menu-schema.wit"
                        --header "${MENU_WIT_GENERATED_H}" --source "${MENU_WIT_GENERATED_C}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/menu-schema.wit"
    COMMENT "Generating Menu Schema C Layouts from WIT..."
)
add_custom_target(menu_wit_schema_gen DEPENDS ${MENU_WIT_GENERATED_H} ${MENU_WIT_GENERATED_C})

# ── Optional dependency resolution ─────────────────────────────────────
set(CROFT_GLFW_TARGET "")
if(CROFT_GLFW_SOURCE_DIR)
    if(EXISTS "${CROFT_GLFW_SOURCE_DIR}/CMakeLists.txt")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW docs" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples" FORCE)
        add_subdirectory("${CROFT_GLFW_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/glfw" EXCLUDE_FROM_ALL)
    else()
        croft_optional_missing("glfw" "CROFT_GLFW_SOURCE_DIR does not contain CMakeLists.txt: ${CROFT_GLFW_SOURCE_DIR}")
    endif()
elseif(CROFT_GLFW_ROOT)
    find_path(CROFT_GLFW_INCLUDE_DIR GLFW/glfw3.h
        PATHS "${CROFT_GLFW_ROOT}/include"
        NO_DEFAULT_PATH
    )
    find_library(CROFT_GLFW_LIBRARY NAMES glfw glfw3
        PATHS "${CROFT_GLFW_ROOT}/lib"
        NO_DEFAULT_PATH
    )
    if(CROFT_GLFW_INCLUDE_DIR AND CROFT_GLFW_LIBRARY)
        add_library(croft_dep_glfw UNKNOWN IMPORTED)
        set_target_properties(croft_dep_glfw PROPERTIES
            IMPORTED_LOCATION "${CROFT_GLFW_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CROFT_GLFW_INCLUDE_DIR}"
        )
        set(CROFT_GLFW_TARGET croft_dep_glfw)
    endif()
endif()

if(NOT CROFT_GLFW_TARGET)
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        set(CROFT_GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        add_library(croft_dep_glfw_pkg INTERFACE)
        target_link_libraries(croft_dep_glfw_pkg INTERFACE glfw3::glfw)
        set(CROFT_GLFW_TARGET croft_dep_glfw_pkg)
    elseif(TARGET glfw3)
        set(CROFT_GLFW_TARGET glfw3)
    endif()
endif()

if(NOT CROFT_GLFW_TARGET)
    croft_optional_missing("glfw"
        "set CROFT_GLFW_SOURCE_DIR, install glfw, or provide CROFT_GLFW_ROOT (current: ${CROFT_GLFW_ROOT})")
endif()

set(CROFT_TGFX_TARGET "")
if(CROFT_GLFW_TARGET)
    if(CROFT_TGFX_SOURCE_DIR)
        if(EXISTS "${CROFT_TGFX_SOURCE_DIR}/CMakeLists.txt")
            set(TGFX_BUILD_SVG OFF CACHE BOOL "Disable tgfx SVG module" FORCE)
            set(TGFX_BUILD_PDF OFF CACHE BOOL "Disable tgfx PDF module" FORCE)
            set(TGFX_BUILD_LAYERS OFF CACHE BOOL "Disable tgfx layers module" FORCE)
            set(TGFX_BUILD_HELLO2D OFF CACHE BOOL "Disable tgfx Hello2D demo" FORCE)
            set(TGFX_USE_OPENGL ON CACHE BOOL "Enable tgfx OpenGL backend" FORCE)
            if(APPLE)
                set(TGFX_USE_METAL ON CACHE BOOL "Enable tgfx Metal backend for Croft variants" FORCE)
            endif()
            add_subdirectory("${CROFT_TGFX_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/tgfx" EXCLUDE_FROM_ALL)
        else()
            croft_optional_missing("tgfx" "CROFT_TGFX_SOURCE_DIR does not contain CMakeLists.txt: ${CROFT_TGFX_SOURCE_DIR}")
        endif()
    endif()

    if(TARGET tgfx)
        set(CROFT_TGFX_TARGET tgfx)
    elseif(NOT CROFT_TGFX_SOURCE_DIR)
        croft_optional_missing("tgfx" "set CROFT_TGFX_SOURCE_DIR to a local tgfx checkout")
    endif()
else()
    message(STATUS "Croft optional artifact skipped: tgfx (render artifacts are unavailable without glfw)")
endif()

set(CROFT_WASM3_TARGET "")
if(CROFT_WASM3_SOURCE_DIR)
    if(EXISTS "${CROFT_WASM3_SOURCE_DIR}/CMakeLists.txt")
        set(BUILD_WASI NONE CACHE STRING "Disable built-in WASI to use custom Sapling WASI shim" FORCE)
        set(d3_compiler OFF CACHE BOOL "Disable wasm3 compiler" FORCE)
        set(d3_extensions OFF CACHE BOOL "Disable wasm3 extensions" FORCE)
        add_subdirectory("${CROFT_WASM3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/wasm3" EXCLUDE_FROM_ALL)
    else()
        croft_optional_missing("wasm3" "CROFT_WASM3_SOURCE_DIR does not contain CMakeLists.txt: ${CROFT_WASM3_SOURCE_DIR}")
    endif()
else()
    find_path(CROFT_WASM3_INCLUDE_DIR wasm3.h)
    find_library(CROFT_WASM3_LIBRARY NAMES m3)
    if(NOT (CROFT_WASM3_INCLUDE_DIR AND CROFT_WASM3_LIBRARY) AND CROFT_FETCH_WASM3)
        set(BUILD_WASI NONE CACHE STRING "Disable built-in WASI to use custom Sapling WASI shim" FORCE)
        set(d3_compiler OFF CACHE BOOL "Disable wasm3 compiler" FORCE)
        set(d3_extensions OFF CACHE BOOL "Disable wasm3 extensions" FORCE)
        FetchContent_Declare(wasm3
            GIT_REPOSITORY "${CROFT_WASM3_GIT_REPOSITORY}"
            GIT_TAG "${CROFT_WASM3_GIT_TAG}"
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(wasm3)
    endif()
endif()

if(TARGET m3)
    set(CROFT_WASM3_TARGET m3)
elseif(CROFT_WASM3_INCLUDE_DIR AND CROFT_WASM3_LIBRARY)
    add_library(croft_dep_wasm3 UNKNOWN IMPORTED)
    set_target_properties(croft_dep_wasm3 PROPERTIES
        IMPORTED_LOCATION "${CROFT_WASM3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CROFT_WASM3_INCLUDE_DIR}"
    )
    set(CROFT_WASM3_TARGET croft_dep_wasm3)
elseif(NOT CROFT_WASM3_SOURCE_DIR AND NOT CROFT_FETCH_WASM3)
    croft_optional_missing("wasm3" "set CROFT_WASM3_SOURCE_DIR, install libm3 + headers, or enable CROFT_FETCH_WASM3")
elseif(NOT TARGET m3)
    croft_optional_missing("wasm3" "FetchContent did not produce the expected m3 target")
endif()

set(CROFT_MINIAUDIO_INCLUDE_DIR "")
if(CROFT_MINIAUDIO_SOURCE_DIR)
    if(EXISTS "${CROFT_MINIAUDIO_SOURCE_DIR}/miniaudio.h")
        set(CROFT_MINIAUDIO_INCLUDE_DIR "${CROFT_MINIAUDIO_SOURCE_DIR}")
    else()
        croft_optional_missing("miniaudio" "CROFT_MINIAUDIO_SOURCE_DIR does not contain miniaudio.h: ${CROFT_MINIAUDIO_SOURCE_DIR}")
    endif()
else()
    if(CROFT_FETCH_MINIAUDIO)
        FetchContent_Declare(miniaudio
            GIT_REPOSITORY "${CROFT_MINIAUDIO_GIT_REPOSITORY}"
            GIT_TAG "${CROFT_MINIAUDIO_GIT_TAG}"
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(miniaudio)
        if(NOT miniaudio_POPULATED)
            FetchContent_Populate(miniaudio)
        endif()
        find_path(CROFT_MINIAUDIO_INCLUDE_DIR miniaudio.h
            PATHS
                "${miniaudio_SOURCE_DIR}"
                "${miniaudio_SOURCE_DIR}/include"
            NO_DEFAULT_PATH
        )
    else()
        find_path(CROFT_MINIAUDIO_INCLUDE_DIR miniaudio.h)
    endif()
    if(NOT CROFT_MINIAUDIO_INCLUDE_DIR)
        croft_optional_missing("miniaudio"
            "set CROFT_MINIAUDIO_SOURCE_DIR, install miniaudio.h, or enable CROFT_FETCH_MINIAUDIO")
    endif()
endif()

# ── Tier 0/1/2 artifacts ───────────────────────────────────────────────
set(TIER0_SOURCES
    src/host/host_log.c
    src/host/host_time.c
    src/host/host_thread.c
)

set(TIER1_SOURCES
    src/host/host_queue.c
    src/host/host_messaging.c
)

set(TIER2_SOURCES
    src/host/host_fs.c
)

add_library(croft_foundation STATIC ${TIER0_SOURCES})
croft_apply_public_includes(croft_foundation)
target_link_libraries(croft_foundation PUBLIC Threads::Threads)
croft_register_artifact(croft_foundation foundation
    TAGS tier0 foundation threads log time
)

add_library(croft_thatch_core STATIC src/runtime/thatch_core.c)
croft_apply_public_includes(croft_thatch_core)
croft_register_artifact(croft_thatch_core serialization
    TAGS thatch packed-data serialization
)

add_library(croft_wire_runtime STATIC
    src/runner/wire_v0.c
    ${WIT_GENERATED_C}
)
add_dependencies(croft_wire_runtime wit_schema_gen)
croft_apply_public_includes(croft_wire_runtime)
target_link_libraries(croft_wire_runtime PUBLIC croft_thatch_core)
croft_register_artifact(croft_wire_runtime wire
    TAGS runner wire dbi-schema validation
    DEPENDS croft_thatch_core
)

add_library(croft_messaging STATIC ${TIER1_SOURCES})
add_dependencies(croft_messaging wit_schema_gen)
croft_apply_public_includes(croft_messaging)
target_link_libraries(croft_messaging PUBLIC croft_foundation croft_wire_runtime)
croft_register_artifact(croft_messaging messaging
    TAGS tier1 messaging queue dbi-schema
    DEPENDS croft_foundation croft_wire_runtime
)

add_library(croft_fs STATIC ${TIER2_SOURCES})
croft_apply_public_includes(croft_fs)
target_link_libraries(croft_fs PUBLIC croft_foundation)
croft_register_artifact(croft_fs filesystem
    TAGS tier2 filesystem io
    DEPENDS croft_foundation
)

add_library(croft STATIC ${TIER0_SOURCES} ${TIER1_SOURCES} ${TIER2_SOURCES})
add_dependencies(croft wit_schema_gen)
croft_apply_public_includes(croft)
target_link_libraries(croft PUBLIC Threads::Threads croft_wire_runtime)
croft_register_artifact(croft aggregate
    TAGS tier0 tier1 tier2 foundation messaging filesystem compatibility
    DEPENDS croft_wire_runtime
)

# ── Library: Sapling ────────────────────────────────────────────────────
file(GLOB SAPLING_SOURCES CONFIGURE_DEPENDS "src/sapling/*.c" "src/runner/*.c" "src/wasi/*.c")

add_library(sapling STATIC ${SAPLING_SOURCES} ${WIT_GENERATED_C})
add_dependencies(sapling wit_schema_gen)
target_include_directories(sapling
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(sapling PUBLIC SAPLING_PAGE_SIZE=4096 SAPLING_THREADED)
target_link_libraries(sapling PUBLIC Threads::Threads croft_thatch_core)
if(CROFT_BUILD_TESTS)
    target_compile_definitions(sapling PRIVATE SAPLING_HAMT_TESTING)
endif()
croft_register_artifact(sapling datastore
    TAGS tier3 tier4 sapling runner wasi datastore persistent-text
)

# ── Optional Croft artifacts ───────────────────────────────────────────
if(CROFT_WASM3_TARGET)
    add_library(croft_wasm_wasm3 STATIC src/host/host_wasm.c)
    croft_apply_public_includes(croft_wasm_wasm3)
    target_link_libraries(croft_wasm_wasm3
        PUBLIC
            croft_foundation
            croft_fs
        PRIVATE
            ${CROFT_WASM3_TARGET}
    )
    croft_register_artifact(croft_wasm_wasm3 wasm
        TAGS tier4 wasm wasm3 host
        DEPENDS croft_foundation croft_fs
    )
endif()

if(CROFT_MINIAUDIO_INCLUDE_DIR)
    add_library(croft_audio_miniaudio STATIC src/host/host_audio.c)
    croft_apply_public_includes(croft_audio_miniaudio)
    target_include_directories(croft_audio_miniaudio PRIVATE "${CROFT_MINIAUDIO_INCLUDE_DIR}")
    if(NOT WIN32)
        target_link_libraries(croft_audio_miniaudio PRIVATE m)
    endif()
    croft_register_artifact(croft_audio_miniaudio audio
        TAGS tier8 audio miniaudio
    )
endif()

add_library(croft_a11y_stub STATIC src/host/host_a11y_stub.c)
croft_apply_public_includes(croft_a11y_stub)
croft_register_artifact(croft_a11y_stub accessibility
    TAGS tier9 a11y stub
)

if(CROFT_GLFW_TARGET)
    add_library(croft_ui_glfw_opengl STATIC src/host/host_ui.c)
    croft_apply_public_includes(croft_ui_glfw_opengl)
    target_link_libraries(croft_ui_glfw_opengl PUBLIC croft_foundation ${CROFT_GLFW_TARGET})
    if(APPLE)
        target_compile_definitions(croft_ui_glfw_opengl PRIVATE GL_SILENCE_DEPRECATION)
    endif()
    croft_register_artifact(croft_ui_glfw_opengl ui
        TAGS tier5 ui glfw opengl
        DEPENDS croft_foundation
    )

    if(APPLE)
        add_library(croft_ui_glfw_metal STATIC src/host/host_ui.c)
        croft_apply_public_includes(croft_ui_glfw_metal)
        target_compile_definitions(croft_ui_glfw_metal PRIVATE CROFT_HOST_UI_GLFW_NO_API=1)
        target_link_libraries(croft_ui_glfw_metal PUBLIC croft_foundation ${CROFT_GLFW_TARGET})
        croft_register_artifact(croft_ui_glfw_metal ui
            TAGS tier5 ui glfw metal macos
            DEPENDS croft_foundation
        )
    endif()
endif()

if(CROFT_GLFW_TARGET AND CROFT_TGFX_TARGET)
    add_library(croft_render_tgfx_opengl STATIC src/host/host_render.cpp)
    croft_apply_public_includes(croft_render_tgfx_opengl)
    target_link_libraries(croft_render_tgfx_opengl PUBLIC croft_ui_glfw_opengl ${CROFT_TGFX_TARGET})
    if(APPLE)
        target_compile_definitions(croft_render_tgfx_opengl PRIVATE GL_SILENCE_DEPRECATION)
    endif()
    croft_register_artifact(croft_render_tgfx_opengl render
        TAGS tier6 render tgfx opengl
        DEPENDS croft_ui_glfw_opengl
    )

    add_library(croft_scene_core_tgfx_opengl STATIC
        src/scene/scene_node.c
        src/scene/node_viewport.c
        src/scene/node_code_block.c
    )
    croft_apply_public_includes(croft_scene_core_tgfx_opengl)
    target_link_libraries(croft_scene_core_tgfx_opengl PUBLIC croft_render_tgfx_opengl)
    croft_register_artifact(croft_scene_core_tgfx_opengl scene
        TAGS tier7 scene tgfx opengl
        DEPENDS croft_render_tgfx_opengl
    )

    add_library(croft_scene_text_editor_tgfx_opengl STATIC src/scene/node_text_editor.c)
    croft_apply_public_includes(croft_scene_text_editor_tgfx_opengl)
    target_link_libraries(croft_scene_text_editor_tgfx_opengl
        PUBLIC
            croft_scene_core_tgfx_opengl
            sapling
    )
    croft_register_artifact(croft_scene_text_editor_tgfx_opengl scene
        TAGS tier7 scene text-editor tgfx opengl sapling
        DEPENDS croft_scene_core_tgfx_opengl sapling
    )

    if(APPLE AND TARGET croft_ui_glfw_metal)
        add_library(croft_render_tgfx_metal STATIC src/host/host_render_metal.mm)
        set_source_files_properties(src/host/host_render_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        croft_apply_public_includes(croft_render_tgfx_metal)
        target_link_libraries(croft_render_tgfx_metal
            PUBLIC
                croft_ui_glfw_metal
                ${CROFT_TGFX_TARGET}
                "-framework AppKit"
                "-framework Metal"
                "-framework QuartzCore"
        )
        croft_register_artifact(croft_render_tgfx_metal render
            TAGS tier6 render tgfx metal macos
            DEPENDS croft_ui_glfw_metal
        )

        add_library(croft_scene_core_tgfx_metal STATIC
            src/scene/scene_node.c
            src/scene/node_viewport.c
            src/scene/node_code_block.c
        )
        croft_apply_public_includes(croft_scene_core_tgfx_metal)
        target_link_libraries(croft_scene_core_tgfx_metal PUBLIC croft_render_tgfx_metal)
        croft_register_artifact(croft_scene_core_tgfx_metal scene
            TAGS tier7 scene tgfx metal macos
            DEPENDS croft_render_tgfx_metal
        )

        add_library(croft_scene_text_editor_tgfx_metal STATIC src/scene/node_text_editor.c)
        croft_apply_public_includes(croft_scene_text_editor_tgfx_metal)
        target_link_libraries(croft_scene_text_editor_tgfx_metal
            PUBLIC
                croft_scene_core_tgfx_metal
                sapling
        )
        croft_register_artifact(croft_scene_text_editor_tgfx_metal scene
            TAGS tier7 scene text-editor tgfx metal macos sapling
            DEPENDS croft_scene_core_tgfx_metal sapling
        )
    endif()
endif()

if(APPLE)
    add_library(croft_a11y_macos STATIC src/host/mac/host_a11y.m)
    croft_apply_public_includes(croft_a11y_macos)
    set_source_files_properties(src/host/mac/host_a11y.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_a11y_macos PUBLIC "-framework AppKit")
    croft_register_artifact(croft_a11y_macos accessibility
        TAGS tier9 a11y macos native
    )

    add_library(croft_menu_macos STATIC
        ${MENU_WIT_GENERATED_C}
        src/host/mac/host_menu.m
    )
    add_dependencies(croft_menu_macos menu_wit_schema_gen)
    croft_apply_public_includes(croft_menu_macos)
    set_source_files_properties(src/host/mac/host_menu.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_menu_macos PUBLIC "-framework AppKit")
    croft_register_artifact(croft_menu_macos menu
        TAGS menu macos native
    )

    if(TARGET croft_ui_glfw_opengl)
        add_library(croft_gesture_macos STATIC src/host/mac/host_gesture.m)
        croft_apply_public_includes(croft_gesture_macos)
        set_source_files_properties(src/host/mac/host_gesture.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(croft_gesture_macos PUBLIC croft_ui_glfw_opengl "-framework AppKit")
        croft_register_artifact(croft_gesture_macos gesture
            TAGS ui gesture macos native
            DEPENDS croft_ui_glfw_opengl
        )
    endif()

    if(TARGET croft_ui_glfw_metal)
        add_library(croft_gesture_macos_metal STATIC src/host/mac/host_gesture.m)
        croft_apply_public_includes(croft_gesture_macos_metal)
        set_source_files_properties(src/host/mac/host_gesture.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(croft_gesture_macos_metal PUBLIC croft_ui_glfw_metal "-framework AppKit")
        croft_register_artifact(croft_gesture_macos_metal gesture
            TAGS ui gesture macos native metal
            DEPENDS croft_ui_glfw_metal
        )
    endif()
endif()

set(CROFT_EXAMPLE_UI_TARGET "")
if(APPLE AND TARGET croft_ui_glfw_metal)
    set(CROFT_EXAMPLE_UI_TARGET croft_ui_glfw_metal)
elseif(TARGET croft_ui_glfw_opengl)
    set(CROFT_EXAMPLE_UI_TARGET croft_ui_glfw_opengl)
endif()

set(CROFT_EXAMPLE_RENDER_TARGET "")
if(APPLE AND TARGET croft_render_tgfx_metal)
    set(CROFT_EXAMPLE_RENDER_TARGET croft_render_tgfx_metal)
elseif(TARGET croft_render_tgfx_opengl)
    set(CROFT_EXAMPLE_RENDER_TARGET croft_render_tgfx_opengl)
endif()

set(CROFT_EXAMPLE_SCENE_TARGET "")
if(APPLE AND TARGET croft_scene_core_tgfx_metal)
    set(CROFT_EXAMPLE_SCENE_TARGET croft_scene_core_tgfx_metal)
elseif(TARGET croft_scene_core_tgfx_opengl)
    set(CROFT_EXAMPLE_SCENE_TARGET croft_scene_core_tgfx_opengl)
endif()

set(CROFT_EXAMPLE_EDITOR_TARGET "")
if(APPLE AND TARGET croft_scene_text_editor_tgfx_metal)
    set(CROFT_EXAMPLE_EDITOR_TARGET croft_scene_text_editor_tgfx_metal)
elseif(TARGET croft_scene_text_editor_tgfx_opengl)
    set(CROFT_EXAMPLE_EDITOR_TARGET croft_scene_text_editor_tgfx_opengl)
endif()

set(CROFT_EXAMPLE_GESTURE_TARGET "")
if(APPLE AND TARGET croft_gesture_macos_metal)
    set(CROFT_EXAMPLE_GESTURE_TARGET croft_gesture_macos_metal)
elseif(TARGET croft_gesture_macos)
    set(CROFT_EXAMPLE_GESTURE_TARGET croft_gesture_macos)
endif()

# ── Tests ───────────────────────────────────────────────────────────────
if(CROFT_BUILD_TESTS)
    enable_testing()

    add_executable(test_croft tests/test_main.c tests/test_queue.c tests/host_tests/test_fs.c)
    target_include_directories(test_croft PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    target_link_libraries(test_croft PRIVATE croft)
    add_test(NAME croft_tests COMMAND test_croft)

    set(CROFT_WAT2WASM_TOOL "")
    set(CROFT_WAT2WASM_DEP "")
    if(TARGET croft_wasm_wasm3)
        if(CROFT_WABT_SOURCE_DIR)
            if(EXISTS "${CROFT_WABT_SOURCE_DIR}/CMakeLists.txt")
                set(BUILD_TESTS OFF CACHE BOOL "Disable WABT tests" FORCE)
                set(BUILD_TOOLS ON CACHE BOOL "Enable WABT tools" FORCE)
                set(BUILD_LIBWASM OFF CACHE BOOL "Disable WABT shared library" FORCE)
                add_subdirectory("${CROFT_WABT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/wabt" EXCLUDE_FROM_ALL)
            else()
                croft_optional_missing("wabt" "CROFT_WABT_SOURCE_DIR does not contain CMakeLists.txt: ${CROFT_WABT_SOURCE_DIR}")
            endif()
        else()
            find_program(CROFT_WAT2WASM_TOOL wat2wasm)
        endif()

        if(TARGET wat2wasm)
            set(CROFT_WAT2WASM_TOOL wat2wasm)
            set(CROFT_WAT2WASM_DEP wat2wasm)
        elseif(NOT CROFT_WAT2WASM_TOOL AND NOT CROFT_WABT_SOURCE_DIR)
            croft_optional_missing("wabt" "set CROFT_WABT_SOURCE_DIR or install wat2wasm to enable host Wasm tests")
        endif()
    endif()

    if(TARGET croft_wasm_wasm3 AND CROFT_WAT2WASM_TOOL)
        set(WASM_GUEST_BIN "${CMAKE_CURRENT_BINARY_DIR}/tests/dummy_guest.wasm")
        add_custom_command(
            OUTPUT ${WASM_GUEST_BIN}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests"
            COMMAND ${CROFT_WAT2WASM_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/dummy_guest.wat"
                                         -o ${WASM_GUEST_BIN}
            DEPENDS ${CROFT_WAT2WASM_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/dummy_guest.wat"
            COMMENT "Compiling dummy_guest.wat to Wasm..."
        )
        add_custom_target(wasm_guest_gen DEPENDS ${WASM_GUEST_BIN})

        target_sources(test_croft PRIVATE tests/host_tests/test_wasm_guest.c)
        target_link_libraries(test_croft PRIVATE croft_wasm_wasm3)
        target_compile_definitions(test_croft PRIVATE
            CROFT_TEST_HAS_WASM
            DUMMY_GUEST_WASM_PATH="${WASM_GUEST_BIN}"
        )
        add_dependencies(test_croft wasm_guest_gen)
    endif()

    if(CROFT_EXAMPLE_UI_TARGET)
        add_executable(test_ui_window_standalone tests/host_tests/test_ui_window_standalone.c)
        target_link_libraries(test_ui_window_standalone PRIVATE ${CROFT_EXAMPLE_UI_TARGET})
    endif()

    if(CROFT_EXAMPLE_RENDER_TARGET)
        add_executable(test_render_standalone tests/host_tests/test_render_standalone.c)
        target_link_libraries(test_render_standalone PRIVATE ${CROFT_EXAMPLE_RENDER_TARGET})
    endif()

    if(CROFT_EXAMPLE_SCENE_TARGET)
        add_executable(test_scene_standalone tests/host_tests/test_scene_standalone.c)
        target_link_libraries(test_scene_standalone PRIVATE
            ${CROFT_EXAMPLE_SCENE_TARGET}
            croft_a11y_stub
        )

        add_executable(test_zoom_standalone tests/host_tests/test_zoom_standalone.c)
        target_link_libraries(test_zoom_standalone PRIVATE
            ${CROFT_EXAMPLE_SCENE_TARGET}
            croft_a11y_stub
        )
        if(CROFT_EXAMPLE_GESTURE_TARGET)
            target_link_libraries(test_zoom_standalone PRIVATE ${CROFT_EXAMPLE_GESTURE_TARGET})
        endif()
    endif()

    if(CROFT_EXAMPLE_EDITOR_TARGET)
        add_executable(test_editor_standalone tests/host_tests/test_editor_standalone.c)
        target_link_libraries(test_editor_standalone PRIVATE
            ${CROFT_EXAMPLE_EDITOR_TARGET}
            croft_a11y_stub
            croft_fs
        )
        if(CROFT_EXAMPLE_GESTURE_TARGET)
            target_link_libraries(test_editor_standalone PRIVATE ${CROFT_EXAMPLE_GESTURE_TARGET})
        endif()
    endif()

    if(TARGET croft_a11y_macos AND CROFT_EXAMPLE_SCENE_TARGET)
        add_executable(test_a11y_standalone tests/host_tests/test_a11y_standalone.c)
        target_link_libraries(test_a11y_standalone PRIVATE
            ${CROFT_EXAMPLE_SCENE_TARGET}
            croft_a11y_macos
        )
    endif()

    if(TARGET croft_menu_macos AND CROFT_EXAMPLE_UI_TARGET)
        add_executable(test_menu_standalone tests/host_tests/test_menu_standalone.c)
        target_link_libraries(test_menu_standalone PRIVATE
            ${CROFT_EXAMPLE_UI_TARGET}
            croft_menu_macos
        )
    endif()

    if(TARGET croft_audio_miniaudio)
        add_executable(test_audio_standalone tests/host_tests/test_audio_standalone.c)
        target_link_libraries(test_audio_standalone PRIVATE croft_audio_miniaudio)
    endif()

    add_executable(test_fuzz_messaging tests/host_tests/test_fuzz_messaging.c)
    target_link_libraries(test_fuzz_messaging PRIVATE croft)

    # Sapling test suite (each file has its own main)
    file(GLOB SAPLING_UNIT_TESTS CONFIGURE_DEPENDS
        "tests/sapling_tests/unit/*.c"
        "tests/sapling_tests/integration/*.c"
    )
    list(REMOVE_ITEM SAPLING_UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/unit/wasm_guest_example.c")
    foreach(test_src IN LISTS SAPLING_UNIT_TESTS)
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        target_link_libraries(${test_name} PRIVATE sapling)
        target_compile_definitions(${test_name} PRIVATE SAPLING_HAMT_TESTING SAPLING_THREADED)
        if(test_name STREQUAL "test_wit_thatch_codegen")
            add_dependencies(${test_name} test_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        add_test(NAME sapling_${test_name} COMMAND ${test_name})
    endforeach()
endif()

# ── Packaging & artifact manifest ──────────────────────────────────────
get_property(CROFT_ARTIFACT_ENTRIES GLOBAL PROPERTY CROFT_ARTIFACT_ENTRIES)
if(NOT CROFT_ARTIFACT_ENTRIES)
    set(CROFT_ARTIFACT_ENTRIES "")
endif()
string(JOIN ",\n" CROFT_ARTIFACT_ENTRIES_JSON ${CROFT_ARTIFACT_ENTRIES})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-artifacts.json"
    CONTENT "{\n  \"artifacts\": [\n${CROFT_ARTIFACT_ENTRIES_JSON}\n  ]\n}\n")

get_property(CROFT_INSTALL_TARGETS GLOBAL PROPERTY CROFT_INSTALL_TARGETS)
install(TARGETS ${CROFT_INSTALL_TARGETS}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/croft/
    DESTINATION include/croft
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY include/sapling/
    DESTINATION include/sapling
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY include/runner/
    DESTINATION include/runner
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY include/wasi/
    DESTINATION include/wasi
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${WIT_GENERATED_H} DESTINATION include/generated)
if(TARGET croft_menu_macos)
    install(FILES ${MENU_WIT_GENERATED_H} DESTINATION include/generated)
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/croft-artifacts.json" DESTINATION share/croft)

# CPack configuration
set(CPACK_PACKAGE_NAME "croft-host")
set(CPACK_PACKAGE_VENDOR "Lambkin-Lang")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Croft modular runtime artifacts")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_GENERATOR "ZIP;TGZ")

include(CPack)
