cmake_minimum_required(VERSION 3.15)

option(CROFT_PREFER_HOMEBREW_LLVM_21
       "Prefer Homebrew llvm@21/lld@21 on macOS for fresh configure runs"
       ON)
set(CROFT_LLVM_21_ROOT "" CACHE PATH
    "Optional override for the preferred LLVM 21 install prefix (expects bin/clang and bin/clang++)")
set(CROFT_LLD_21_ROOT "" CACHE PATH
    "Optional override for the preferred LLD 21 install prefix (expects bin/wasm-ld)")

function(croft_collect_preferred_prefixes out_var explicit_root formula_name)
    set(prefixes "")
    if(explicit_root)
        list(APPEND prefixes "${explicit_root}")
    endif()
    if(APPLE)
        list(APPEND prefixes
            "/opt/homebrew/opt/${formula_name}"
            "/usr/local/opt/${formula_name}")
    endif()
    list(REMOVE_DUPLICATES prefixes)
    set(${out_var} "${prefixes}" PARENT_SCOPE)
endfunction()

function(croft_find_tool_in_prefixes out_var tool_name)
    set(prefixes ${ARGN})
    foreach(prefix IN LISTS prefixes)
        if(prefix AND EXISTS "${prefix}/bin/${tool_name}")
            set(${out_var} "${prefix}/bin/${tool_name}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

set(CROFT_LLVM_CLANG_TOOL "" CACHE FILEPATH
    "Resolved clang path used by Croft tool discovery")
set(CROFT_LLVM_CLANGXX_TOOL "" CACHE FILEPATH
    "Resolved clang++ path used by Croft tool discovery")
set(CROFT_WASM_LD_TOOL "" CACHE FILEPATH
    "Resolved wasm-ld path used by Croft Wasm guest tooling")

if(CROFT_PREFER_HOMEBREW_LLVM_21)
    croft_collect_preferred_prefixes(_croft_llvm_prefixes "${CROFT_LLVM_21_ROOT}" "llvm@21")
    croft_collect_preferred_prefixes(_croft_lld_prefixes "${CROFT_LLD_21_ROOT}" "lld@21")
    croft_find_tool_in_prefixes(_croft_clang clang ${_croft_llvm_prefixes})
    croft_find_tool_in_prefixes(_croft_clangxx clang++ ${_croft_llvm_prefixes})
    croft_find_tool_in_prefixes(_croft_wasm_ld wasm-ld ${_croft_lld_prefixes})

    if(_croft_clang AND _croft_clangxx)
        set(CROFT_LLVM_CLANG_TOOL "${_croft_clang}" CACHE FILEPATH
            "Resolved clang path used by Croft tool discovery" FORCE)
        set(CROFT_LLVM_CLANGXX_TOOL "${_croft_clangxx}" CACHE FILEPATH
            "Resolved clang++ path used by Croft tool discovery" FORCE)
        get_filename_component(_croft_llvm_bin_dir "${_croft_clang}" DIRECTORY)
        list(PREPEND CMAKE_PROGRAM_PATH "${_croft_llvm_bin_dir}")
        if(NOT DEFINED CMAKE_C_COMPILER)
            set(CMAKE_C_COMPILER "${_croft_clang}" CACHE FILEPATH
                "C compiler selected by Croft tool discovery")
        endif()
        if(NOT DEFINED CMAKE_CXX_COMPILER)
            set(CMAKE_CXX_COMPILER "${_croft_clangxx}" CACHE FILEPATH
                "C++ compiler selected by Croft tool discovery")
        endif()
        if(NOT DEFINED CMAKE_OBJC_COMPILER)
            set(CMAKE_OBJC_COMPILER "${_croft_clang}" CACHE FILEPATH
                "Objective-C compiler selected by Croft tool discovery")
        endif()
        if(NOT DEFINED CMAKE_OBJCXX_COMPILER)
            set(CMAKE_OBJCXX_COMPILER "${_croft_clangxx}" CACHE FILEPATH
                "Objective-C++ compiler selected by Croft tool discovery")
        endif()
    endif()

    if(_croft_wasm_ld)
        set(CROFT_WASM_LD_TOOL "${_croft_wasm_ld}" CACHE FILEPATH
            "Resolved wasm-ld path used by Croft Wasm guest tooling" FORCE)
        get_filename_component(_croft_lld_bin_dir "${_croft_wasm_ld}" DIRECTORY)
        list(PREPEND CMAKE_PROGRAM_PATH "${_croft_lld_bin_dir}")
    endif()
endif()

if(CMAKE_PROGRAM_PATH)
    list(REMOVE_DUPLICATES CMAKE_PROGRAM_PATH)
endif()

project(croft C CXX OBJC OBJCXX)

include(FetchContent)

if(CROFT_PREFER_HOMEBREW_LLVM_21)
    if(CROFT_LLVM_CLANG_TOOL)
        if(CMAKE_C_COMPILER STREQUAL CROFT_LLVM_CLANG_TOOL)
            message(STATUS "Croft compiler toolchain: ${CMAKE_C_COMPILER}")
        else()
            message(STATUS
                "Croft preferred llvm@21 detected at ${CROFT_LLVM_CLANG_TOOL}, "
                "but active compiler is ${CMAKE_C_COMPILER}. Use a fresh build dir to adopt the preferred toolchain.")
        endif()
    endif()
    if(CROFT_WASM_LD_TOOL)
        message(STATUS "Croft Wasm linker: ${CROFT_WASM_LD_TOOL}")
    endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(croft_append_unique_flag var_name flag)
    set(current_value "${${var_name}}")
    string(FIND " ${current_value} " " ${flag} " already_present)
    if(already_present EQUAL -1)
        if(current_value STREQUAL "")
            set(${var_name} "${flag}" PARENT_SCOPE)
        else()
            set(${var_name} "${current_value} ${flag}" PARENT_SCOPE)
        endif()
    else()
        set(${var_name} "${current_value}" PARENT_SCOPE)
    endif()
endfunction()

foreach(lang C CXX OBJC OBJCXX)
    foreach(cfg RELEASE MINSIZEREL)
        croft_append_unique_flag(CMAKE_${lang}_FLAGS_${cfg} "-g0")
        croft_append_unique_flag(CMAKE_${lang}_FLAGS_${cfg} "-ffunction-sections")
        croft_append_unique_flag(CMAKE_${lang}_FLAGS_${cfg} "-fdata-sections")
    endforeach()
endforeach()

foreach(link_var
        CMAKE_EXE_LINKER_FLAGS_RELEASE
        CMAKE_EXE_LINKER_FLAGS_MINSIZEREL
        CMAKE_SHARED_LINKER_FLAGS_RELEASE
        CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL
        CMAKE_MODULE_LINKER_FLAGS_RELEASE
        CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL)
    croft_append_unique_flag(${link_var} "-Wl,-dead_strip")
    croft_append_unique_flag(${link_var} "-Wl,-x")
    if(APPLE)
        croft_append_unique_flag(${link_var} "-Wl,-dead_strip_dylibs")
    endif()
endforeach()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)

option(CROFT_BUILD_TESTS "Build the test suite" ON)
option(CROFT_FAIL_ON_MISSING_OPTIONAL_DEPS
       "Treat unresolved optional subsystem dependencies as configure errors" OFF)
option(CROFT_FETCH_WASM3 "Fetch wasm3 with FetchContent when no local/source install is provided" ON)
option(CROFT_FETCH_MINIAUDIO
       "Fetch miniaudio with FetchContent when no local header path is provided" ON)
set(CROFT_WASI_PROPOSALS_DIR "" CACHE PATH
    "Optional path to a local WASI proposals checkout used for current-machine WIT generation")

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

function(croft_select_wasi_wit out_var relative_path fallback_path)
    if(CROFT_WASI_PROPOSALS_DIR AND EXISTS "${CROFT_WASI_PROPOSALS_DIR}/${relative_path}")
        set(selected "${CROFT_WASI_PROPOSALS_DIR}/${relative_path}")
    elseif(IS_ABSOLUTE "${fallback_path}")
        set(selected "${fallback_path}")
    else()
        set(selected "${CMAKE_CURRENT_SOURCE_DIR}/${fallback_path}")
    endif()
    set(${out_var} "${selected}" PARENT_SCOPE)
endfunction()

function(croft_collect_wit_dir_depends out_var wit_path)
    get_filename_component(wit_dir "${wit_path}" DIRECTORY)
    file(GLOB wit_dir_inputs CONFIGURE_DEPENDS "${wit_dir}/*.wit")
    set(${out_var} ${wit_dir_inputs} PARENT_SCOPE)
endfunction()

function(croft_define_wasi_wit_schema var_prefix)
    cmake_parse_arguments(WASI
        "CURRENT_MACHINE"
        "RELATIVE_PATH;COMMENT"
        "EXTRA_DEPENDS"
        ${ARGN})
    if(NOT WASI_RELATIVE_PATH)
        message(FATAL_ERROR "croft_define_wasi_wit_schema(${var_prefix}) requires RELATIVE_PATH")
    endif()

    if(WASI_CURRENT_MACHINE)
        set(fallback_path "${CROFT_WASI_CURRENT_MACHINE_0_2_9_DIR}/${WASI_RELATIVE_PATH}")
        set(extra_args ${CROFT_WASI_CURRENT_MACHINE_PACKAGE_ROOT_ARGS})
    else()
        set(fallback_path "${CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR}/${WASI_RELATIVE_PATH}")
        set(extra_args "")
    endif()

    croft_select_wasi_wit(wit_path "${WASI_RELATIVE_PATH}" "${fallback_path}")
    croft_collect_wit_dir_depends(wit_depends "${wit_path}")
    if(WASI_CURRENT_MACHINE)
        list(APPEND wit_depends ${CROFT_WASI_VENDOR_0_2_9_WIT_INPUTS})
    endif()
    list(APPEND wit_depends ${WASI_EXTRA_DEPENDS})

    set(header_var "${var_prefix}_WIT_GENERATED_H")
    set(source_var "${var_prefix}_WIT_GENERATED_C")
    set(manifest_var "${var_prefix}_WIT_GENERATED_MANIFEST")
    set(generated_h "${${header_var}}")
    set(generated_c "${${source_var}}")
    set(generated_manifest "${${manifest_var}}")
    if(NOT generated_h OR NOT generated_c OR NOT generated_manifest)
        message(FATAL_ERROR
            "croft_define_wasi_wit_schema(${var_prefix}) requires ${header_var}, ${source_var}, and ${manifest_var} to be set first")
    endif()

    string(TOLOWER "${var_prefix}" target_prefix)
    set(schema_target "${target_prefix}_wit_schema_gen")

    add_custom_command(
        OUTPUT ${generated_h} ${generated_c} ${generated_manifest}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND wit_codegen --wit "${wit_path}"
                            ${extra_args}
                            --header "${generated_h}" --source "${generated_c}" --manifest "${generated_manifest}"
        DEPENDS wit_codegen ${wit_depends}
        COMMENT "${WASI_COMMENT}"
    )
    add_custom_target(${schema_target}
        DEPENDS ${generated_h}
                ${generated_c}
                ${generated_manifest})

    set(path_var "${var_prefix}_WIT_PATH")
    set(depends_var "${var_prefix}_WIT_DEPENDS")
    set(${path_var} "${wit_path}" PARENT_SCOPE)
    set(${depends_var} "${wit_depends}" PARENT_SCOPE)
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

function(croft_json_string out_var value)
    string(REPLACE "\\" "\\\\" escaped "${value}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    set(${out_var} "\"${escaped}\"" PARENT_SCOPE)
endfunction()

function(croft_register_artifact target kind)
    cmake_parse_arguments(ART
        ""
        "PROFILE;UPSTREAM_PACKAGE;UPSTREAM_VERSION;SUPPORT_STATUS;APPLICABILITY"
        "TAGS;DEPENDS;CAPABILITY_BUNDLES;SHARED_SUBSTRATES;HELPER_INTERFACES;DECLARED_WORLDS;EXPANDED_SURFACES"
        ${ARGN})
    if(ART_PROFILE)
        set(profile "${ART_PROFILE}")
    else()
        croft_get_target_profile(profile ${target})
    endif()
    if(profile)
        croft_json_string(profile_json "${profile}")
    else()
        set(profile_json "null")
    endif()
    if(ART_UPSTREAM_PACKAGE)
        croft_json_string(upstream_package_json "${ART_UPSTREAM_PACKAGE}")
    else()
        set(upstream_package_json "null")
    endif()
    if(ART_UPSTREAM_VERSION)
        croft_json_string(upstream_version_json "${ART_UPSTREAM_VERSION}")
    else()
        set(upstream_version_json "null")
    endif()
    if(ART_SUPPORT_STATUS)
        croft_json_string(support_status_json "${ART_SUPPORT_STATUS}")
    else()
        set(support_status_json "null")
    endif()
    if(ART_APPLICABILITY)
        croft_json_string(applicability_json "${ART_APPLICABILITY}")
    else()
        set(applicability_json "null")
    endif()
    croft_json_array(tags_json ${ART_TAGS})
    croft_json_array(depends_json ${ART_DEPENDS})
    croft_json_array(capability_bundles_json ${ART_CAPABILITY_BUNDLES})
    croft_json_array(shared_substrates_json ${ART_SHARED_SUBSTRATES})
    croft_json_array(helper_interfaces_json ${ART_HELPER_INTERFACES})
    croft_json_array(declared_worlds_json ${ART_DECLARED_WORLDS})
    croft_json_array(expanded_surfaces_json ${ART_EXPANDED_SURFACES})
    set(entry "    {\n      \"name\": \"${target}\",\n      \"kind\": \"${kind}\",\n      \"profile\": ${profile_json},\n      \"upstream_package\": ${upstream_package_json},\n      \"upstream_version\": ${upstream_version_json},\n      \"support_status\": ${support_status_json},\n      \"applicability\": ${applicability_json},\n      \"capability_bundles\": ${capability_bundles_json},\n      \"shared_substrates\": ${shared_substrates_json},\n      \"helper_interfaces\": ${helper_interfaces_json},\n      \"declared_worlds\": ${declared_worlds_json},\n      \"expanded_surfaces\": ${expanded_surfaces_json},\n      \"tags\": ${tags_json},\n      \"depends\": ${depends_json}\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_ARTIFACT_ENTRIES "${entry}")
    set_property(GLOBAL APPEND PROPERTY CROFT_INSTALL_TARGETS "${target}")
endfunction()

function(croft_register_xpi_substrate name)
    cmake_parse_arguments(XPI
        ""
        "KIND;APPLICABILITY;DESCRIPTION"
        "ARTIFACTS;INTERFACES"
        ${ARGN})
    if(XPI_KIND)
        croft_json_string(kind_json "${XPI_KIND}")
    else()
        set(kind_json "null")
    endif()
    if(XPI_APPLICABILITY)
        croft_json_string(applicability_json "${XPI_APPLICABILITY}")
    else()
        set(applicability_json "null")
    endif()
    if(XPI_DESCRIPTION)
        croft_json_string(description_json "${XPI_DESCRIPTION}")
    else()
        set(description_json "null")
    endif()
    croft_json_array(artifacts_json ${XPI_ARTIFACTS})
    croft_json_array(interfaces_json ${XPI_INTERFACES})
    set(entry "    {\n      \"name\": \"${name}\",\n      \"kind\": ${kind_json},\n      \"applicability\": ${applicability_json},\n      \"description\": ${description_json},\n      \"artifacts\": ${artifacts_json},\n      \"interfaces\": ${interfaces_json}\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_XPI_SUBSTRATE_ENTRIES "${entry}")
endfunction()

function(croft_register_xpi_bundle name)
    cmake_parse_arguments(XPI
        ""
        "SUPPORT_STATUS;APPLICABILITY;DESCRIPTION"
        "ARTIFACTS;SUBSTRATES;DECLARED_WORLDS;EXPANDED_SURFACES;HELPER_INTERFACES"
        ${ARGN})
    if(XPI_SUPPORT_STATUS)
        croft_json_string(support_status_json "${XPI_SUPPORT_STATUS}")
    else()
        set(support_status_json "null")
    endif()
    if(XPI_APPLICABILITY)
        croft_json_string(applicability_json "${XPI_APPLICABILITY}")
    else()
        set(applicability_json "null")
    endif()
    if(XPI_DESCRIPTION)
        croft_json_string(description_json "${XPI_DESCRIPTION}")
    else()
        set(description_json "null")
    endif()
    croft_json_array(artifacts_json ${XPI_ARTIFACTS})
    croft_json_array(substrates_json ${XPI_SUBSTRATES})
    croft_json_array(declared_worlds_json ${XPI_DECLARED_WORLDS})
    croft_json_array(expanded_surfaces_json ${XPI_EXPANDED_SURFACES})
    croft_json_array(helper_interfaces_json ${XPI_HELPER_INTERFACES})
    set(entry "    {\n      \"name\": \"${name}\",\n      \"support_status\": ${support_status_json},\n      \"applicability\": ${applicability_json},\n      \"description\": ${description_json},\n      \"artifacts\": ${artifacts_json},\n      \"substrates\": ${substrates_json},\n      \"declared_worlds\": ${declared_worlds_json},\n      \"expanded_surfaces\": ${expanded_surfaces_json},\n      \"helper_interfaces\": ${helper_interfaces_json}\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_XPI_BUNDLE_ENTRIES "${entry}")
endfunction()

function(croft_register_example target)
    cmake_parse_arguments(EXAMPLE "" "FOCUS" "TAGS;REQUIRES" ${ARGN})
    croft_json_string(focus_json "${EXAMPLE_FOCUS}")
    croft_json_array(tags_json ${EXAMPLE_TAGS})
    croft_json_array(requires_json ${EXAMPLE_REQUIRES})
    set(entry "    {\n      \"name\": \"${target}\",\n      \"focus\": ${focus_json},\n      \"tags\": ${tags_json},\n      \"requires\": ${requires_json}\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_EXAMPLE_ENTRIES "${entry}")
    set_property(GLOBAL APPEND PROPERTY CROFT_EXAMPLE_TARGETS "${target}")
endfunction()

function(croft_register_wasm_guest target kind output_path)
    set(entry "    {\n      \"name\": \"${target}\",\n      \"kind\": \"${kind}\",\n      \"path\": \"${output_path}\"\n    }")
    set_property(GLOBAL APPEND PROPERTY CROFT_WASM_GUEST_ENTRIES "${entry}")
    set_property(GLOBAL APPEND PROPERTY CROFT_WASM_GUEST_TARGETS "${target}")
endfunction()

function(croft_add_example target)
    cmake_parse_arguments(EXAMPLE "" "FOCUS" "SOURCES;LINKS;REQUIRES;TAGS;COMPILE_DEFINITIONS" ${ARGN})
    add_executable(${target} EXCLUDE_FROM_ALL ${EXAMPLE_SOURCES})
    target_include_directories(${target} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )
    if(EXAMPLE_LINKS)
        target_link_libraries(${target} PRIVATE ${EXAMPLE_LINKS})
    endif()
    if(EXAMPLE_COMPILE_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${EXAMPLE_COMPILE_DEFINITIONS})
    endif()
    croft_register_example(${target}
        FOCUS "${EXAMPLE_FOCUS}"
        TAGS ${EXAMPLE_TAGS}
        REQUIRES ${EXAMPLE_REQUIRES}
    )
endfunction()

function(croft_set_target_profile target profile)
    set_property(TARGET ${target} PROPERTY CROFT_TARGET_PROFILE "${profile}")
endfunction()

function(croft_mark_sapling_profile target)
    cmake_parse_arguments(SAPLING_PROFILE "THREADED" "" "" ${ARGN})
    if(SAPLING_PROFILE_THREADED)
        set(profile threaded)
    else()
        set(profile single-thread)
    endif()
    croft_set_target_profile(${target} "${profile}")
endfunction()

function(croft_get_target_profile out_var target)
    if(NOT TARGET ${target})
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    get_target_property(profile ${target} CROFT_TARGET_PROFILE)
    if(NOT profile OR profile MATCHES "-NOTFOUND$")
        set(profile "")
    endif()
    set(${out_var} "${profile}" PARENT_SCOPE)
endfunction()

function(croft_validate_profile_dependency target dep)
    croft_get_target_profile(target_profile ${target})
    croft_get_target_profile(dep_profile ${dep})
    if(target_profile AND dep_profile AND NOT target_profile STREQUAL dep_profile)
        message(FATAL_ERROR
            "Sapling profile mismatch: target '${target}' uses the '${target_profile}' profile, "
            "but dependency '${dep}' uses '${dep_profile}'. "
            "Keep single-thread and threaded variants in separate target graphs.")
    endif()
endfunction()

function(croft_link_libraries_checked target)
    cmake_parse_arguments(CROFT_LINK "" "" "PUBLIC;PRIVATE;INTERFACE" ${ARGN})
    foreach(scope IN ITEMS PUBLIC PRIVATE INTERFACE)
        if(CROFT_LINK_${scope})
            foreach(dep IN LISTS CROFT_LINK_${scope})
                if(TARGET ${dep})
                    croft_validate_profile_dependency(${target} ${dep})
                endif()
            endforeach()
            target_link_libraries(${target} ${scope} ${CROFT_LINK_${scope}})
        endif()
    endforeach()
endfunction()

function(croft_add_wat_wasm_guest target)
    cmake_parse_arguments(WASM_GUEST "" "SOURCE;OUTPUT_NAME;OUTPUT_SUBDIR;OUT_VAR" "DEPENDS" ${ARGN})
    if(NOT TARGET croft_wasm_wasm3 OR NOT CROFT_WAT2WASM_TOOL)
        return()
    endif()
    if(NOT WASM_GUEST_SOURCE)
        message(FATAL_ERROR "croft_add_wat_wasm_guest(${target}) requires SOURCE")
    endif()

    if(WASM_GUEST_OUTPUT_NAME)
        set(output_name "${WASM_GUEST_OUTPUT_NAME}")
    else()
        set(output_name "${target}")
    endif()
    if(WASM_GUEST_OUTPUT_SUBDIR)
        set(output_subdir "${WASM_GUEST_OUTPUT_SUBDIR}")
    else()
        set(output_subdir "examples")
    endif()

    set(output_path "${CMAKE_CURRENT_BINARY_DIR}/${output_subdir}/${output_name}.wasm")
    add_custom_command(
        OUTPUT ${output_path}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${output_subdir}"
        COMMAND ${CROFT_WAT2WASM_TOOL} "${WASM_GUEST_SOURCE}" -o ${output_path}
        DEPENDS ${CROFT_WAT2WASM_DEP} "${WASM_GUEST_SOURCE}" ${WASM_GUEST_DEPENDS}
        COMMENT "Compiling ${WASM_GUEST_SOURCE} to Wasm..."
    )
    add_custom_target(${target} DEPENDS ${output_path})
    set_property(TARGET ${target} PROPERTY CROFT_WASM_GUEST_PATH "${output_path}")
    croft_register_wasm_guest(${target} wat "${output_path}")
    if(WASM_GUEST_OUT_VAR)
        set(${WASM_GUEST_OUT_VAR} "${output_path}" PARENT_SCOPE)
    endif()
endfunction()

function(croft_add_freestanding_c_wasm_guest target)
    cmake_parse_arguments(WASM_GUEST
        ""
        "OUTPUT_NAME;OUTPUT_SUBDIR;OUT_VAR"
        "SOURCES;DEPENDS;INCLUDE_DIRS;COMPILE_DEFINITIONS;COMPILE_OPTIONS;LINK_OPTIONS"
        ${ARGN})
    if(NOT TARGET croft_wasm_wasm3 OR NOT CROFT_LLVM_CLANG_TOOL OR NOT CROFT_WASM_LD_TOOL)
        return()
    endif()
    if(NOT WASM_GUEST_SOURCES)
        message(FATAL_ERROR "croft_add_freestanding_c_wasm_guest(${target}) requires SOURCES")
    endif()

    if(WASM_GUEST_OUTPUT_NAME)
        set(output_name "${WASM_GUEST_OUTPUT_NAME}")
    else()
        set(output_name "${target}")
    endif()
    if(WASM_GUEST_OUTPUT_SUBDIR)
        set(output_subdir "${WASM_GUEST_OUTPUT_SUBDIR}")
    else()
        set(output_subdir "examples")
    endif()

    set(output_path "${CMAKE_CURRENT_BINARY_DIR}/${output_subdir}/${output_name}.wasm")
    set(compile_cmd
        ${CROFT_LLVM_CLANG_TOOL}
        --target=wasm32-unknown-unknown
        -std=c11
        -Oz
        -ffreestanding
        -fno-builtin
        -fno-stack-protector)

    foreach(definition IN LISTS WASM_GUEST_COMPILE_DEFINITIONS)
        list(APPEND compile_cmd "-D${definition}")
    endforeach()
    foreach(option IN LISTS WASM_GUEST_COMPILE_OPTIONS)
        list(APPEND compile_cmd "${option}")
    endforeach()
    foreach(include_dir IN LISTS WASM_GUEST_INCLUDE_DIRS)
        list(APPEND compile_cmd "-I${include_dir}")
    endforeach()

    list(APPEND compile_cmd
        ${WASM_GUEST_SOURCES}
        -nostdlib
        -Wl,--no-entry
        -Wl,--allow-undefined
        -Wl,--export-memory
        -Wl,--initial-memory=131072
        -Wl,-z,stack-size=32768)

    foreach(link_option IN LISTS WASM_GUEST_LINK_OPTIONS)
        list(APPEND compile_cmd "${link_option}")
    endforeach()

    list(APPEND compile_cmd "-fuse-ld=${CROFT_WASM_LD_TOOL}" -o ${output_path})

    add_custom_command(
        OUTPUT ${output_path}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${output_subdir}"
        COMMAND ${compile_cmd}
        DEPENDS ${WASM_GUEST_SOURCES} ${WASM_GUEST_DEPENDS}
        COMMENT "Compiling ${output_name}.c to Wasm..."
    )
    add_custom_target(${target} DEPENDS ${output_path})
    set_property(TARGET ${target} PROPERTY CROFT_WASM_GUEST_PATH "${output_path}")
    croft_register_wasm_guest(${target} freestanding-c "${output_path}")
    if(WASM_GUEST_OUT_VAR)
        set(${WASM_GUEST_OUT_VAR} "${output_path}" PARENT_SCOPE)
    endif()
endfunction()

function(croft_add_wit_wasm_guest target)
    cmake_parse_arguments(WIT_GUEST
        ""
        "OUTPUT_NAME;OUTPUT_SUBDIR;OUT_VAR"
        "SOURCES;DEPENDS;INCLUDE_DIRS;COMPILE_DEFINITIONS;COMPILE_OPTIONS;LINK_OPTIONS"
        ${ARGN})
    set(all_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/wit_runtime_support_freestanding.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/wit_guest_exports_runtime.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/thatch_core.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/wit_skip.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/wit_world_runtime.c"
        ${WIT_GUEST_SOURCES})
    set(all_depends
        "${CMAKE_CURRENT_SOURCE_DIR}/include/croft/wit_runtime_support.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/croft/wit_guest_exports_runtime.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/croft/wit_guest_runtime.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/croft/wit_croft_wasm_guest.h"
        ${WIT_GUEST_DEPENDS})
    set(all_defs
        CROFT_WIT_FREESTANDING=1
        ${WIT_GUEST_COMPILE_DEFINITIONS})

    set(_croft_wit_guest_output_path "")

    croft_add_freestanding_c_wasm_guest(${target}
        OUTPUT_NAME "${WIT_GUEST_OUTPUT_NAME}"
        OUTPUT_SUBDIR "${WIT_GUEST_OUTPUT_SUBDIR}"
        OUT_VAR _croft_wit_guest_output_path
        SOURCES ${all_sources}
        DEPENDS ${all_depends}
        INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            ${WIT_GUEST_INCLUDE_DIRS}
        COMPILE_DEFINITIONS ${all_defs}
        COMPILE_OPTIONS ${WIT_GUEST_COMPILE_OPTIONS}
        LINK_OPTIONS ${WIT_GUEST_LINK_OPTIONS}
    )

    if(WIT_GUEST_OUT_VAR)
        set(${WIT_GUEST_OUT_VAR} "${_croft_wit_guest_output_path}" PARENT_SCOPE)
    endif()
endfunction()

# pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ── WIT Schema Codegen ──────────────────────────────────────────────────
set(CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/wasi/0.2.9/proposals")
set(CROFT_WASI_CURRENT_MACHINE_0_2_9_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/wasi-current-machine/0.2.9")
file(GLOB_RECURSE CROFT_WASI_VENDOR_0_2_9_WIT_INPUTS CONFIGURE_DEPENDS
    "${CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR}/*/wit/*.wit")
if(CROFT_WASI_PROPOSALS_DIR)
    set(CROFT_WASI_CURRENT_MACHINE_PACKAGE_ROOT_ARGS "")
    set(CROFT_WASI_VENDOR_REPORT_EXTERNAL_ROOT "${CROFT_WASI_PROPOSALS_DIR}")
else()
    set(CROFT_WASI_CURRENT_MACHINE_PACKAGE_ROOT_ARGS
        --package-root "${CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR}")
    set(CROFT_WASI_VENDOR_REPORT_EXTERNAL_ROOT "-")
endif()

set(WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.h")
set(WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.c")
set(WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_schema_dbis.manifest")

set(COMMON_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_common_core.h")
set(COMMON_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_common_core.c")
set(COMMON_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_common_core.manifest")

set(HOST_FS_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_fs.h")
set(HOST_FS_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_fs.c")
set(HOST_FS_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_fs.manifest")

set(HOST_CLOCK_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clock.h")
set(HOST_CLOCK_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clock.c")
set(HOST_CLOCK_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clock.manifest")

set(HOST_WINDOW_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_window.h")
set(HOST_WINDOW_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_window.c")
set(HOST_WINDOW_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_window.manifest")

set(HOST_GPU2D_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_gpu2d.h")
set(HOST_GPU2D_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_gpu2d.c")
set(HOST_GPU2D_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_gpu2d.manifest")

set(HOST_MENU_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_menu.h")
set(HOST_MENU_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_menu.c")
set(HOST_MENU_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_menu.manifest")

set(HOST_POPUP_MENU_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_popup_menu.h")
set(HOST_POPUP_MENU_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_popup_menu.c")
set(HOST_POPUP_MENU_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_popup_menu.manifest")

set(HOST_CLIPBOARD_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clipboard.h")
set(HOST_CLIPBOARD_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clipboard.c")
set(HOST_CLIPBOARD_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_clipboard.manifest")

set(HOST_EDITOR_INPUT_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_editor_input.h")
set(HOST_EDITOR_INPUT_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_editor_input.c")
set(HOST_EDITOR_INPUT_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_editor_input.manifest")

set(HOST_A11Y_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_a11y.h")
set(HOST_A11Y_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_a11y.c")
set(HOST_A11Y_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_host_a11y.manifest")

set(TEST_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.h")
set(TEST_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.c")
set(TEST_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/test_result_types.manifest")

set(WASI_CLI_ENVIRONMENT_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_environment.h")
set(WASI_CLI_ENVIRONMENT_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_environment.c")
set(WASI_CLI_ENVIRONMENT_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_environment.manifest")

set(WASI_CLI_STDIO_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_stdio.h")
set(WASI_CLI_STDIO_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_stdio.c")
set(WASI_CLI_STDIO_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_stdio.manifest")

set(WASI_CLI_TERMINAL_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_terminal.h")
set(WASI_CLI_TERMINAL_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_terminal.c")
set(WASI_CLI_TERMINAL_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_terminal.manifest")

set(WASI_CLI_RUN_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_run.h")
set(WASI_CLI_RUN_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_run.c")
set(WASI_CLI_RUN_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_run.manifest")

set(WASI_CLI_IMPORTS_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_imports.h")
set(WASI_CLI_IMPORTS_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_imports.c")
set(WASI_CLI_IMPORTS_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_imports.manifest")

set(WASI_CLI_COMMAND_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_command.h")
set(WASI_CLI_COMMAND_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_command.c")
set(WASI_CLI_COMMAND_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_cli_command.manifest")

set(WASI_RANDOM_RANDOM_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_random.h")
set(WASI_RANDOM_RANDOM_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_random.c")
set(WASI_RANDOM_RANDOM_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_random.manifest")

set(WASI_RANDOM_INSECURE_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure.h")
set(WASI_RANDOM_INSECURE_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure.c")
set(WASI_RANDOM_INSECURE_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure.manifest")

set(WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure_seed.h")
set(WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure_seed.c")
set(WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_insecure_seed.manifest")

set(WASI_RANDOM_WORLD_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_world.h")
set(WASI_RANDOM_WORLD_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_world.c")
set(WASI_RANDOM_WORLD_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_random_world.manifest")

set(WASI_CLOCKS_TIMEZONE_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_timezone.h")
set(WASI_CLOCKS_TIMEZONE_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_timezone.c")
set(WASI_CLOCKS_TIMEZONE_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_timezone.manifest")

set(WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_wall_clock.h")
set(WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_wall_clock.c")
set(WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_wall_clock.manifest")

set(WASI_IO_POLL_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_poll.h")
set(WASI_IO_POLL_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_poll.c")
set(WASI_IO_POLL_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_poll.manifest")

set(WASI_IO_STREAMS_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_streams.h")
set(WASI_IO_STREAMS_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_streams.c")
set(WASI_IO_STREAMS_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_streams.manifest")

set(WASI_IO_WORLD_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_world.h")
set(WASI_IO_WORLD_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_world.c")
set(WASI_IO_WORLD_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_io_world.manifest")

set(WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_monotonic_clock.h")
set(WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_monotonic_clock.c")
set(WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_monotonic_clock.manifest")

set(WASI_CLOCKS_WORLD_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_world.h")
set(WASI_CLOCKS_WORLD_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_world.c")
set(WASI_CLOCKS_WORLD_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_clocks_world.manifest")

set(WASI_FILESYSTEM_TYPES_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_types.h")
set(WASI_FILESYSTEM_TYPES_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_types.c")
set(WASI_FILESYSTEM_TYPES_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_types.manifest")

set(WASI_FILESYSTEM_PREOPENS_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_preopens.h")
set(WASI_FILESYSTEM_PREOPENS_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_preopens.c")
set(WASI_FILESYSTEM_PREOPENS_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_preopens.manifest")

set(WASI_FILESYSTEM_WORLD_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_world.h")
set(WASI_FILESYSTEM_WORLD_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_world.c")
set(WASI_FILESYSTEM_WORLD_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_wasi_filesystem_world.manifest")

set(SCOPED_NAMES_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/scoped_names.h")
set(SCOPED_NAMES_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/scoped_names.c")
set(SCOPED_NAMES_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/scoped_names.manifest")

set(OPERATION_COLLISIONS_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/operation_collisions.h")
set(OPERATION_COLLISIONS_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/operation_collisions.c")
set(OPERATION_COLLISIONS_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/operation_collisions.manifest")

set(USE_REEXPORTS_TYPES_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/use_reexports_types.h")
set(USE_REEXPORTS_TYPES_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/use_reexports_types.c")
set(USE_REEXPORTS_TYPES_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/use_reexports_types.manifest")

set(WORLD_META_COMMAND_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_meta_command.h")
set(WORLD_META_COMMAND_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_meta_command.c")
set(WORLD_META_COMMAND_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_meta_command.manifest")

set(WORLD_INLINE_COMMAND_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_inline_command.h")
set(WORLD_INLINE_COMMAND_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_inline_command.c")
set(WORLD_INLINE_COMMAND_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/tests/generated/world_inline_command.manifest")

set(MENU_WIT_GENERATED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_menu_schema.h")
set(MENU_WIT_GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_menu_schema.c")
set(MENU_WIT_GENERATED_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/generated/wit_menu_schema.manifest")

add_executable(wit_codegen tools/wit_codegen.c)

# Main runtime schema
add_custom_command(
    OUTPUT ${WIT_GENERATED_H} ${WIT_GENERATED_C} ${WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/runtime-schema.wit"
                        --header "${WIT_GENERATED_H}" --source "${WIT_GENERATED_C}" --manifest "${WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/runtime-schema.wit"
    COMMENT "Generating Schema C Layouts from WIT..."
)
add_custom_target(wit_schema_gen DEPENDS ${WIT_GENERATED_H} ${WIT_GENERATED_C} ${WIT_GENERATED_MANIFEST})

# Common-core schema
add_custom_command(
    OUTPUT ${COMMON_WIT_GENERATED_H} ${COMMON_WIT_GENERATED_C} ${COMMON_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/common-core.wit"
                        --header "${COMMON_WIT_GENERATED_H}" --source "${COMMON_WIT_GENERATED_C}" --manifest "${COMMON_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/common-core.wit"
    COMMENT "Generating Common-Core WIT C Layouts..."
)
add_custom_target(common_wit_schema_gen DEPENDS ${COMMON_WIT_GENERATED_H} ${COMMON_WIT_GENERATED_C} ${COMMON_WIT_GENERATED_MANIFEST})

# Host-fs schema
add_custom_command(
    OUTPUT ${HOST_FS_WIT_GENERATED_H} ${HOST_FS_WIT_GENERATED_C} ${HOST_FS_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-fs.wit"
                        --header "${HOST_FS_WIT_GENERATED_H}" --source "${HOST_FS_WIT_GENERATED_C}" --manifest "${HOST_FS_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-fs.wit"
    COMMENT "Generating Host-FS WIT C Layouts..."
)
add_custom_target(host_fs_wit_schema_gen DEPENDS ${HOST_FS_WIT_GENERATED_H} ${HOST_FS_WIT_GENERATED_C} ${HOST_FS_WIT_GENERATED_MANIFEST})

# Host-clock schema
add_custom_command(
    OUTPUT ${HOST_CLOCK_WIT_GENERATED_H} ${HOST_CLOCK_WIT_GENERATED_C} ${HOST_CLOCK_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-clock.wit"
                        --header "${HOST_CLOCK_WIT_GENERATED_H}" --source "${HOST_CLOCK_WIT_GENERATED_C}" --manifest "${HOST_CLOCK_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-clock.wit"
    COMMENT "Generating Host-Clock WIT C Layouts..."
)
add_custom_target(host_clock_wit_schema_gen DEPENDS ${HOST_CLOCK_WIT_GENERATED_H} ${HOST_CLOCK_WIT_GENERATED_C} ${HOST_CLOCK_WIT_GENERATED_MANIFEST})

# Host-window schema
add_custom_command(
    OUTPUT ${HOST_WINDOW_WIT_GENERATED_H} ${HOST_WINDOW_WIT_GENERATED_C} ${HOST_WINDOW_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-window.wit"
                        --header "${HOST_WINDOW_WIT_GENERATED_H}" --source "${HOST_WINDOW_WIT_GENERATED_C}" --manifest "${HOST_WINDOW_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-window.wit"
    COMMENT "Generating Host-Window WIT C Layouts..."
)
add_custom_target(host_window_wit_schema_gen DEPENDS ${HOST_WINDOW_WIT_GENERATED_H} ${HOST_WINDOW_WIT_GENERATED_C} ${HOST_WINDOW_WIT_GENERATED_MANIFEST})

# Host-gpu2d schema
add_custom_command(
    OUTPUT ${HOST_GPU2D_WIT_GENERATED_H} ${HOST_GPU2D_WIT_GENERATED_C} ${HOST_GPU2D_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-gpu2d.wit"
                        --header "${HOST_GPU2D_WIT_GENERATED_H}" --source "${HOST_GPU2D_WIT_GENERATED_C}" --manifest "${HOST_GPU2D_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-gpu2d.wit"
    COMMENT "Generating Host-GPU2D WIT C Layouts..."
)
add_custom_target(host_gpu2d_wit_schema_gen DEPENDS ${HOST_GPU2D_WIT_GENERATED_H} ${HOST_GPU2D_WIT_GENERATED_C} ${HOST_GPU2D_WIT_GENERATED_MANIFEST})

# Host-menu schema
add_custom_command(
    OUTPUT ${HOST_MENU_WIT_GENERATED_H} ${HOST_MENU_WIT_GENERATED_C} ${HOST_MENU_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-menu.wit"
                        --header "${HOST_MENU_WIT_GENERATED_H}" --source "${HOST_MENU_WIT_GENERATED_C}" --manifest "${HOST_MENU_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-menu.wit"
    COMMENT "Generating Host-Menu WIT C Layouts..."
)
add_custom_target(host_menu_wit_schema_gen DEPENDS ${HOST_MENU_WIT_GENERATED_H} ${HOST_MENU_WIT_GENERATED_C} ${HOST_MENU_WIT_GENERATED_MANIFEST})

# Host-popup-menu schema
add_custom_command(
    OUTPUT ${HOST_POPUP_MENU_WIT_GENERATED_H} ${HOST_POPUP_MENU_WIT_GENERATED_C} ${HOST_POPUP_MENU_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-popup-menu.wit"
                        --header "${HOST_POPUP_MENU_WIT_GENERATED_H}" --source "${HOST_POPUP_MENU_WIT_GENERATED_C}" --manifest "${HOST_POPUP_MENU_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-popup-menu.wit"
    COMMENT "Generating Host-Popup-Menu WIT C Layouts..."
)
add_custom_target(host_popup_menu_wit_schema_gen DEPENDS ${HOST_POPUP_MENU_WIT_GENERATED_H} ${HOST_POPUP_MENU_WIT_GENERATED_C} ${HOST_POPUP_MENU_WIT_GENERATED_MANIFEST})

# Host-clipboard schema
add_custom_command(
    OUTPUT ${HOST_CLIPBOARD_WIT_GENERATED_H} ${HOST_CLIPBOARD_WIT_GENERATED_C} ${HOST_CLIPBOARD_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-clipboard.wit"
                        --header "${HOST_CLIPBOARD_WIT_GENERATED_H}" --source "${HOST_CLIPBOARD_WIT_GENERATED_C}" --manifest "${HOST_CLIPBOARD_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-clipboard.wit"
    COMMENT "Generating Host-Clipboard WIT C Layouts..."
)
add_custom_target(host_clipboard_wit_schema_gen DEPENDS ${HOST_CLIPBOARD_WIT_GENERATED_H} ${HOST_CLIPBOARD_WIT_GENERATED_C} ${HOST_CLIPBOARD_WIT_GENERATED_MANIFEST})

# Host-editor-input schema
add_custom_command(
    OUTPUT ${HOST_EDITOR_INPUT_WIT_GENERATED_H} ${HOST_EDITOR_INPUT_WIT_GENERATED_C} ${HOST_EDITOR_INPUT_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-editor-input.wit"
                        --header "${HOST_EDITOR_INPUT_WIT_GENERATED_H}" --source "${HOST_EDITOR_INPUT_WIT_GENERATED_C}" --manifest "${HOST_EDITOR_INPUT_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-editor-input.wit"
    COMMENT "Generating Host-Editor-Input WIT C Layouts..."
)
add_custom_target(host_editor_input_wit_schema_gen DEPENDS ${HOST_EDITOR_INPUT_WIT_GENERATED_H} ${HOST_EDITOR_INPUT_WIT_GENERATED_C} ${HOST_EDITOR_INPUT_WIT_GENERATED_MANIFEST})

# Host-a11y schema
add_custom_command(
    OUTPUT ${HOST_A11Y_WIT_GENERATED_H} ${HOST_A11Y_WIT_GENERATED_C} ${HOST_A11Y_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-a11y.wit"
                        --header "${HOST_A11Y_WIT_GENERATED_H}" --source "${HOST_A11Y_WIT_GENERATED_C}" --manifest "${HOST_A11Y_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/host-a11y.wit"
    COMMENT "Generating Host-A11Y WIT C Layouts..."
)
add_custom_target(host_a11y_wit_schema_gen DEPENDS ${HOST_A11Y_WIT_GENERATED_H} ${HOST_A11Y_WIT_GENERATED_C} ${HOST_A11Y_WIT_GENERATED_MANIFEST})

# Test fixture schema
add_custom_command(
    OUTPUT ${TEST_WIT_GENERATED_H} ${TEST_WIT_GENERATED_C} ${TEST_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/result-test.wit"
                        --header "${TEST_WIT_GENERATED_H}" --source "${TEST_WIT_GENERATED_C}" --manifest "${TEST_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/result-test.wit"
    COMMENT "Generating Test Schema C Layouts from WIT..."
)
add_custom_target(test_wit_schema_gen DEPENDS ${TEST_WIT_GENERATED_H} ${TEST_WIT_GENERATED_C} ${TEST_WIT_GENERATED_MANIFEST})

croft_define_wasi_wit_schema(WASI_CLI_ENVIRONMENT
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/environment.wit"
    COMMENT "Generating WASI CLI environment bindings...")
croft_define_wasi_wit_schema(WASI_CLI_STDIO
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/stdio.wit"
    COMMENT "Generating WASI CLI stdio bindings...")
croft_define_wasi_wit_schema(WASI_CLI_TERMINAL
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/terminal.wit"
    COMMENT "Generating WASI CLI terminal bindings...")
croft_define_wasi_wit_schema(WASI_CLI_RUN
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/run.wit"
    COMMENT "Generating WASI CLI run bindings...")
croft_define_wasi_wit_schema(WASI_CLI_IMPORTS
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/imports.wit"
    COMMENT "Generating WASI CLI imports world bindings...")
croft_define_wasi_wit_schema(WASI_CLI_COMMAND
    CURRENT_MACHINE
    RELATIVE_PATH "cli/wit/command.wit"
    COMMENT "Generating WASI CLI command world bindings...")
croft_define_wasi_wit_schema(WASI_RANDOM_RANDOM
    RELATIVE_PATH "random/wit/random.wit"
    COMMENT "Generating WASI random bindings...")
croft_define_wasi_wit_schema(WASI_RANDOM_INSECURE
    RELATIVE_PATH "random/wit/insecure.wit"
    COMMENT "Generating WASI random insecure bindings...")
croft_define_wasi_wit_schema(WASI_RANDOM_INSECURE_SEED
    RELATIVE_PATH "random/wit/insecure-seed.wit"
    COMMENT "Generating WASI random insecure-seed bindings...")
croft_define_wasi_wit_schema(WASI_RANDOM_WORLD
    RELATIVE_PATH "random/wit/world.wit"
    COMMENT "Generating WASI random imports world bindings...")
croft_define_wasi_wit_schema(WASI_CLOCKS_TIMEZONE
    RELATIVE_PATH "clocks/wit/timezone.wit"
    COMMENT "Generating WASI clocks timezone bindings...")
croft_define_wasi_wit_schema(WASI_CLOCKS_WALL_CLOCK
    RELATIVE_PATH "clocks/wit/wall-clock.wit"
    COMMENT "Generating WASI clocks wall-clock bindings...")
croft_define_wasi_wit_schema(WASI_IO_POLL
    RELATIVE_PATH "io/wit/poll.wit"
    COMMENT "Generating WASI io/poll bindings...")
croft_define_wasi_wit_schema(WASI_IO_STREAMS
    RELATIVE_PATH "io/wit/streams.wit"
    COMMENT "Generating WASI io/streams bindings...")
croft_define_wasi_wit_schema(WASI_IO_WORLD
    RELATIVE_PATH "io/wit/world.wit"
    COMMENT "Generating WASI io imports world bindings...")
croft_define_wasi_wit_schema(WASI_CLOCKS_MONOTONIC_CLOCK
    RELATIVE_PATH "clocks/wit/monotonic-clock.wit"
    COMMENT "Generating WASI clocks monotonic-clock bindings..."
    EXTRA_DEPENDS "${WASI_IO_POLL_WIT_PATH}")
croft_define_wasi_wit_schema(WASI_CLOCKS_WORLD
    RELATIVE_PATH "clocks/wit/world.wit"
    COMMENT "Generating WASI clocks imports world bindings..."
    EXTRA_DEPENDS "${WASI_IO_POLL_WIT_PATH}")
croft_define_wasi_wit_schema(WASI_FILESYSTEM_TYPES
    RELATIVE_PATH "filesystem/wit/types.wit"
    COMMENT "Generating WASI filesystem/types bindings..."
    EXTRA_DEPENDS
        "${WASI_IO_STREAMS_WIT_PATH}"
        "${WASI_CLOCKS_WALL_CLOCK_WIT_PATH}")
croft_define_wasi_wit_schema(WASI_FILESYSTEM_PREOPENS
    RELATIVE_PATH "filesystem/wit/preopens.wit"
    COMMENT "Generating WASI filesystem/preopens bindings..."
    EXTRA_DEPENDS "${WASI_FILESYSTEM_TYPES_WIT_PATH}")
croft_define_wasi_wit_schema(WASI_FILESYSTEM_WORLD
    RELATIVE_PATH "filesystem/wit/world.wit"
    COMMENT "Generating WASI filesystem imports world bindings..."
    EXTRA_DEPENDS
        "${WASI_FILESYSTEM_TYPES_WIT_PATH}"
        "${WASI_FILESYSTEM_PREOPENS_WIT_PATH}")

# Scoped-name regression fixture
add_custom_command(
    OUTPUT ${SCOPED_NAMES_WIT_GENERATED_H} ${SCOPED_NAMES_WIT_GENERATED_C} ${SCOPED_NAMES_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/scoped-names.wit"
                        --header "${SCOPED_NAMES_WIT_GENERATED_H}" --source "${SCOPED_NAMES_WIT_GENERATED_C}" --manifest "${SCOPED_NAMES_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/scoped-names.wit"
    COMMENT "Generating scoped-name WIT regression fixture bindings..."
)
add_custom_target(scoped_names_wit_schema_gen
    DEPENDS ${SCOPED_NAMES_WIT_GENERATED_H}
            ${SCOPED_NAMES_WIT_GENERATED_C}
            ${SCOPED_NAMES_WIT_GENERATED_MANIFEST})

# Operation-lowering regression fixture
add_custom_command(
    OUTPUT ${OPERATION_COLLISIONS_WIT_GENERATED_H} ${OPERATION_COLLISIONS_WIT_GENERATED_C} ${OPERATION_COLLISIONS_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/operation-collisions.wit"
                        --header "${OPERATION_COLLISIONS_WIT_GENERATED_H}" --source "${OPERATION_COLLISIONS_WIT_GENERATED_C}" --manifest "${OPERATION_COLLISIONS_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/operation-collisions.wit"
    COMMENT "Generating operation-lowering WIT regression fixture bindings..."
)
add_custom_target(operation_collisions_wit_schema_gen
    DEPENDS ${OPERATION_COLLISIONS_WIT_GENERATED_H}
            ${OPERATION_COLLISIONS_WIT_GENERATED_C}
            ${OPERATION_COLLISIONS_WIT_GENERATED_MANIFEST})

# Transitive-use regression fixture
add_custom_command(
    OUTPUT ${USE_REEXPORTS_TYPES_WIT_GENERATED_H} ${USE_REEXPORTS_TYPES_WIT_GENERATED_C} ${USE_REEXPORTS_TYPES_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/use-reexports/types.wit"
                        --header "${USE_REEXPORTS_TYPES_WIT_GENERATED_H}" --source "${USE_REEXPORTS_TYPES_WIT_GENERATED_C}" --manifest "${USE_REEXPORTS_TYPES_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/use-reexports/types.wit"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/use-reexports/streams.wit"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/use-reexports/error.wit"
    COMMENT "Generating transitive-use WIT regression fixture bindings..."
)
add_custom_target(use_reexports_types_wit_schema_gen
    DEPENDS ${USE_REEXPORTS_TYPES_WIT_GENERATED_H}
            ${USE_REEXPORTS_TYPES_WIT_GENERATED_C}
            ${USE_REEXPORTS_TYPES_WIT_GENERATED_MANIFEST})

# World/manifest regression fixture
add_custom_command(
    OUTPUT ${WORLD_META_COMMAND_WIT_GENERATED_H} ${WORLD_META_COMMAND_WIT_GENERATED_C} ${WORLD_META_COMMAND_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-meta/command.wit"
                        --header "${WORLD_META_COMMAND_WIT_GENERATED_H}" --source "${WORLD_META_COMMAND_WIT_GENERATED_C}" --manifest "${WORLD_META_COMMAND_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-meta/command.wit"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-meta/world.wit"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-meta/environment.wit"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-meta/run.wit"
    COMMENT "Generating world/manifest WIT regression fixture bindings..."
)
add_custom_target(world_meta_command_wit_schema_gen
    DEPENDS ${WORLD_META_COMMAND_WIT_GENERATED_H}
            ${WORLD_META_COMMAND_WIT_GENERATED_C}
            ${WORLD_META_COMMAND_WIT_GENERATED_MANIFEST})

# Inline-world regression fixture
add_custom_command(
    OUTPUT ${WORLD_INLINE_COMMAND_WIT_GENERATED_H} ${WORLD_INLINE_COMMAND_WIT_GENERATED_C} ${WORLD_INLINE_COMMAND_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-inline/command.wit"
                        --header "${WORLD_INLINE_COMMAND_WIT_GENERATED_H}" --source "${WORLD_INLINE_COMMAND_WIT_GENERATED_C}" --manifest "${WORLD_INLINE_COMMAND_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/fixtures/world-inline/command.wit"
    COMMENT "Generating inline-world WIT regression fixture bindings..."
)
add_custom_target(world_inline_command_wit_schema_gen
    DEPENDS ${WORLD_INLINE_COMMAND_WIT_GENERATED_H}
            ${WORLD_INLINE_COMMAND_WIT_GENERATED_C}
            ${WORLD_INLINE_COMMAND_WIT_GENERATED_MANIFEST})

# Menu Schema
add_custom_command(
    OUTPUT ${MENU_WIT_GENERATED_H} ${MENU_WIT_GENERATED_C} ${MENU_WIT_GENERATED_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND wit_codegen --wit "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/menu-schema.wit"
                        --header "${MENU_WIT_GENERATED_H}" --source "${MENU_WIT_GENERATED_C}" --manifest "${MENU_WIT_GENERATED_MANIFEST}"
    DEPENDS wit_codegen "${CMAKE_CURRENT_SOURCE_DIR}/schemas/wit/menu-schema.wit"
    COMMENT "Generating Menu Schema C Layouts from WIT..."
)
add_custom_target(menu_wit_schema_gen DEPENDS ${MENU_WIT_GENERATED_H} ${MENU_WIT_GENERATED_C} ${MENU_WIT_GENERATED_MANIFEST})

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
set(CROFT_TGFX_REQUEST_OPENGL OFF)
set(CROFT_TGFX_REQUEST_METAL OFF)

if(DEFINED TGFX_USE_METAL)
    if(TGFX_USE_METAL)
        set(CROFT_TGFX_REQUEST_METAL ON)
    endif()
elseif(APPLE)
    set(CROFT_TGFX_REQUEST_METAL ON)
endif()

if(DEFINED TGFX_USE_OPENGL)
    if(TGFX_USE_OPENGL)
        set(CROFT_TGFX_REQUEST_OPENGL ON)
    endif()
elseif(NOT CROFT_TGFX_REQUEST_METAL)
    set(CROFT_TGFX_REQUEST_OPENGL ON)
endif()

if(CROFT_TGFX_REQUEST_METAL AND CROFT_TGFX_REQUEST_OPENGL)
    message(FATAL_ERROR
        "Croft's current tgfx integration supports one backend variant per configure. "
        "Use separate build directories to compare OpenGL and Metal.")
endif()

if(NOT CROFT_TGFX_REQUEST_METAL AND NOT CROFT_TGFX_REQUEST_OPENGL)
    message(FATAL_ERROR
        "Croft requires one tgfx GPU backend variant per configure. "
        "Set TGFX_USE_METAL=ON or TGFX_USE_OPENGL=ON.")
endif()

if(CROFT_GLFW_TARGET)
    if(CROFT_TGFX_SOURCE_DIR)
        if(EXISTS "${CROFT_TGFX_SOURCE_DIR}/CMakeLists.txt")
            set(TGFX_BUILD_SVG OFF CACHE BOOL "Disable tgfx SVG module" FORCE)
            set(TGFX_BUILD_PDF OFF CACHE BOOL "Disable tgfx PDF module" FORCE)
            set(TGFX_BUILD_LAYERS OFF CACHE BOOL "Disable tgfx layers module" FORCE)
            set(TGFX_BUILD_HELLO2D OFF CACHE BOOL "Disable tgfx Hello2D demo" FORCE)
            set(TGFX_USE_PNG_DECODE OFF CACHE BOOL "Disable tgfx PNG decoding for Croft" FORCE)
            set(TGFX_USE_PNG_ENCODE OFF CACHE BOOL "Disable tgfx PNG encoding for Croft" FORCE)
            set(TGFX_USE_JPEG_DECODE OFF CACHE BOOL "Disable tgfx JPEG decoding for Croft" FORCE)
            set(TGFX_USE_JPEG_ENCODE OFF CACHE BOOL "Disable tgfx JPEG encoding for Croft" FORCE)
            set(TGFX_USE_WEBP_DECODE OFF CACHE BOOL "Disable tgfx WEBP decoding for Croft" FORCE)
            set(TGFX_USE_WEBP_ENCODE OFF CACHE BOOL "Disable tgfx WEBP encoding for Croft" FORCE)
            set(TGFX_USE_METAL ${CROFT_TGFX_REQUEST_METAL} CACHE BOOL
                "Enable tgfx Metal backend for this Croft configure" FORCE)
            set(TGFX_USE_OPENGL ${CROFT_TGFX_REQUEST_OPENGL} CACHE BOOL
                "Enable tgfx OpenGL backend for this Croft configure" FORCE)
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
add_library(croft_foundation STATIC src/runtime/croft_foundation.c)
croft_apply_public_includes(croft_foundation)
croft_register_artifact(croft_foundation foundation
    TAGS tier0 foundation base host-neutral
)

add_library(croft_host_log STATIC src/host/host_log.c)
croft_apply_public_includes(croft_host_log)
target_link_libraries(croft_host_log PUBLIC croft_foundation)
croft_register_artifact(croft_host_log host
    TAGS tier0 host log
    DEPENDS croft_foundation
)

add_library(croft_host_time STATIC src/host/host_time.c)
croft_apply_public_includes(croft_host_time)
target_link_libraries(croft_host_time PUBLIC croft_foundation)
croft_register_artifact(croft_host_time host
    TAGS tier0 host time
    DEPENDS croft_foundation
)

add_library(croft_host_thread STATIC src/host/host_thread.c)
croft_apply_public_includes(croft_host_thread)
target_link_libraries(croft_host_thread PUBLIC croft_foundation Threads::Threads)
croft_register_artifact(croft_host_thread host
    TAGS tier0 host threads
    DEPENDS croft_foundation
)

add_library(croft_thatch_core STATIC src/runtime/thatch_core.c src/runtime/wit_skip.c)
croft_apply_public_includes(croft_thatch_core)
croft_register_artifact(croft_thatch_core serialization
    TAGS thatch packed-data serialization
)

add_library(croft_wit_common_core STATIC ${COMMON_WIT_GENERATED_C})
add_dependencies(croft_wit_common_core common_wit_schema_gen)
croft_apply_public_includes(croft_wit_common_core)
target_link_libraries(croft_wit_common_core PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_common_core wit
    TAGS wit common-core resource-handles xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_fs STATIC ${HOST_FS_WIT_GENERATED_C})
add_dependencies(croft_wit_host_fs host_fs_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_fs)
target_link_libraries(croft_wit_host_fs PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_fs wit
    TAGS wit host-fs resource-handles xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_clock STATIC ${HOST_CLOCK_WIT_GENERATED_C})
add_dependencies(croft_wit_host_clock host_clock_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_clock)
target_link_libraries(croft_wit_host_clock PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_clock wit
    TAGS wit host-clock service xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_window STATIC ${HOST_WINDOW_WIT_GENERATED_C})
add_dependencies(croft_wit_host_window host_window_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_window)
target_link_libraries(croft_wit_host_window PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_window wit
    TAGS wit host-window resource-handles xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_gpu2d STATIC ${HOST_GPU2D_WIT_GENERATED_C})
add_dependencies(croft_wit_host_gpu2d host_gpu2d_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_gpu2d)
target_link_libraries(croft_wit_host_gpu2d PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_gpu2d wit
    TAGS wit host-gpu2d resource-handles service xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_menu STATIC ${HOST_MENU_WIT_GENERATED_C})
add_dependencies(croft_wit_host_menu host_menu_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_menu)
target_link_libraries(croft_wit_host_menu PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_menu wit
    TAGS wit host-menu service xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_popup_menu STATIC ${HOST_POPUP_MENU_WIT_GENERATED_C})
add_dependencies(croft_wit_host_popup_menu host_popup_menu_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_popup_menu)
target_link_libraries(croft_wit_host_popup_menu PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_popup_menu wit
    TAGS wit host-popup-menu service xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_clipboard STATIC ${HOST_CLIPBOARD_WIT_GENERATED_C})
add_dependencies(croft_wit_host_clipboard host_clipboard_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_clipboard)
target_link_libraries(croft_wit_host_clipboard PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_clipboard wit
    TAGS wit host-clipboard service xpi
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_editor_input STATIC ${HOST_EDITOR_INPUT_WIT_GENERATED_C})
add_dependencies(croft_wit_host_editor_input host_editor_input_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_editor_input)
target_link_libraries(croft_wit_host_editor_input PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_editor_input wit
    TAGS wit host-editor-input service xpi editor
    DEPENDS croft_thatch_core
)

add_library(croft_wit_host_a11y STATIC ${HOST_A11Y_WIT_GENERATED_C})
add_dependencies(croft_wit_host_a11y host_a11y_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_a11y)
target_link_libraries(croft_wit_host_a11y PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_host_a11y wit
    TAGS wit host-a11y resource-handles service xpi accessibility
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_environment STATIC ${WASI_CLI_ENVIRONMENT_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_environment wasi_cli_environment_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_environment)
target_link_libraries(croft_wit_wasi_cli_environment PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_environment wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-process-context
    EXPANDED_SURFACES "wasi:cli@0.2.9/environment"
    TAGS wit wasi cli environment service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_stdio STATIC ${WASI_CLI_STDIO_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_stdio wasi_cli_stdio_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_stdio)
target_link_libraries(croft_wit_wasi_cli_stdio PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_stdio wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-byte-streams wasi-descriptor-table
    EXPANDED_SURFACES "wasi:cli@0.2.9/stdio"
    TAGS wit wasi cli stdio service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_terminal STATIC ${WASI_CLI_TERMINAL_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_terminal wasi_cli_terminal_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_terminal)
target_link_libraries(croft_wit_wasi_cli_terminal PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_terminal wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-descriptor-table
    EXPANDED_SURFACES "wasi:cli@0.2.9/terminal"
    TAGS wit wasi cli terminal service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_run STATIC ${WASI_CLI_RUN_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_run wasi_cli_run_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_run)
target_link_libraries(croft_wit_wasi_cli_run PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_run wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-process-context
    EXPANDED_SURFACES "wasi:cli@0.2.9/run"
    TAGS wit wasi cli run service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_imports STATIC ${WASI_CLI_IMPORTS_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_imports wasi_cli_imports_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_imports)
target_link_libraries(croft_wit_wasi_cli_imports PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_imports wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-process-context wasi-byte-streams wasi-descriptor-table
    DECLARED_WORLDS "wasi:cli@0.2.9/imports"
    EXPANDED_SURFACES
        "wasi:cli@0.2.9/environment"
        "wasi:cli@0.2.9/stdio"
        "wasi:cli@0.2.9/terminal"
    TAGS wit wasi cli world imports metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_cli_command STATIC ${WASI_CLI_COMMAND_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_cli_command wasi_cli_command_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_cli_command)
target_link_libraries(croft_wit_wasi_cli_command PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_cli_command wit
    UPSTREAM_PACKAGE "wasi:cli"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-cli-stdio-terminal-current-machine
    SHARED_SUBSTRATES wasi-process-context wasi-byte-streams wasi-descriptor-table
    DECLARED_WORLDS "wasi:cli@0.2.9/command"
    EXPANDED_SURFACES
        "wasi:cli@0.2.9/environment"
        "wasi:cli@0.2.9/stdio"
        "wasi:cli@0.2.9/terminal"
        "wasi:cli@0.2.9/run"
    TAGS wit wasi cli world command metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_random_random STATIC ${WASI_RANDOM_RANDOM_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_random_random wasi_random_random_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_random_random)
target_link_libraries(croft_wit_wasi_random_random PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_random_random wit
    UPSTREAM_PACKAGE "wasi:random"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine"
    CAPABILITY_BUNDLES wasi-random-current-machine
    SHARED_SUBSTRATES wasi-system-random
    EXPANDED_SURFACES "wasi:random@0.2.9/random"
    TAGS wit wasi random service metadata current-machine
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_random_insecure STATIC ${WASI_RANDOM_INSECURE_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_random_insecure wasi_random_insecure_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_random_insecure)
target_link_libraries(croft_wit_wasi_random_insecure PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_random_insecure wit
    UPSTREAM_PACKAGE "wasi:random"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine"
    CAPABILITY_BUNDLES wasi-random-current-machine
    SHARED_SUBSTRATES wasi-system-random
    EXPANDED_SURFACES "wasi:random@0.2.9/insecure"
    TAGS wit wasi random insecure service metadata current-machine
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_random_insecure_seed STATIC ${WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_random_insecure_seed wasi_random_insecure_seed_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_random_insecure_seed)
target_link_libraries(croft_wit_wasi_random_insecure_seed PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_random_insecure_seed wit
    UPSTREAM_PACKAGE "wasi:random"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine"
    CAPABILITY_BUNDLES wasi-random-current-machine
    SHARED_SUBSTRATES wasi-system-random
    EXPANDED_SURFACES "wasi:random@0.2.9/insecure-seed"
    TAGS wit wasi random insecure-seed service metadata current-machine
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_random_world STATIC ${WASI_RANDOM_WORLD_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_random_world wasi_random_world_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_random_world)
target_link_libraries(croft_wit_wasi_random_world PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_random_world wit
    UPSTREAM_PACKAGE "wasi:random"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine"
    CAPABILITY_BUNDLES wasi-random-current-machine
    SHARED_SUBSTRATES wasi-system-random
    DECLARED_WORLDS "wasi:random@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:random@0.2.9/random"
        "wasi:random@0.2.9/insecure"
        "wasi:random@0.2.9/insecure-seed"
    TAGS wit wasi random world imports metadata current-machine
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_clocks_timezone STATIC ${WASI_CLOCKS_TIMEZONE_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_clocks_timezone wasi_clocks_timezone_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_clocks_timezone)
target_link_libraries(croft_wit_wasi_clocks_timezone PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_clocks_timezone wit
    UPSTREAM_PACKAGE "wasi:clocks"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-clocks-poll-current-machine
    SHARED_SUBSTRATES wasi-time-base
    EXPANDED_SURFACES "wasi:clocks@0.2.9/timezone"
    TAGS wit wasi clocks timezone service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_clocks_wall_clock STATIC ${WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_clocks_wall_clock wasi_clocks_wall_clock_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_clocks_wall_clock)
target_link_libraries(croft_wit_wasi_clocks_wall_clock PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_clocks_wall_clock wit
    UPSTREAM_PACKAGE "wasi:clocks"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-clocks-poll-current-machine
    SHARED_SUBSTRATES wasi-time-base
    EXPANDED_SURFACES "wasi:clocks@0.2.9/wall-clock"
    TAGS wit wasi clocks wall-clock service metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_io_poll STATIC ${WASI_IO_POLL_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_io_poll wasi_io_poll_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_io_poll)
target_link_libraries(croft_wit_wasi_io_poll PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_io_poll wit
    UPSTREAM_PACKAGE "wasi:io"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-clocks-poll-current-machine
    SHARED_SUBSTRATES wasi-pollables
    EXPANDED_SURFACES "wasi:io@0.2.9/poll"
    TAGS wit wasi io poll resource-handles metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_io_streams STATIC ${WASI_IO_STREAMS_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_io_streams wasi_io_streams_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_io_streams)
target_link_libraries(croft_wit_wasi_io_streams PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_io_streams wit
    UPSTREAM_PACKAGE "wasi:io"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES wasi-byte-streams wasi-descriptor-table
    HELPER_INTERFACES "wasi:io@0.2.9/error"
    EXPANDED_SURFACES "wasi:io@0.2.9/streams"
    TAGS wit wasi io streams resource-handles metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_io_world STATIC ${WASI_IO_WORLD_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_io_world wasi_io_world_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_io_world)
target_link_libraries(croft_wit_wasi_io_world PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_io_world wit
    UPSTREAM_PACKAGE "wasi:io"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES
        wasi-clocks-poll-current-machine
        wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES wasi-byte-streams wasi-descriptor-table wasi-pollables
    HELPER_INTERFACES "wasi:io@0.2.9/error"
    DECLARED_WORLDS "wasi:io@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:io@0.2.9/streams"
        "wasi:io@0.2.9/poll"
    TAGS wit wasi io world imports metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_clocks_monotonic_clock STATIC ${WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_clocks_monotonic_clock wasi_clocks_monotonic_clock_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_clocks_monotonic_clock)
target_link_libraries(croft_wit_wasi_clocks_monotonic_clock PUBLIC croft_thatch_core croft_wit_wasi_io_poll)
croft_register_artifact(croft_wit_wasi_clocks_monotonic_clock wit
    UPSTREAM_PACKAGE "wasi:clocks"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-clocks-poll-current-machine
    SHARED_SUBSTRATES wasi-time-base wasi-pollables
    EXPANDED_SURFACES "wasi:clocks@0.2.9/monotonic-clock"
    TAGS wit wasi clocks monotonic-clock poll service metadata current-machine unix
    DEPENDS croft_thatch_core croft_wit_wasi_io_poll
)

add_library(croft_wit_wasi_clocks_world STATIC ${WASI_CLOCKS_WORLD_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_clocks_world wasi_clocks_world_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_clocks_world)
target_link_libraries(croft_wit_wasi_clocks_world PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_clocks_world wit
    UPSTREAM_PACKAGE "wasi:clocks"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-clocks-poll-current-machine
    SHARED_SUBSTRATES wasi-time-base wasi-pollables
    DECLARED_WORLDS "wasi:clocks@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:clocks@0.2.9/monotonic-clock"
        "wasi:clocks@0.2.9/wall-clock"
        "wasi:clocks@0.2.9/timezone"
    TAGS wit wasi clocks world imports metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_filesystem_types STATIC ${WASI_FILESYSTEM_TYPES_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_filesystem_types wasi_filesystem_types_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_filesystem_types)
target_link_libraries(croft_wit_wasi_filesystem_types PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_filesystem_types wit
    UPSTREAM_PACKAGE "wasi:filesystem"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES wasi-byte-streams wasi-descriptor-table
    HELPER_INTERFACES "wasi:filesystem@0.2.9/error"
    EXPANDED_SURFACES "wasi:filesystem@0.2.9/types"
    TAGS wit wasi filesystem types resource-handles metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_filesystem_preopens STATIC ${WASI_FILESYSTEM_PREOPENS_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_filesystem_preopens wasi_filesystem_preopens_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_filesystem_preopens)
target_link_libraries(croft_wit_wasi_filesystem_preopens PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_filesystem_preopens wit
    UPSTREAM_PACKAGE "wasi:filesystem"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES wasi-descriptor-table
    EXPANDED_SURFACES "wasi:filesystem@0.2.9/preopens"
    TAGS wit wasi filesystem preopens metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_wasi_filesystem_world STATIC ${WASI_FILESYSTEM_WORLD_WIT_GENERATED_C})
add_dependencies(croft_wit_wasi_filesystem_world wasi_filesystem_world_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_filesystem_world)
target_link_libraries(croft_wit_wasi_filesystem_world PUBLIC croft_thatch_core)
croft_register_artifact(croft_wit_wasi_filesystem_world wit
    UPSTREAM_PACKAGE "wasi:filesystem"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "declaration-only"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES wasi-byte-streams wasi-descriptor-table
    HELPER_INTERFACES "wasi:filesystem@0.2.9/error"
    DECLARED_WORLDS "wasi:filesystem@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:filesystem@0.2.9/types"
        "wasi:filesystem@0.2.9/preopens"
    TAGS wit wasi filesystem world imports metadata current-machine unix
    DEPENDS croft_thatch_core
)

add_library(croft_wit_scoped_names STATIC ${SCOPED_NAMES_WIT_GENERATED_C})
add_dependencies(croft_wit_scoped_names scoped_names_wit_schema_gen)
croft_apply_public_includes(croft_wit_scoped_names)
target_link_libraries(croft_wit_scoped_names PUBLIC croft_thatch_core)

add_library(croft_wit_operation_collisions STATIC ${OPERATION_COLLISIONS_WIT_GENERATED_C})
add_dependencies(croft_wit_operation_collisions operation_collisions_wit_schema_gen)
croft_apply_public_includes(croft_wit_operation_collisions)
target_link_libraries(croft_wit_operation_collisions PUBLIC croft_thatch_core)

add_library(croft_wit_use_reexports_types STATIC ${USE_REEXPORTS_TYPES_WIT_GENERATED_C})
add_dependencies(croft_wit_use_reexports_types use_reexports_types_wit_schema_gen)
croft_apply_public_includes(croft_wit_use_reexports_types)
target_link_libraries(croft_wit_use_reexports_types PUBLIC croft_thatch_core)

add_library(croft_wit_world_meta_command STATIC ${WORLD_META_COMMAND_WIT_GENERATED_C})
add_dependencies(croft_wit_world_meta_command world_meta_command_wit_schema_gen)
croft_apply_public_includes(croft_wit_world_meta_command)
target_link_libraries(croft_wit_world_meta_command PUBLIC croft_thatch_core)

add_library(croft_wit_world_inline_command STATIC ${WORLD_INLINE_COMMAND_WIT_GENERATED_C})
add_dependencies(croft_wit_world_inline_command world_inline_command_wit_schema_gen)
croft_apply_public_includes(croft_wit_world_inline_command)
target_link_libraries(croft_wit_world_inline_command PUBLIC croft_thatch_core)

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

add_library(croft_msg_frame STATIC src/host/host_messaging.c)
add_dependencies(croft_msg_frame wit_schema_gen)
croft_apply_public_includes(croft_msg_frame)
target_link_libraries(croft_msg_frame PUBLIC croft_foundation croft_wire_runtime)
croft_register_artifact(croft_msg_frame messaging
    TAGS tier1 messaging wire envelope dbi-schema
    DEPENDS croft_foundation croft_wire_runtime
)

add_library(croft_host_queue STATIC src/host/host_queue.c)
add_dependencies(croft_host_queue wit_schema_gen)
croft_apply_public_includes(croft_host_queue)
target_link_libraries(croft_host_queue PUBLIC croft_foundation croft_host_thread croft_msg_frame)
croft_register_artifact(croft_host_queue host
    TAGS tier1 host queue messaging
    DEPENDS croft_foundation croft_host_thread croft_msg_frame
)

add_library(croft_messaging STATIC src/runtime/croft_messaging.c)
add_dependencies(croft_messaging wit_schema_gen)
croft_apply_public_includes(croft_messaging)
target_link_libraries(croft_messaging PUBLIC croft_msg_frame croft_host_queue)
croft_register_artifact(croft_messaging messaging
    TAGS tier1 messaging queue dbi-schema
    DEPENDS croft_msg_frame croft_host_queue
)

add_library(croft_fs STATIC src/host/host_fs.c)
croft_apply_public_includes(croft_fs)
target_link_libraries(croft_fs PUBLIC croft_foundation)
croft_register_artifact(croft_fs filesystem
    TAGS tier2 filesystem io
    DEPENDS croft_foundation
)

add_library(croft STATIC src/runtime/croft_aggregate.c)
add_dependencies(croft wit_schema_gen)
croft_apply_public_includes(croft)
target_link_libraries(croft
    PUBLIC
        croft_foundation
        croft_host_log
        croft_host_time
        croft_host_thread
        croft_msg_frame
        croft_host_queue
        croft_fs
        croft_wire_runtime
)
croft_register_artifact(croft aggregate
    TAGS tier0 tier1 tier2 foundation host messaging filesystem compatibility
    DEPENDS
        croft_foundation
        croft_host_log
        croft_host_time
        croft_host_thread
        croft_msg_frame
        croft_host_queue
        croft_fs
        croft_wire_runtime
)

# ── Library: Sapling ────────────────────────────────────────────────────
file(GLOB SAPLING_CORE_SOURCES CONFIGURE_DEPENDS "src/sapling/*.c")
file(GLOB SAPLING_RUNNER_CORE_SOURCES CONFIGURE_DEPENDS "src/runner/*.c")
list(REMOVE_ITEM SAPLING_RUNNER_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/runner/runner_v0.c")

function(croft_apply_sapling_profile target)
    cmake_parse_arguments(SAPLING_PROFILE "THREADED" "" "" ${ARGN})
    croft_mark_sapling_profile(${target} ${ARGN})
    target_compile_definitions(${target} PUBLIC SAPLING_PAGE_SIZE=4096)
    if(SAPLING_PROFILE_THREADED)
        target_compile_definitions(${target} PUBLIC SAPLING_THREADED)
        target_link_libraries(${target} PUBLIC Threads::Threads)
    endif()
endfunction()

add_library(sapling_core STATIC ${SAPLING_CORE_SOURCES})
croft_apply_public_includes(sapling_core)
croft_apply_sapling_profile(sapling_core)
croft_link_libraries_checked(sapling_core PUBLIC croft_thatch_core)
if(CROFT_BUILD_TESTS)
    target_compile_definitions(sapling_core PRIVATE SAPLING_HAMT_TESTING)
endif()
croft_register_artifact(sapling_core datastore
    TAGS tier3 tier4 sapling core single-thread linear-memory
    DEPENDS croft_thatch_core
)

add_library(sapling_core_threaded STATIC ${SAPLING_CORE_SOURCES})
croft_apply_public_includes(sapling_core_threaded)
croft_apply_sapling_profile(sapling_core_threaded THREADED)
croft_link_libraries_checked(sapling_core_threaded PUBLIC croft_thatch_core)
if(CROFT_BUILD_TESTS)
    target_compile_definitions(sapling_core_threaded PRIVATE SAPLING_HAMT_TESTING)
endif()
croft_register_artifact(sapling_core_threaded datastore
    TAGS tier3 tier4 sapling core threaded
    DEPENDS croft_thatch_core
)

add_library(sapling_runner_core STATIC ${SAPLING_RUNNER_CORE_SOURCES})
add_dependencies(sapling_runner_core wit_schema_gen)
croft_apply_public_includes(sapling_runner_core)
croft_apply_sapling_profile(sapling_runner_core)
croft_link_libraries_checked(sapling_runner_core PUBLIC sapling_core croft_wire_runtime)
croft_register_artifact(sapling_runner_core runner
    TAGS tier4 sapling runner common protocol wire intent replay
    DEPENDS sapling_core croft_wire_runtime
)

add_library(sapling_runner_core_threaded STATIC ${SAPLING_RUNNER_CORE_SOURCES})
add_dependencies(sapling_runner_core_threaded wit_schema_gen)
croft_apply_public_includes(sapling_runner_core_threaded)
croft_apply_sapling_profile(sapling_runner_core_threaded THREADED)
croft_link_libraries_checked(sapling_runner_core_threaded PUBLIC sapling_core_threaded croft_wire_runtime)
croft_register_artifact(sapling_runner_core_threaded runner
    TAGS tier4 sapling runner common protocol wire intent replay threaded
    DEPENDS sapling_core_threaded croft_wire_runtime
)

add_library(sapling_wasi_runtime STATIC src/wasi/runtime_v0.c)
croft_apply_public_includes(sapling_wasi_runtime)
croft_apply_sapling_profile(sapling_wasi_runtime)
croft_link_libraries_checked(sapling_wasi_runtime PUBLIC sapling_runner_core)
croft_register_artifact(sapling_wasi_runtime wasi
    TAGS tier4 wasi runtime adapter common
    DEPENDS sapling_runner_core
)

add_library(sapling_wasi_runtime_threaded STATIC src/wasi/runtime_v0.c)
croft_apply_public_includes(sapling_wasi_runtime_threaded)
croft_apply_sapling_profile(sapling_wasi_runtime_threaded THREADED)
croft_link_libraries_checked(sapling_wasi_runtime_threaded PUBLIC sapling_runner_core_threaded)
croft_register_artifact(sapling_wasi_runtime_threaded wasi
    TAGS tier4 wasi runtime adapter common threaded
    DEPENDS sapling_runner_core_threaded
)

add_library(sapling_runner_host STATIC src/runner/runner_v0.c)
add_dependencies(sapling_runner_host wit_schema_gen)
croft_apply_public_includes(sapling_runner_host)
croft_apply_sapling_profile(sapling_runner_host THREADED)
croft_link_libraries_checked(sapling_runner_host PUBLIC sapling_runner_core_threaded)
croft_register_artifact(sapling_runner_host runner
    TAGS tier4 sapling runner host threaded clocks sleep polling
    DEPENDS sapling_runner_core_threaded
)

add_library(sapling_wasi_host STATIC src/wasi/shim_v0.c)
add_dependencies(sapling_wasi_host wit_schema_gen)
croft_apply_public_includes(sapling_wasi_host)
croft_apply_sapling_profile(sapling_wasi_host THREADED)
croft_link_libraries_checked(sapling_wasi_host PUBLIC sapling_runner_host sapling_wasi_runtime_threaded)
croft_register_artifact(sapling_wasi_host wasi
    TAGS tier4 wasi shim host reply-routing
    DEPENDS sapling_runner_host sapling_wasi_runtime_threaded
)

add_library(sapling STATIC src/runtime/sapling_aggregate.c)
croft_apply_public_includes(sapling)
croft_mark_sapling_profile(sapling THREADED)
croft_link_libraries_checked(sapling PUBLIC
    sapling_core_threaded
    sapling_runner_core_threaded
    sapling_wasi_runtime_threaded
    sapling_runner_host
    sapling_wasi_host
)
if(CROFT_BUILD_TESTS)
    target_compile_definitions(sapling PRIVATE SAPLING_HAMT_TESTING)
endif()
croft_register_artifact(sapling compatibility
    TAGS tier3 tier4 sapling threaded runner wasi datastore persistent-text aggregate
    DEPENDS sapling_core_threaded sapling_runner_core_threaded sapling_wasi_runtime_threaded sapling_runner_host sapling_wasi_host
)

add_library(croft_editor_core STATIC
    src/editor/editor_text_model.c
    src/editor/editor_syntax.c
    src/editor/editor_brackets.c
    src/editor/editor_folding.c
    src/editor/editor_whitespace.c
    src/editor/editor_search.c
    src/editor/editor_status.c
)
croft_apply_public_includes(croft_editor_core)
croft_register_artifact(croft_editor_core editor
    TAGS tier8 editor text-model selection position range line-index
)

add_library(croft_editor_commands STATIC src/editor/editor_commands.c)
croft_apply_public_includes(croft_editor_commands)
target_link_libraries(croft_editor_commands PUBLIC croft_editor_core)
croft_register_artifact(croft_editor_commands editor
    TAGS tier8 editor commands cursor movement word-ops
    DEPENDS croft_editor_core
)

add_library(croft_editor_scene_runtime STATIC src/editor/editor_scene_runtime.c)
croft_apply_public_includes(croft_editor_scene_runtime)
croft_register_artifact(croft_editor_scene_runtime editor
    TAGS tier8 editor scene runtime loop invalidation
)

add_library(croft_editor_document_core STATIC src/editor/editor_document.c)
croft_apply_public_includes(croft_editor_document_core)
croft_set_target_profile(croft_editor_document_core single-thread)
croft_link_libraries_checked(croft_editor_document_core PUBLIC croft_editor_core sapling_core)
croft_register_artifact(croft_editor_document_core editor
    TAGS tier8 editor document sapling common host-neutral
    DEPENDS croft_editor_core sapling_core
)

add_library(croft_editor_document_fs STATIC src/editor/editor_document_fs.c)
croft_apply_public_includes(croft_editor_document_fs)
croft_set_target_profile(croft_editor_document_fs single-thread)
croft_link_libraries_checked(croft_editor_document_fs PUBLIC croft_editor_document_core croft_fs)
croft_register_artifact(croft_editor_document_fs editor
    TAGS tier8 editor document filesystem io
    DEPENDS croft_editor_document_core croft_fs
)

add_library(croft_editor_document STATIC src/editor/editor_document_aggregate.c)
croft_apply_public_includes(croft_editor_document)
croft_set_target_profile(croft_editor_document single-thread)
croft_link_libraries_checked(croft_editor_document PUBLIC croft_editor_document_core croft_editor_document_fs)
croft_register_artifact(croft_editor_document editor
    TAGS tier8 editor document compatibility aggregate
    DEPENDS croft_editor_document_core croft_editor_document_fs
)

add_library(croft_wit_world_runtime STATIC src/runtime/wit_world_runtime.c)
croft_apply_public_includes(croft_wit_world_runtime)
croft_register_artifact(croft_wit_world_runtime wit
    TAGS tier8 wit world runtime metadata generated-glue
)

add_library(croft_wit_wasi_machine_runtime STATIC src/runtime/wit_wasi_machine_runtime.c)
add_dependencies(croft_wit_wasi_machine_runtime
    wasi_cli_command_wit_schema_gen
    wasi_random_world_wit_schema_gen
    wasi_io_world_wit_schema_gen
    wasi_clocks_world_wit_schema_gen
    wasi_filesystem_world_wit_schema_gen)
croft_apply_public_includes(croft_wit_wasi_machine_runtime)
croft_set_target_profile(croft_wit_wasi_machine_runtime single-thread)
target_link_libraries(croft_wit_wasi_machine_runtime PUBLIC
    croft_wit_world_runtime
    croft_wit_wasi_cli_command
    croft_wit_wasi_random_world
    croft_wit_wasi_io_world
    croft_wit_wasi_clocks_world
    croft_wit_wasi_filesystem_world)
croft_register_artifact(croft_wit_wasi_machine_runtime wit
    UPSTREAM_PACKAGE "wasi"
    UPSTREAM_VERSION "0.2.9"
    SUPPORT_STATUS "host-implemented-subset"
    APPLICABILITY "current-machine-unix"
    CAPABILITY_BUNDLES
        wasi-cli-stdio-terminal-current-machine
        wasi-random-current-machine
        wasi-clocks-poll-current-machine
        wasi-filesystem-streams-current-machine
    SHARED_SUBSTRATES
        wasi-process-context
        wasi-byte-streams
        wasi-descriptor-table
        wasi-pollables
        wasi-system-random
        wasi-time-base
    HELPER_INTERFACES
        "wasi:io@0.2.9/error"
        "wasi:filesystem@0.2.9/error"
    DECLARED_WORLDS
        "wasi:cli@0.2.9/imports"
        "wasi:cli@0.2.9/command"
        "wasi:random@0.2.9/world"
        "wasi:clocks@0.2.9/world"
        "wasi:io@0.2.9/world"
        "wasi:filesystem@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:cli@0.2.9/environment"
        "wasi:cli@0.2.9/stdio"
        "wasi:cli@0.2.9/terminal"
        "wasi:cli@0.2.9/run"
        "wasi:random@0.2.9/random"
        "wasi:random@0.2.9/insecure"
        "wasi:random@0.2.9/insecure-seed"
        "wasi:clocks@0.2.9/monotonic-clock"
        "wasi:clocks@0.2.9/wall-clock"
        "wasi:clocks@0.2.9/timezone"
        "wasi:io@0.2.9/poll"
        "wasi:io@0.2.9/streams"
        "wasi:filesystem@0.2.9/types"
        "wasi:filesystem@0.2.9/preopens"
    TAGS tier8 wit wasi runtime current-machine unix generated-glue host
    DEPENDS croft_wit_world_runtime
            croft_wit_wasi_cli_command
            croft_wit_wasi_random_world
            croft_wit_wasi_io_world
            croft_wit_wasi_clocks_world
            croft_wit_wasi_filesystem_world
)

croft_register_xpi_substrate(wasi-process-context
    KIND "process-context"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Process argv/env/cwd and run-status substrate shared by current-machine CLI surfaces."
    ARTIFACTS
        croft_wit_wasi_cli_environment
        croft_wit_wasi_cli_run
        croft_wit_wasi_cli_imports
        croft_wit_wasi_cli_command
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:cli@0.2.9/environment"
        "wasi:cli@0.2.9/run"
)
croft_register_xpi_substrate(wasi-byte-streams
    KIND "byte-streams"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Shared byte-stream substrate backing stdio, filesystem stream handles, and io/streams."
    ARTIFACTS
        croft_wit_wasi_cli_stdio
        croft_wit_wasi_io_streams
        croft_wit_wasi_io_world
        croft_wit_wasi_filesystem_types
        croft_wit_wasi_filesystem_world
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:cli@0.2.9/stdio"
        "wasi:io@0.2.9/streams"
        "wasi:filesystem@0.2.9/types"
)
croft_register_xpi_substrate(wasi-descriptor-table
    KIND "descriptor-table"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Shared descriptor/resource handle table backing stdio, terminal authority, filesystem descriptors, and streams."
    ARTIFACTS
        croft_wit_wasi_cli_stdio
        croft_wit_wasi_cli_terminal
        croft_wit_wasi_cli_imports
        croft_wit_wasi_cli_command
        croft_wit_wasi_io_streams
        croft_wit_wasi_io_world
        croft_wit_wasi_filesystem_types
        croft_wit_wasi_filesystem_preopens
        croft_wit_wasi_filesystem_world
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:cli@0.2.9/stdio"
        "wasi:cli@0.2.9/terminal"
        "wasi:io@0.2.9/streams"
        "wasi:filesystem@0.2.9/types"
        "wasi:filesystem@0.2.9/preopens"
)
croft_register_xpi_substrate(wasi-pollables
    KIND "pollable-set"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Shared pollable handle set used by monotonic clock subscriptions and io/poll readiness."
    ARTIFACTS
        croft_wit_wasi_io_poll
        croft_wit_wasi_io_world
        croft_wit_wasi_clocks_monotonic_clock
        croft_wit_wasi_clocks_world
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:io@0.2.9/poll"
        "wasi:clocks@0.2.9/monotonic-clock"
)
croft_register_xpi_substrate(wasi-system-random
    KIND "entropy-source"
    APPLICABILITY "current-machine"
    DESCRIPTION "Current-machine system entropy source shared by secure and insecure WASI random surfaces."
    ARTIFACTS
        croft_wit_wasi_random_random
        croft_wit_wasi_random_insecure
        croft_wit_wasi_random_insecure_seed
        croft_wit_wasi_random_world
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:random@0.2.9/random"
        "wasi:random@0.2.9/insecure"
        "wasi:random@0.2.9/insecure-seed"
)
croft_register_xpi_substrate(wasi-time-base
    KIND "clock-source"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Host time and timezone substrate shared by wall-clock, monotonic-clock, and timezone surfaces."
    ARTIFACTS
        croft_wit_wasi_clocks_monotonic_clock
        croft_wit_wasi_clocks_wall_clock
        croft_wit_wasi_clocks_timezone
        croft_wit_wasi_clocks_world
        croft_wit_wasi_machine_runtime
    INTERFACES
        "wasi:clocks@0.2.9/monotonic-clock"
        "wasi:clocks@0.2.9/wall-clock"
        "wasi:clocks@0.2.9/timezone"
)

croft_register_xpi_bundle(wasi-cli-stdio-terminal-current-machine
    SUPPORT_STATUS "host-implemented-subset"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "CLI imports/command bundle over shared process-context, byte-stream, and descriptor substrates."
    ARTIFACTS
        croft_wit_wasi_cli_environment
        croft_wit_wasi_cli_stdio
        croft_wit_wasi_cli_terminal
        croft_wit_wasi_cli_run
        croft_wit_wasi_cli_imports
        croft_wit_wasi_cli_command
        croft_wit_wasi_machine_runtime
    SUBSTRATES
        wasi-process-context
        wasi-byte-streams
        wasi-descriptor-table
    DECLARED_WORLDS
        "wasi:cli@0.2.9/imports"
        "wasi:cli@0.2.9/command"
    EXPANDED_SURFACES
        "wasi:cli@0.2.9/environment"
        "wasi:cli@0.2.9/stdio"
        "wasi:cli@0.2.9/terminal"
        "wasi:cli@0.2.9/run"
)
croft_register_xpi_bundle(wasi-random-current-machine
    SUPPORT_STATUS "host-implemented-subset"
    APPLICABILITY "current-machine"
    DESCRIPTION "Current-machine random bundle sharing one system entropy substrate."
    ARTIFACTS
        croft_wit_wasi_random_random
        croft_wit_wasi_random_insecure
        croft_wit_wasi_random_insecure_seed
        croft_wit_wasi_random_world
        croft_wit_wasi_machine_runtime
    SUBSTRATES wasi-system-random
    DECLARED_WORLDS "wasi:random@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:random@0.2.9/random"
        "wasi:random@0.2.9/insecure"
        "wasi:random@0.2.9/insecure-seed"
)
croft_register_xpi_bundle(wasi-clocks-poll-current-machine
    SUPPORT_STATUS "host-implemented-subset"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Current-machine clocks/poll bundle sharing time-base and pollable substrates."
    ARTIFACTS
        croft_wit_wasi_clocks_timezone
        croft_wit_wasi_clocks_wall_clock
        croft_wit_wasi_clocks_monotonic_clock
        croft_wit_wasi_clocks_world
        croft_wit_wasi_io_poll
        croft_wit_wasi_io_world
        croft_wit_wasi_machine_runtime
    SUBSTRATES
        wasi-time-base
        wasi-pollables
    DECLARED_WORLDS
        "wasi:clocks@0.2.9/world"
        "wasi:io@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:clocks@0.2.9/monotonic-clock"
        "wasi:clocks@0.2.9/wall-clock"
        "wasi:clocks@0.2.9/timezone"
        "wasi:io@0.2.9/poll"
)
croft_register_xpi_bundle(wasi-filesystem-streams-current-machine
    SUPPORT_STATUS "host-implemented-subset"
    APPLICABILITY "current-machine-unix"
    DESCRIPTION "Current-machine filesystem/streams bundle sharing descriptor and byte-stream substrates plus helper errors."
    ARTIFACTS
        croft_wit_wasi_filesystem_types
        croft_wit_wasi_filesystem_preopens
        croft_wit_wasi_filesystem_world
        croft_wit_wasi_io_streams
        croft_wit_wasi_io_world
        croft_wit_wasi_machine_runtime
    SUBSTRATES
        wasi-byte-streams
        wasi-descriptor-table
    DECLARED_WORLDS
        "wasi:filesystem@0.2.9/world"
        "wasi:io@0.2.9/world"
    EXPANDED_SURFACES
        "wasi:filesystem@0.2.9/types"
        "wasi:filesystem@0.2.9/preopens"
        "wasi:io@0.2.9/streams"
    HELPER_INTERFACES
        "wasi:io@0.2.9/error"
        "wasi:filesystem@0.2.9/error"
)

add_library(croft_wit_text_runtime STATIC src/runtime/wit_text_runtime.c)
add_dependencies(croft_wit_text_runtime common_wit_schema_gen)
croft_apply_public_includes(croft_wit_text_runtime)
croft_set_target_profile(croft_wit_text_runtime single-thread)
croft_link_libraries_checked(croft_wit_text_runtime PUBLIC sapling_core croft_wit_common_core)
croft_register_artifact(croft_wit_text_runtime wit
    TAGS tier8 wit text resource-handles common-core machine-generated-model
    DEPENDS sapling_core croft_wit_common_core
)

add_library(croft_wit_text_program STATIC src/runtime/wit_text_program.c)
add_dependencies(croft_wit_text_program common_wit_schema_gen)
croft_apply_public_includes(croft_wit_text_program)
target_link_libraries(croft_wit_text_program PUBLIC croft_wit_common_core)
croft_register_artifact(croft_wit_text_program wit
    TAGS tier8 wit text common-side shared-logic machine-generated-model
    DEPENDS croft_wit_common_core
)

add_library(croft_wit_store_runtime STATIC src/runtime/wit_store_runtime.c)
add_dependencies(croft_wit_store_runtime common_wit_schema_gen)
croft_apply_public_includes(croft_wit_store_runtime)
croft_set_target_profile(croft_wit_store_runtime single-thread)
croft_link_libraries_checked(croft_wit_store_runtime PUBLIC sapling_core croft_wit_common_core)
croft_register_artifact(croft_wit_store_runtime wit
    TAGS tier8 wit db txn resource-handles common-core machine-generated-model
    DEPENDS sapling_core croft_wit_common_core
)

add_library(croft_wit_mailbox_runtime STATIC src/runtime/wit_mailbox_runtime.c)
add_dependencies(croft_wit_mailbox_runtime common_wit_schema_gen)
croft_apply_public_includes(croft_wit_mailbox_runtime)
target_link_libraries(croft_wit_mailbox_runtime PUBLIC croft_wit_common_core)
croft_register_artifact(croft_wit_mailbox_runtime wit
    TAGS tier8 wit mailbox resource-handles common-core machine-generated-model no-shared-memory
    DEPENDS croft_wit_common_core
)

add_library(croft_wit_host_fs_runtime STATIC src/runtime/wit_host_fs_runtime.c)
add_dependencies(croft_wit_host_fs_runtime host_fs_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_fs_runtime)
target_link_libraries(croft_wit_host_fs_runtime PUBLIC croft_fs croft_wit_host_fs)
croft_register_artifact(croft_wit_host_fs_runtime wit
    TAGS tier8 wit host-fs file-handles host-mix-in machine-generated-model
    DEPENDS croft_fs croft_wit_host_fs
)

add_library(croft_wit_host_clock_runtime STATIC src/runtime/wit_host_clock_runtime.c)
add_dependencies(croft_wit_host_clock_runtime host_clock_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_clock_runtime)
target_link_libraries(croft_wit_host_clock_runtime PUBLIC croft_host_time croft_wit_host_clock)
croft_register_artifact(croft_wit_host_clock_runtime wit
    TAGS tier8 wit host-clock host-mix-in service machine-generated-model
    DEPENDS croft_host_time croft_wit_host_clock
)

add_library(croft_wit_host_editor_input_runtime STATIC src/runtime/wit_host_editor_input_runtime.c)
add_dependencies(croft_wit_host_editor_input_runtime host_editor_input_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_editor_input_runtime)
target_link_libraries(croft_wit_host_editor_input_runtime PUBLIC croft_wit_host_editor_input)
croft_register_artifact(croft_wit_host_editor_input_runtime wit
    TAGS tier8 wit host-editor-input host-mix-in service editor machine-generated-model
    DEPENDS croft_wit_host_editor_input
)

# ── Optional Croft artifacts ───────────────────────────────────────────
if(CROFT_WASM3_TARGET)
    add_library(croft_wasm_wasm3 STATIC src/host/host_wasm.c)
    croft_apply_public_includes(croft_wasm_wasm3)
    target_link_libraries(croft_wasm_wasm3
        PRIVATE
            croft_host_log
            croft_host_time
            croft_fs
            croft_wit_world_runtime
            ${CROFT_WASM3_TARGET}
    )
    croft_register_artifact(croft_wasm_wasm3 wasm
        TAGS tier4 wasm wasm3 host
        DEPENDS croft_host_log croft_host_time croft_fs
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
    target_link_libraries(croft_ui_glfw_opengl PUBLIC croft_host_log ${CROFT_GLFW_TARGET})
    if(APPLE)
        target_compile_definitions(croft_ui_glfw_opengl PRIVATE GL_SILENCE_DEPRECATION)
        target_link_libraries(croft_ui_glfw_opengl PUBLIC "-framework OpenGL")
    endif()
    croft_register_artifact(croft_ui_glfw_opengl ui
        TAGS tier5 ui glfw opengl
        DEPENDS croft_host_log
    )

    if(APPLE)
        add_library(croft_ui_glfw_metal STATIC src/host/host_ui.c)
        croft_apply_public_includes(croft_ui_glfw_metal)
        target_compile_definitions(croft_ui_glfw_metal PRIVATE CROFT_HOST_UI_GLFW_NO_API=1)
        target_link_libraries(croft_ui_glfw_metal PUBLIC croft_host_log ${CROFT_GLFW_TARGET})
        croft_register_artifact(croft_ui_glfw_metal ui
            TAGS tier5 ui glfw metal macos
            DEPENDS croft_host_log
        )
    endif()
endif()

set(CROFT_WIT_HOST_WINDOW_UI_TARGET "")
if(TARGET croft_ui_glfw_metal)
    set(CROFT_WIT_HOST_WINDOW_UI_TARGET croft_ui_glfw_metal)
elseif(TARGET croft_ui_glfw_opengl)
    set(CROFT_WIT_HOST_WINDOW_UI_TARGET croft_ui_glfw_opengl)
endif()

if(CROFT_WIT_HOST_WINDOW_UI_TARGET)
    add_library(croft_wit_host_window_runtime STATIC src/runtime/wit_host_window_runtime.c)
    add_dependencies(croft_wit_host_window_runtime host_window_wit_schema_gen)
    croft_apply_public_includes(croft_wit_host_window_runtime)
    target_link_libraries(croft_wit_host_window_runtime PUBLIC
        ${CROFT_WIT_HOST_WINDOW_UI_TARGET}
        croft_wit_host_window
    )
    croft_register_artifact(croft_wit_host_window_runtime wit
        TAGS tier8 wit host-window resource-handles host-mix-in event-queue machine-generated-model
        DEPENDS ${CROFT_WIT_HOST_WINDOW_UI_TARGET} croft_wit_host_window
    )

    add_library(croft_wit_host_clipboard_runtime STATIC src/runtime/wit_host_clipboard_runtime.c)
    add_dependencies(croft_wit_host_clipboard_runtime host_clipboard_wit_schema_gen)
    croft_apply_public_includes(croft_wit_host_clipboard_runtime)
    target_link_libraries(croft_wit_host_clipboard_runtime PUBLIC
        ${CROFT_WIT_HOST_WINDOW_UI_TARGET}
        croft_wit_host_clipboard
    )
    croft_register_artifact(croft_wit_host_clipboard_runtime wit
        TAGS tier8 wit host-clipboard host-mix-in service machine-generated-model
        DEPENDS ${CROFT_WIT_HOST_WINDOW_UI_TARGET} croft_wit_host_clipboard
    )
endif()

if(CROFT_GLFW_TARGET AND CROFT_TGFX_TARGET AND CROFT_TGFX_REQUEST_OPENGL)
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
        src/scene/scene_a11y_bridge.c
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
            croft_editor_document_core
            croft_editor_commands
            sapling_core
    )
    croft_register_artifact(croft_scene_text_editor_tgfx_opengl scene
        TAGS tier7 scene text-editor tgfx opengl sapling
        DEPENDS croft_scene_core_tgfx_opengl sapling_core
    )
endif()

if(APPLE AND CROFT_GLFW_TARGET AND CROFT_TGFX_TARGET AND CROFT_TGFX_REQUEST_METAL
        AND TARGET croft_ui_glfw_metal)
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
        src/scene/scene_a11y_bridge.c
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
            croft_editor_document_core
            croft_editor_commands
            sapling_core
    )
    croft_register_artifact(croft_scene_text_editor_tgfx_metal scene
        TAGS tier7 scene text-editor tgfx metal macos sapling
        DEPENDS croft_scene_core_tgfx_metal sapling_core
    )
endif()

if(APPLE AND TARGET croft_ui_glfw_metal)
    add_library(croft_render_metal_native STATIC src/host/host_render_native_metal.mm)
    set_source_files_properties(src/host/host_render_native_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    croft_apply_public_includes(croft_render_metal_native)
    target_link_libraries(croft_render_metal_native
        PUBLIC
            croft_ui_glfw_metal
            "-framework AppKit"
            "-framework Metal"
            "-framework QuartzCore"
    )
    croft_register_artifact(croft_render_metal_native render
        TAGS tier6 render metal native-direct macos
        DEPENDS croft_ui_glfw_metal
    )

    add_library(croft_wit_host_gpu2d_runtime STATIC src/runtime/wit_host_gpu2d_runtime.c)
    add_dependencies(croft_wit_host_gpu2d_runtime host_gpu2d_wit_schema_gen)
    croft_apply_public_includes(croft_wit_host_gpu2d_runtime)
    target_link_libraries(croft_wit_host_gpu2d_runtime PUBLIC
        croft_render_metal_native
        croft_wit_host_gpu2d
    )
    croft_register_artifact(croft_wit_host_gpu2d_runtime wit
        TAGS tier8 wit host-gpu2d surface render metal native-direct host-mix-in machine-generated-model
        DEPENDS croft_render_metal_native croft_wit_host_gpu2d
    )

    add_library(croft_scene_core_metal_native STATIC
        src/scene/scene_node.c
        src/scene/scene_a11y_bridge.c
        src/scene/node_viewport.c
        src/scene/node_code_block.c
    )
    croft_apply_public_includes(croft_scene_core_metal_native)
    target_link_libraries(croft_scene_core_metal_native PUBLIC croft_render_metal_native)
    croft_register_artifact(croft_scene_core_metal_native scene
        TAGS tier7 scene metal native-direct macos
        DEPENDS croft_render_metal_native
    )

    add_library(croft_scene_text_editor_metal_native STATIC src/scene/node_text_editor.c)
    croft_apply_public_includes(croft_scene_text_editor_metal_native)
    target_link_libraries(croft_scene_text_editor_metal_native
        PUBLIC
            croft_scene_core_metal_native
            croft_editor_document_core
            croft_editor_commands
            sapling_core
    )
    croft_register_artifact(croft_scene_text_editor_metal_native scene
        TAGS tier7 scene text-editor metal native-direct macos sapling
        DEPENDS croft_scene_core_metal_native sapling_core
    )
endif()

if(APPLE)
    add_library(croft_file_dialog_macos STATIC src/host/mac/host_file_dialog.m)
    croft_apply_public_includes(croft_file_dialog_macos)
    set_source_files_properties(src/host/mac/host_file_dialog.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_file_dialog_macos PUBLIC "-framework AppKit")
    croft_register_artifact(croft_file_dialog_macos host
        TAGS macos native file-dialog
    )
else()
    add_library(croft_file_dialog_macos STATIC src/host/host_file_dialog_stub.c)
    croft_apply_public_includes(croft_file_dialog_macos)
    croft_register_artifact(croft_file_dialog_macos host
        TAGS file-dialog stub
    )
endif()

if(APPLE)
    add_library(croft_popup_menu_macos STATIC src/host/mac/host_popup_menu.m)
    croft_apply_public_includes(croft_popup_menu_macos)
    set_source_files_properties(src/host/mac/host_popup_menu.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_popup_menu_macos PUBLIC "-framework AppKit")
    croft_register_artifact(croft_popup_menu_macos host
        TAGS macos native popup-menu
    )
else()
    add_library(croft_popup_menu_macos STATIC src/host/host_popup_menu_stub.c)
    croft_apply_public_includes(croft_popup_menu_macos)
    croft_register_artifact(croft_popup_menu_macos host
        TAGS popup-menu stub
    )
endif()

add_library(croft_wit_host_popup_menu_runtime STATIC src/runtime/wit_host_popup_menu_runtime.c)
add_dependencies(croft_wit_host_popup_menu_runtime host_popup_menu_wit_schema_gen)
croft_apply_public_includes(croft_wit_host_popup_menu_runtime)
target_link_libraries(croft_wit_host_popup_menu_runtime PUBLIC
    croft_popup_menu_macos
    croft_wit_host_popup_menu
)
croft_register_artifact(croft_wit_host_popup_menu_runtime wit
    TAGS tier8 wit host-popup-menu host-mix-in service machine-generated-model
    DEPENDS croft_popup_menu_macos croft_wit_host_popup_menu
)

if(APPLE)
    add_library(croft_editor_appkit STATIC src/host/mac/host_editor_appkit.mm)
    croft_apply_public_includes(croft_editor_appkit)
    set_source_files_properties(src/host/mac/host_editor_appkit.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_editor_appkit
        PUBLIC
            croft_file_dialog_macos
            croft_editor_commands
            croft_editor_document_core
            croft_editor_document_fs
            "-framework AppKit"
    )
    croft_register_artifact(croft_editor_appkit editor
        TAGS tier9 editor native appkit textkit macos cpu
        DEPENDS croft_editor_commands croft_editor_document_core croft_editor_document_fs
    )

    add_library(croft_a11y_macos STATIC src/host/mac/host_a11y.m)
    croft_apply_public_includes(croft_a11y_macos)
    set_source_files_properties(src/host/mac/host_a11y.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(croft_a11y_macos PUBLIC "-framework AppKit")
    croft_register_artifact(croft_a11y_macos accessibility
        TAGS tier9 a11y macos native
    )

    if(CROFT_WIT_HOST_WINDOW_UI_TARGET)
        add_library(croft_wit_host_a11y_runtime STATIC src/runtime/wit_host_a11y_runtime.c)
        add_dependencies(croft_wit_host_a11y_runtime host_a11y_wit_schema_gen)
        croft_apply_public_includes(croft_wit_host_a11y_runtime)
        target_link_libraries(croft_wit_host_a11y_runtime PUBLIC
            ${CROFT_WIT_HOST_WINDOW_UI_TARGET}
            croft_a11y_macos
            croft_wit_host_a11y
        )
        croft_register_artifact(croft_wit_host_a11y_runtime wit
            TAGS tier8 wit host-a11y host-mix-in resource-handles accessibility machine-generated-model
            DEPENDS ${CROFT_WIT_HOST_WINDOW_UI_TARGET} croft_a11y_macos croft_wit_host_a11y
        )
    endif()

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

    add_library(croft_wit_host_menu_runtime STATIC src/runtime/wit_host_menu_runtime.c)
    add_dependencies(croft_wit_host_menu_runtime host_menu_wit_schema_gen menu_wit_schema_gen)
    croft_apply_public_includes(croft_wit_host_menu_runtime)
    target_link_libraries(croft_wit_host_menu_runtime PUBLIC
        croft_menu_macos
        croft_wit_host_menu
    )
    croft_register_artifact(croft_wit_host_menu_runtime wit
        TAGS tier8 wit host-menu host-mix-in service machine-generated-model
        DEPENDS croft_menu_macos croft_wit_host_menu
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

    if(TARGET croft_wit_host_window_runtime AND TARGET croft_gesture_macos_metal AND CROFT_WIT_HOST_WINDOW_UI_TARGET STREQUAL "croft_ui_glfw_metal")
        target_link_libraries(croft_wit_host_window_runtime PUBLIC croft_gesture_macos_metal)
    elseif(TARGET croft_wit_host_window_runtime AND TARGET croft_gesture_macos)
        target_link_libraries(croft_wit_host_window_runtime PUBLIC croft_gesture_macos)
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

# ── Shared Wasm guest tooling for examples/tests ─────────────────────
unset(CROFT_WAT2WASM_TOOL)
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
        unset(CROFT_WAT2WASM_TOOL)
        unset(CROFT_WAT2WASM_TOOL CACHE)
        find_program(CROFT_WAT2WASM_TOOL wat2wasm HINTS ${CMAKE_PROGRAM_PATH})
    endif()

    if(TARGET wat2wasm)
        set(CROFT_WAT2WASM_TOOL wat2wasm)
        set(CROFT_WAT2WASM_DEP wat2wasm)
    elseif(NOT CROFT_WAT2WASM_TOOL AND NOT CROFT_WABT_SOURCE_DIR)
        croft_optional_missing("wabt" "set CROFT_WABT_SOURCE_DIR or install wat2wasm to enable host Wasm examples/tests")
    endif()
endif()

if(TARGET croft_wasm_wasm3 AND CROFT_WAT2WASM_TOOL)
    croft_add_wat_wasm_guest(wasm_guest_gen
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/dummy_guest.wat"
        OUTPUT_NAME "dummy_guest"
        OUT_VAR WASM_GUEST_BIN
    )
    croft_add_wat_wasm_guest(wasm_text_bridge_guest_gen
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/text_bridge_guest.wat"
        OUTPUT_NAME "text_bridge_guest"
        OUT_VAR WASM_TEXT_BRIDGE_BIN
    )
endif()

if(TARGET croft_wasm_wasm3 AND CROFT_LLVM_CLANG_TOOL AND CROFT_WASM_LD_TOOL)
    croft_add_wit_wasm_guest(wasm_wit_world_bridge_guest_gen
        OUTPUT_NAME "wit_world_bridge_guest"
        OUT_VAR WASM_WIT_WORLD_BRIDGE_BIN
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/wit_world_bridge_guest.c"
            "${WASI_CLI_COMMAND_WIT_GENERATED_C}"
            "${WASI_RANDOM_WORLD_WIT_GENERATED_C}"
        DEPENDS
            "${WASI_CLI_COMMAND_WIT_GENERATED_H}"
            "${WASI_RANDOM_WORLD_WIT_GENERATED_H}"
    )
    croft_add_wit_wasm_guest(wasm_wit_world_export_guest_gen
        OUTPUT_NAME "wit_world_export_guest"
        OUT_VAR WASM_WIT_WORLD_EXPORT_GUEST_BIN
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_guests/wit_world_export_guest.c"
            "${WORLD_INLINE_COMMAND_WIT_GENERATED_C}"
        DEPENDS
            "${WORLD_INLINE_COMMAND_WIT_GENERATED_H}"
        INCLUDE_DIRS
            "${CMAKE_CURRENT_BINARY_DIR}/tests/generated"
    )
elseif(TARGET croft_wasm_wasm3)
    croft_optional_missing("wasm-c-guest"
        "set CROFT_LLVM_CLANG_TOOL and CROFT_WASM_LD_TOOL to build the generated WIT C guest bridge")
endif()

# ── Examples ─────────────────────────────────────────────────────────
croft_add_example(example_foundation_threads
    SOURCES examples/foundation_threads.c
    LINKS croft_host_log croft_host_time croft_host_thread
    REQUIRES croft_host_log croft_host_time croft_host_thread
    TAGS foundation threads time log
    FOCUS "Foundation-only threaded work and timing."
)

croft_add_example(example_messaging_roundtrip
    SOURCES examples/messaging_roundtrip.c
    LINKS croft_host_queue
    REQUIRES croft_msg_frame croft_host_queue
    TAGS messaging queue wire roundtrip
    FOCUS "Validated message framing and queue round-trip without Sapling."
)

croft_add_example(example_fs_inspect
    SOURCES examples/fs_inspect.c
    LINKS croft_fs
    REQUIRES croft_fs
    TAGS filesystem io host
    FOCUS "Filesystem access and resource-path discovery."
)

croft_add_example(example_wit_fs_read
    SOURCES examples/wit_fs_read.c
    LINKS croft_wit_host_fs_runtime
    REQUIRES croft_wit_host_fs_runtime
    TAGS wit xpi host-fs file-handles host-mix-in
    FOCUS "Host filesystem access driven through generated WIT file resource handles."
)
add_dependencies(example_wit_fs_read host_fs_wit_schema_gen)
target_compile_definitions(example_wit_fs_read PRIVATE
    CROFT_WIT_FS_SAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wit_host_fs_sample.txt"
)

croft_add_example(example_wit_text_cli
    SOURCES examples/wit_text_cli.c
    LINKS croft_wit_host_fs_runtime croft_wit_text_program croft_wit_text_runtime
    REQUIRES croft_wit_host_fs_runtime croft_wit_text_program croft_wit_text_runtime
    TAGS wit xpi common-core host-fs shared-logic cli
    FOCUS "Shared WIT text logic reused in a CLI-shaped host with optional file input."
)
add_dependencies(example_wit_text_cli common_wit_schema_gen host_fs_wit_schema_gen)

croft_add_example(example_wit_clock_now
    SOURCES examples/wit_clock_now.c
    LINKS croft_wit_host_clock_runtime
    REQUIRES croft_wit_host_clock_runtime
    TAGS wit xpi host-clock host-mix-in
    FOCUS "Host monotonic clock access driven through generated WIT service commands."
)
add_dependencies(example_wit_clock_now host_clock_wit_schema_gen)

if(TARGET croft_wit_host_window_runtime)
    croft_add_example(example_wit_window_events
        SOURCES examples/wit_window_events.c
        LINKS croft_wit_host_window_runtime croft_wit_host_clock_runtime
        REQUIRES croft_wit_host_window_runtime croft_wit_host_clock_runtime
        TAGS wit xpi host-window host-clock host-mix-in event-queue
        FOCUS "Window lifecycle and polled UI events driven through generated WIT commands."
    )
    add_dependencies(example_wit_window_events host_window_wit_schema_gen host_clock_wit_schema_gen)
endif()

if(TARGET croft_wit_host_window_runtime AND TARGET croft_wit_host_gpu2d_runtime)
    croft_add_example(example_wit_gpu_canvas
        SOURCES examples/wit_gpu_canvas.c
        LINKS croft_wit_host_window_runtime croft_wit_host_gpu2d_runtime croft_wit_host_clock_runtime
        REQUIRES croft_wit_host_window_runtime croft_wit_host_gpu2d_runtime croft_wit_host_clock_runtime
        TAGS wit xpi host-window host-gpu2d host-clock render metal native-direct
        FOCUS "Direct-Metal host GPU surface access driven through generated WIT window, clock, and GPU mix-ins."
    )
    add_dependencies(example_wit_gpu_canvas host_window_wit_schema_gen host_gpu2d_wit_schema_gen host_clock_wit_schema_gen)
endif()

if(TARGET croft_wit_host_window_runtime AND TARGET croft_wit_host_gpu2d_runtime AND TARGET croft_wit_text_runtime)
    croft_add_example(example_wit_text_window
        SOURCES examples/wit_text_window.c
        LINKS croft_wit_host_window_runtime
              croft_wit_host_gpu2d_runtime
              croft_wit_host_clock_runtime
              croft_wit_text_program
              croft_wit_text_runtime
        REQUIRES croft_wit_host_window_runtime
                 croft_wit_host_gpu2d_runtime
                 croft_wit_host_clock_runtime
                 croft_wit_text_program
                 croft_wit_text_runtime
        TAGS wit xpi common-core host-window host-gpu2d host-clock shared-logic native-window
        FOCUS "The same common-side WIT text logic rendered through native window and GPU mix-ins."
    )
    add_dependencies(example_wit_text_window common_wit_schema_gen host_window_wit_schema_gen host_gpu2d_wit_schema_gen host_clock_wit_schema_gen)
endif()

if(TARGET croft_wit_host_window_runtime
        AND TARGET croft_wit_host_gpu2d_runtime
        AND TARGET croft_wit_text_runtime
        AND TARGET croft_wit_host_menu_runtime
        AND TARGET croft_wit_host_clipboard_runtime
        AND TARGET croft_wit_host_editor_input_runtime)
    croft_add_example(example_wit_textpad_window
        SOURCES examples/wit_textpad_window.c
        LINKS croft_wit_host_window_runtime
              croft_wit_host_gpu2d_runtime
              croft_wit_host_clock_runtime
              croft_wit_host_menu_runtime
              croft_wit_host_clipboard_runtime
              croft_wit_host_editor_input_runtime
              croft_editor_commands
              croft_wit_text_program
              croft_wit_text_runtime
        REQUIRES croft_wit_host_window_runtime
                 croft_wit_host_gpu2d_runtime
                 croft_wit_host_clock_runtime
                 croft_wit_host_menu_runtime
                 croft_wit_host_clipboard_runtime
                 croft_wit_host_editor_input_runtime
                 croft_editor_core
                 croft_editor_commands
                 croft_wit_text_program
                 croft_wit_text_runtime
        TAGS wit xpi common-core host-window host-gpu2d host-clock host-menu host-clipboard host-editor-input textpad native-window
        FOCUS "A small non-scene textpad that reuses WIT window, menu, clipboard, and editor-input mix-ins over the common text resource."
    )
    add_dependencies(example_wit_textpad_window
        common_wit_schema_gen
        host_window_wit_schema_gen
        host_gpu2d_wit_schema_gen
        host_clock_wit_schema_gen
        host_menu_wit_schema_gen
        host_clipboard_wit_schema_gen
        host_editor_input_wit_schema_gen)
endif()

croft_add_example(example_sapling_text
    SOURCES examples/sapling_text.c
    LINKS sapling_core
    REQUIRES sapling_core
    TAGS sapling text transactions persistent
    FOCUS "Sapling text clone-on-write editing without any GUI."
)

croft_add_example(example_wit_text_handles
    SOURCES examples/wit_text_handles.c
    LINKS croft_wit_text_runtime
    REQUIRES croft_wit_text_runtime
    TAGS wit xpi text handles common-core sapling
    FOCUS "Sapling text editing driven through generated WIT resource handles."
)
add_dependencies(example_wit_text_handles common_wit_schema_gen)

croft_add_example(example_wit_db_kv
    SOURCES examples/wit_db_kv.c
    LINKS croft_wit_store_runtime
    REQUIRES croft_wit_store_runtime
    TAGS wit xpi db txn handles common-core sapling
    FOCUS "Sapling key-value round-trip driven through generated WIT db and txn resource handles."
)
add_dependencies(example_wit_db_kv common_wit_schema_gen)

croft_add_example(example_wit_mailbox_ping
    SOURCES examples/wit_mailbox_ping.c
    LINKS croft_wit_mailbox_runtime
    REQUIRES croft_wit_mailbox_runtime
    TAGS wit xpi mailbox handles common-core no-shared-memory
    FOCUS "Mailbox round-trip driven through generated WIT mailbox resource handles."
)
add_dependencies(example_wit_mailbox_ping common_wit_schema_gen)

if(TARGET wasm_guest_gen)
    croft_add_example(example_wasm_guest
        SOURCES examples/wasm_guest.c
        LINKS croft_wasm_wasm3
        REQUIRES croft_wasm_wasm3
        TAGS wasm wasm3 guest host-bridge
        COMPILE_DEFINITIONS DUMMY_GUEST_WASM_PATH="${WASM_GUEST_BIN}"
        FOCUS "Embedded Wasm guest calling back into Croft host APIs."
    )
    add_dependencies(example_wasm_guest wasm_guest_gen)
endif()

if(TARGET wasm_text_bridge_guest_gen)
    croft_add_example(example_wit_text_wasm_host
        SOURCES examples/wit_text_wasm_host.c
        LINKS croft_wasm_wasm3 croft_wit_text_program croft_wit_text_runtime
        REQUIRES croft_wasm_wasm3 croft_wit_text_program croft_wit_text_runtime
        TAGS wasm wasm3 wit common-core shared-logic host-bridge linear-memory
        COMPILE_DEFINITIONS TEXT_BRIDGE_GUEST_WASM_PATH="${WASM_TEXT_BRIDGE_BIN}"
        FOCUS "The same common-side WIT text logic reused in a Wasm-hosted world over Croft's current wasm3 host."
    )
    add_dependencies(example_wit_text_wasm_host wasm_text_bridge_guest_gen common_wit_schema_gen)
endif()

if(TARGET croft_ui_glfw_opengl)
    croft_add_example(example_ui_window_opengl
        SOURCES examples/ui_window.c
        LINKS croft_ui_glfw_opengl
        REQUIRES croft_ui_glfw_opengl
        TAGS ui window glfw opengl
        FOCUS "Windowing-only sample using a GLFW OpenGL context and no renderer."
    )
endif()

if(TARGET croft_ui_glfw_metal)
    croft_add_example(example_ui_window_metal
        SOURCES examples/ui_window.c
        LINKS croft_ui_glfw_metal
        REQUIRES croft_ui_glfw_metal
        TAGS ui window glfw metal macos
        FOCUS "Windowing-only sample using a GLFW no-API window for the Metal path."
    )
endif()

if(TARGET croft_render_tgfx_opengl)
    croft_add_example(example_render_canvas_opengl
        SOURCES examples/render_canvas_opengl.c
        LINKS croft_render_tgfx_opengl
        REQUIRES croft_render_tgfx_opengl
        TAGS render canvas tgfx opengl
        FOCUS "GPU-backed 2D canvas rendering on the tgfx OpenGL path."
    )
endif()

if(TARGET croft_render_tgfx_metal)
    croft_add_example(example_render_canvas_metal
        SOURCES examples/render_canvas_metal.c
        LINKS croft_render_tgfx_metal
        REQUIRES croft_render_tgfx_metal
        TAGS render canvas tgfx metal macos
        FOCUS "GPU-backed 2D canvas rendering on the tgfx Metal path."
    )
endif()

if(TARGET croft_render_metal_native)
    croft_add_example(example_render_canvas_metal_native
        SOURCES examples/render_canvas_metal_native.c
        LINKS croft_render_metal_native
        REQUIRES croft_render_metal_native
        TAGS render canvas metal native-direct macos
        FOCUS "Direct Metal 2D rectangle rendering with no tgfx dependency."
    )
endif()

if(CROFT_EXAMPLE_SCENE_TARGET)
    croft_add_example(example_scene_graph
        SOURCES tests/host_tests/test_scene_standalone.c
        LINKS ${CROFT_EXAMPLE_SCENE_TARGET} croft_a11y_stub
        REQUIRES ${CROFT_EXAMPLE_SCENE_TARGET}
        TAGS scene graph hit-testing
        FOCUS "Scene graph layout, hit-testing, and draw traversal."
    )
endif()

if(CROFT_EXAMPLE_SCENE_TARGET)
    if(NOT APPLE OR CROFT_EXAMPLE_GESTURE_TARGET)
        set(CROFT_ZOOM_EXAMPLE_LINKS ${CROFT_EXAMPLE_SCENE_TARGET} croft_a11y_stub)
        set(CROFT_ZOOM_EXAMPLE_REQUIRES ${CROFT_EXAMPLE_SCENE_TARGET})
        if(CROFT_EXAMPLE_GESTURE_TARGET)
            list(APPEND CROFT_ZOOM_EXAMPLE_LINKS ${CROFT_EXAMPLE_GESTURE_TARGET})
            list(APPEND CROFT_ZOOM_EXAMPLE_REQUIRES ${CROFT_EXAMPLE_GESTURE_TARGET})
        endif()
        croft_add_example(example_zoom_canvas
            SOURCES tests/host_tests/test_zoom_standalone.c
            LINKS ${CROFT_ZOOM_EXAMPLE_LINKS}
            REQUIRES ${CROFT_ZOOM_EXAMPLE_REQUIRES}
            TAGS scene zoom gesture canvas
            FOCUS "Scrollable and zoomable infinite-canvas scene demo."
        )
    endif()
endif()

if(CROFT_EXAMPLE_EDITOR_TARGET)
    set(CROFT_EDITOR_EXAMPLE_LINKS ${CROFT_EXAMPLE_EDITOR_TARGET} croft_a11y_stub croft_editor_document_fs croft_editor_scene_runtime)
    set(CROFT_EDITOR_EXAMPLE_REQUIRES ${CROFT_EXAMPLE_EDITOR_TARGET} croft_editor_document_core croft_editor_document_fs croft_editor_scene_runtime)
    if(TARGET croft_file_dialog_macos)
        list(APPEND CROFT_EDITOR_EXAMPLE_LINKS croft_file_dialog_macos)
        list(APPEND CROFT_EDITOR_EXAMPLE_REQUIRES croft_file_dialog_macos)
    endif()
    if(TARGET croft_popup_menu_macos)
        list(APPEND CROFT_EDITOR_EXAMPLE_LINKS croft_popup_menu_macos)
        list(APPEND CROFT_EDITOR_EXAMPLE_REQUIRES croft_popup_menu_macos)
    endif()
    croft_add_example(example_editor_text
        SOURCES examples/editor_scene_shell.c
        LINKS ${CROFT_EDITOR_EXAMPLE_LINKS}
        REQUIRES ${CROFT_EDITOR_EXAMPLE_REQUIRES}
        TAGS editor text sapling gui
        FOCUS "Text-editor shell built on Sapling text, scene, and host IO."
    )
endif()

if(TARGET croft_scene_text_editor_metal_native
        AND TARGET croft_wit_host_window_runtime
        AND TARGET croft_wit_host_clock_runtime
        AND TARGET croft_wit_host_menu_runtime
        AND TARGET croft_wit_host_popup_menu_runtime
        AND TARGET croft_wit_host_clipboard_runtime
        AND TARGET croft_wit_host_editor_input_runtime
        AND TARGET croft_wit_host_a11y_runtime
        AND TARGET croft_file_dialog_macos)
    croft_add_example(example_editor_text_metal_native
        SOURCES examples/editor_scene_shell_wit.c
        LINKS croft_scene_text_editor_metal_native
              croft_editor_document_fs
              croft_editor_scene_runtime
              croft_file_dialog_macos
              croft_wit_host_window_runtime
              croft_wit_host_clock_runtime
              croft_wit_host_menu_runtime
              croft_wit_host_popup_menu_runtime
              croft_wit_host_clipboard_runtime
              croft_wit_host_editor_input_runtime
              croft_wit_host_a11y_runtime
        REQUIRES croft_scene_text_editor_metal_native
                 croft_editor_document_core
                 croft_editor_document_fs
                 croft_editor_scene_runtime
                 croft_file_dialog_macos
                 croft_wit_host_window_runtime
                 croft_wit_host_clock_runtime
                 croft_wit_host_menu_runtime
                 croft_wit_host_popup_menu_runtime
                 croft_wit_host_clipboard_runtime
                 croft_wit_host_editor_input_runtime
                 croft_wit_host_a11y_runtime
        TAGS editor text sapling metal native-direct macos wit xpi host-window host-menu host-popup-menu host-clipboard host-editor-input host-a11y
        FOCUS "Text-editor shell built on the direct-Metal renderer while routing window, menu, popup menu, clipboard, input, and accessibility through WIT host mix-ins."
    )
endif()

if(TARGET croft_editor_appkit)
    croft_add_example(example_editor_text_appkit
        SOURCES examples/editor_text_appkit.mm
        LINKS croft_editor_appkit croft_editor_document_fs
        REQUIRES croft_editor_appkit croft_editor_document_core croft_editor_document_fs
        TAGS editor text sapling appkit textkit macos cpu
        FOCUS "CPU-based macOS text editor using AppKit/TextKit over a Sapling-backed document."
    )
endif()

if(TARGET croft_a11y_macos AND CROFT_EXAMPLE_SCENE_TARGET)
    croft_add_example(example_a11y_tree
        SOURCES tests/host_tests/test_a11y_standalone.c
        LINKS ${CROFT_EXAMPLE_SCENE_TARGET} croft_a11y_macos
        REQUIRES ${CROFT_EXAMPLE_SCENE_TARGET} croft_a11y_macos
        TAGS a11y macos scene
        FOCUS "Native accessibility tree wiring for scene nodes on macOS."
    )
endif()

if(TARGET croft_menu_macos AND TARGET croft_ui_glfw_opengl)
    croft_add_example(example_window_menu_opengl
        SOURCES examples/window_menu.c
        LINKS croft_ui_glfw_opengl croft_menu_macos
        REQUIRES croft_ui_glfw_opengl croft_menu_macos
        TAGS menu macos ui opengl
        FOCUS "Window and native menu shell with an OpenGL-capable GLFW context and no renderer."
    )
endif()

if(TARGET croft_menu_macos AND TARGET croft_ui_glfw_metal)
    croft_add_example(example_window_menu_metal
        SOURCES examples/window_menu.c
        LINKS croft_ui_glfw_metal croft_menu_macos
        REQUIRES croft_ui_glfw_metal croft_menu_macos
        TAGS menu macos ui metal
        FOCUS "Window and native menu shell with a Metal-style no-API window and no renderer."
    )
endif()

if(TARGET croft_audio_miniaudio)
    croft_add_example(example_audio_tone
        SOURCES tests/host_tests/test_audio_standalone.c
        LINKS croft_audio_miniaudio
        REQUIRES croft_audio_miniaudio
        TAGS audio miniaudio tone
        FOCUS "Synchronous audio playback using the miniaudio host backend."
    )
endif()

# ── Tests ───────────────────────────────────────────────────────────────
if(CROFT_BUILD_TESTS)
    enable_testing()

    add_executable(test_croft tests/test_main.c tests/test_queue.c tests/host_tests/test_fs.c tests/test_editor_text_model.c tests/test_editor_syntax.c tests/test_editor_brackets.c tests/test_editor_folding.c tests/test_editor_whitespace.c tests/test_editor_search.c tests/test_editor_status.c tests/test_editor_document.c tests/test_editor_commands.c tests/test_editor_scene_runtime.c tests/test_wit_resource_codegen.c tests/test_wit_text_runtime.c tests/test_wit_text_program.c tests/test_wit_store_runtime.c tests/test_wit_mailbox_runtime.c tests/test_wit_host_fs_runtime.c tests/test_wit_host_clock_runtime.c tests/test_wit_host_editor_input_runtime.c tests/test_wit_host_popup_menu_runtime.c)
    target_include_directories(test_croft PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    target_link_libraries(test_croft
        PRIVATE
            croft_host_queue
            croft_editor_document_fs
            croft_editor_commands
            croft_editor_scene_runtime
            croft_wit_text_runtime
            croft_wit_text_program
            croft_wit_store_runtime
            croft_wit_mailbox_runtime
            croft_wit_host_fs_runtime
            croft_wit_host_clock_runtime
            croft_wit_host_editor_input_runtime
            croft_wit_host_popup_menu_runtime
    )
    add_dependencies(test_croft common_wit_schema_gen host_fs_wit_schema_gen host_clock_wit_schema_gen host_editor_input_wit_schema_gen host_popup_menu_wit_schema_gen)
    target_compile_definitions(test_croft PRIVATE
        CROFT_WIT_FS_SAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wit_host_fs_sample.txt"
    )
    if(NOT TARGET croft_wasm_wasm3)
        target_link_libraries(test_croft PRIVATE croft_host_log croft_host_time)
    endif()
    add_test(NAME croft_tests COMMAND test_croft)

    if(TARGET croft_wasm_wasm3 AND TARGET wasm_guest_gen)
        target_sources(test_croft PRIVATE tests/host_tests/test_wasm_guest.c)
        target_link_libraries(test_croft PRIVATE croft_wasm_wasm3)
        target_compile_definitions(test_croft PRIVATE
            CROFT_TEST_HAS_WASM
            DUMMY_GUEST_WASM_PATH="${WASM_GUEST_BIN}"
        )
        add_dependencies(test_croft wasm_guest_gen)
    endif()

    if(TARGET croft_wasm_wasm3 AND TARGET wasm_wit_world_bridge_guest_gen)
        target_sources(test_croft PRIVATE tests/host_tests/test_wasm_wit_guest.c)
        target_link_libraries(test_croft PRIVATE croft_wit_wasi_machine_runtime)
        target_compile_definitions(test_croft PRIVATE
            CROFT_TEST_HAS_WIT_BRIDGE_WASM
            WIT_WORLD_BRIDGE_GUEST_WASM_PATH="${WASM_WIT_WORLD_BRIDGE_BIN}"
        )
        add_dependencies(test_croft wasm_wit_world_bridge_guest_gen)
    endif()

    if(TARGET croft_wasm_wasm3 AND TARGET wasm_wit_world_export_guest_gen)
        target_sources(test_croft PRIVATE tests/host_tests/test_wasm_wit_export_guest.c)
        target_include_directories(test_croft PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/tests/generated>)
        target_link_libraries(test_croft PRIVATE croft_wit_world_inline_command)
        target_compile_definitions(test_croft PRIVATE
            CROFT_TEST_HAS_WIT_EXPORT_WASM
            WIT_WORLD_EXPORT_GUEST_WASM_PATH="${WASM_WIT_WORLD_EXPORT_GUEST_BIN}"
        )
        add_dependencies(test_croft wasm_wit_world_export_guest_gen world_inline_command_wit_schema_gen)
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
    target_link_libraries(test_fuzz_messaging PRIVATE croft_host_queue)

    # Sapling test suite (each file has its own main)
    file(GLOB SAPLING_UNIT_TESTS CONFIGURE_DEPENDS
        "tests/sapling_tests/unit/*.c"
        "tests/sapling_tests/integration/*.c"
    )
    list(REMOVE_ITEM SAPLING_UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sapling_tests/unit/wasm_guest_example.c")
    foreach(test_src IN LISTS SAPLING_UNIT_TESTS)
        get_filename_component(test_name ${test_src} NAME_WE)
        set(test_link_target sapling_core)
        set(test_labels "sapling-core")
        if(test_name STREQUAL "test_sapling")
            set(test_link_target sapling_core_threaded)
            set(test_labels "sapling-core;sapling-threaded")
        elseif(test_name STREQUAL "wasi_runtime_test")
            set(test_link_target sapling_wasi_runtime)
            set(test_labels "sapling-wasi")
        elseif(test_name MATCHES "^runner_" OR test_name MATCHES "^test_runner_")
            set(test_link_target sapling_runner_host)
            set(test_labels "sapling-runner;sapling-threaded")
        elseif(test_name MATCHES "^wasi_" OR test_name STREQUAL "wasm_runner_test")
            set(test_link_target sapling_wasi_host)
            set(test_labels "sapling-wasi;sapling-threaded")
        endif()

        add_executable(${test_name} ${test_src})
        target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        target_link_libraries(${test_name} PRIVATE ${test_link_target})
        if(test_name STREQUAL "test_wit_thatch_codegen")
            target_link_libraries(${test_name} PRIVATE croft_wire_runtime)
        endif()
        if(test_name STREQUAL "wasi_wit_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_wasi_cli_environment
                croft_wit_wasi_random_insecure
            )
        endif()
        if(test_name STREQUAL "wasi_wit_imports_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_wasi_clocks_timezone
                croft_wit_wasi_clocks_monotonic_clock
            )
        endif()
        if(test_name STREQUAL "wasi_wit_worlds_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_world_runtime
                croft_wit_wasi_cli_command
                croft_wit_wasi_io_world
                croft_wit_wasi_random_world
                croft_wit_wasi_clocks_world
                croft_wit_wasi_filesystem_world
            )
        endif()
        if(test_name STREQUAL "test_wit_scoped_names_smoke")
            target_link_libraries(${test_name} PRIVATE croft_wit_scoped_names)
        endif()
        if(test_name STREQUAL "test_wit_operation_lowering_smoke")
            target_link_libraries(${test_name} PRIVATE croft_wit_operation_collisions)
        endif()
        if(test_name STREQUAL "test_wit_use_reexports_smoke")
            target_link_libraries(${test_name} PRIVATE croft_wit_use_reexports_types)
        endif()
        if(test_name STREQUAL "test_wit_world_manifest_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_world_runtime
                croft_wit_world_meta_command
            )
        endif()
        if(test_name STREQUAL "test_wit_inline_world_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_world_runtime
                croft_wit_world_inline_command
            )
        endif()
        if(test_name STREQUAL "test_wit_wasi_machine_runtime_smoke")
            target_link_libraries(${test_name} PRIVATE croft_wit_wasi_machine_runtime)
        endif()
        if(test_name STREQUAL "test_wit_wasi_xpi_smoke")
            target_link_libraries(${test_name} PRIVATE croft_wit_wasi_machine_runtime)
        endif()
        if(test_name STREQUAL "test_wit_croft_wasm_guest_smoke")
            target_link_libraries(${test_name} PRIVATE
                croft_wit_world_runtime
                croft_wit_wasi_cli_command
                croft_wit_wasi_random_world
            )
        endif()
        if(test_name STREQUAL "test_wasi_vendor_report_smoke")
            target_compile_definitions(${test_name} PRIVATE
                CROFT_WASI_VENDOR_REPORT_TXT_PATH="${CMAKE_CURRENT_BINARY_DIR}/reports/croft-wasi-vendor-drift.txt"
                CROFT_WASI_VENDOR_REPORT_JSON_PATH="${CMAKE_CURRENT_BINARY_DIR}/reports/croft-wasi-vendor-drift.json")
        endif()
        target_compile_definitions(${test_name} PRIVATE SAPLING_HAMT_TESTING)
        if(test_name STREQUAL "test_wit_thatch_codegen")
            add_dependencies(${test_name} test_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "wasi_wit_smoke")
            add_dependencies(${test_name}
                wasi_cli_environment_wit_schema_gen
                wasi_cli_stdio_wit_schema_gen
                wasi_cli_terminal_wit_schema_gen
                wasi_random_insecure_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "wasi_wit_imports_smoke")
            add_dependencies(${test_name}
                wasi_io_poll_wit_schema_gen
                wasi_clocks_timezone_wit_schema_gen
                wasi_clocks_monotonic_clock_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "wasi_wit_worlds_smoke")
            add_dependencies(${test_name}
                wasi_cli_command_wit_schema_gen
                wasi_io_world_wit_schema_gen
                wasi_random_world_wit_schema_gen
                wasi_clocks_world_wit_schema_gen
                wasi_filesystem_world_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_wasi_machine_runtime_smoke")
            add_dependencies(${test_name}
                wasi_cli_command_wit_schema_gen
                wasi_random_world_wit_schema_gen
                wasi_io_world_wit_schema_gen
                wasi_clocks_world_wit_schema_gen
                wasi_filesystem_world_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_wasi_xpi_smoke")
            add_dependencies(${test_name}
                wasi_cli_command_wit_schema_gen
                wasi_random_world_wit_schema_gen
                wasi_io_world_wit_schema_gen
                wasi_clocks_world_wit_schema_gen
                wasi_filesystem_world_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
            target_compile_definitions(${test_name} PRIVATE
                CROFT_XPI_JSON_PATH="${CMAKE_CURRENT_BINARY_DIR}/croft-xpi.json"
                CROFT_ARTIFACT_JSON_PATH="${CMAKE_CURRENT_BINARY_DIR}/croft-artifacts.json")
        endif()
        if(test_name STREQUAL "test_wit_croft_wasm_guest_smoke")
            add_dependencies(${test_name}
                wasi_cli_command_wit_schema_gen
                wasi_random_world_wit_schema_gen
            )
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_scoped_names_smoke")
            add_dependencies(${test_name} scoped_names_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_operation_lowering_smoke")
            add_dependencies(${test_name} operation_collisions_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_use_reexports_smoke")
            add_dependencies(${test_name} use_reexports_types_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
        endif()
        if(test_name STREQUAL "test_wit_world_manifest_smoke")
            add_dependencies(${test_name} world_meta_command_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
            target_compile_definitions(${test_name} PRIVATE WIT_WORLD_META_MANIFEST_PATH="${WORLD_META_COMMAND_WIT_GENERATED_MANIFEST}")
        endif()
        if(test_name STREQUAL "test_wit_inline_world_smoke")
            add_dependencies(${test_name} world_inline_command_wit_schema_gen)
            target_include_directories(${test_name} PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
            target_compile_definitions(${test_name} PRIVATE WIT_WORLD_INLINE_MANIFEST_PATH="${WORLD_INLINE_COMMAND_WIT_GENERATED_MANIFEST}")
        endif()
        add_test(NAME sapling_${test_name} COMMAND ${test_name})
        set_tests_properties(sapling_${test_name} PROPERTIES LABELS "${test_labels}")
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

get_property(CROFT_XPI_SUBSTRATE_ENTRIES GLOBAL PROPERTY CROFT_XPI_SUBSTRATE_ENTRIES)
if(NOT CROFT_XPI_SUBSTRATE_ENTRIES)
    set(CROFT_XPI_SUBSTRATE_ENTRIES "")
endif()
string(JOIN ",\n" CROFT_XPI_SUBSTRATE_ENTRIES_JSON ${CROFT_XPI_SUBSTRATE_ENTRIES})

get_property(CROFT_XPI_BUNDLE_ENTRIES GLOBAL PROPERTY CROFT_XPI_BUNDLE_ENTRIES)
if(NOT CROFT_XPI_BUNDLE_ENTRIES)
    set(CROFT_XPI_BUNDLE_ENTRIES "")
endif()
string(JOIN ",\n" CROFT_XPI_BUNDLE_ENTRIES_JSON ${CROFT_XPI_BUNDLE_ENTRIES})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-xpi.json"
    CONTENT "{\n  \"substrates\": [\n${CROFT_XPI_SUBSTRATE_ENTRIES_JSON}\n  ],\n  \"bundles\": [\n${CROFT_XPI_BUNDLE_ENTRIES_JSON}\n  ]\n}\n")

get_property(CROFT_INSTALL_TARGETS GLOBAL PROPERTY CROFT_INSTALL_TARGETS)
if(NOT CROFT_INSTALL_TARGETS)
    set(CROFT_INSTALL_TARGETS "")
endif()
get_property(CROFT_WASM_GUEST_TARGETS GLOBAL PROPERTY CROFT_WASM_GUEST_TARGETS)
if(NOT CROFT_WASM_GUEST_TARGETS)
    set(CROFT_WASM_GUEST_TARGETS "")
endif()
get_property(CROFT_WASM_GUEST_ENTRIES GLOBAL PROPERTY CROFT_WASM_GUEST_ENTRIES)
if(NOT CROFT_WASM_GUEST_ENTRIES)
    set(CROFT_WASM_GUEST_ENTRIES "")
endif()
string(JOIN ",\n" CROFT_WASM_GUEST_ENTRIES_JSON ${CROFT_WASM_GUEST_ENTRIES})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-wasm-guests.json"
    CONTENT "{\n  \"guests\": [\n${CROFT_WASM_GUEST_ENTRIES_JSON}\n  ]\n}\n")
if(CROFT_WASM_GUEST_TARGETS)
    add_custom_target(croft_wasm_guests DEPENDS ${CROFT_WASM_GUEST_TARGETS})
    string(JOIN "\n" CROFT_WASM_GUEST_TARGETS_TEXT ${CROFT_WASM_GUEST_TARGETS})
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-wasm-guest-targets.txt"
        CONTENT "${CROFT_WASM_GUEST_TARGETS_TEXT}\n")
else()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-wasm-guest-targets.txt" CONTENT "")
endif()

set(CROFT_PUBLIC_GENERATED_HEADERS
    ${WIT_GENERATED_H}
    ${COMMON_WIT_GENERATED_H}
    ${HOST_FS_WIT_GENERATED_H}
    ${HOST_CLOCK_WIT_GENERATED_H}
    ${HOST_WINDOW_WIT_GENERATED_H}
    ${HOST_GPU2D_WIT_GENERATED_H}
    ${HOST_MENU_WIT_GENERATED_H}
    ${HOST_POPUP_MENU_WIT_GENERATED_H}
    ${HOST_CLIPBOARD_WIT_GENERATED_H}
    ${HOST_EDITOR_INPUT_WIT_GENERATED_H}
    ${HOST_A11Y_WIT_GENERATED_H}
    ${WASI_CLI_ENVIRONMENT_WIT_GENERATED_H}
    ${WASI_CLI_STDIO_WIT_GENERATED_H}
    ${WASI_CLI_TERMINAL_WIT_GENERATED_H}
    ${WASI_CLI_RUN_WIT_GENERATED_H}
    ${WASI_CLI_IMPORTS_WIT_GENERATED_H}
    ${WASI_CLI_COMMAND_WIT_GENERATED_H}
    ${WASI_RANDOM_RANDOM_WIT_GENERATED_H}
    ${WASI_RANDOM_INSECURE_WIT_GENERATED_H}
    ${WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_H}
    ${WASI_RANDOM_WORLD_WIT_GENERATED_H}
    ${WASI_CLOCKS_TIMEZONE_WIT_GENERATED_H}
    ${WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_H}
    ${WASI_IO_POLL_WIT_GENERATED_H}
    ${WASI_IO_STREAMS_WIT_GENERATED_H}
    ${WASI_IO_WORLD_WIT_GENERATED_H}
    ${WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_H}
    ${WASI_CLOCKS_WORLD_WIT_GENERATED_H}
)
set(CROFT_PUBLIC_GENERATED_MANIFESTS
    ${WIT_GENERATED_MANIFEST}
    ${COMMON_WIT_GENERATED_MANIFEST}
    ${HOST_FS_WIT_GENERATED_MANIFEST}
    ${HOST_CLOCK_WIT_GENERATED_MANIFEST}
    ${HOST_WINDOW_WIT_GENERATED_MANIFEST}
    ${HOST_GPU2D_WIT_GENERATED_MANIFEST}
    ${HOST_MENU_WIT_GENERATED_MANIFEST}
    ${HOST_POPUP_MENU_WIT_GENERATED_MANIFEST}
    ${HOST_CLIPBOARD_WIT_GENERATED_MANIFEST}
    ${HOST_EDITOR_INPUT_WIT_GENERATED_MANIFEST}
    ${HOST_A11Y_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_ENVIRONMENT_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_STDIO_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_TERMINAL_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_RUN_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_IMPORTS_WIT_GENERATED_MANIFEST}
    ${WASI_CLI_COMMAND_WIT_GENERATED_MANIFEST}
    ${WASI_RANDOM_RANDOM_WIT_GENERATED_MANIFEST}
    ${WASI_RANDOM_INSECURE_WIT_GENERATED_MANIFEST}
    ${WASI_RANDOM_INSECURE_SEED_WIT_GENERATED_MANIFEST}
    ${WASI_RANDOM_WORLD_WIT_GENERATED_MANIFEST}
    ${WASI_CLOCKS_TIMEZONE_WIT_GENERATED_MANIFEST}
    ${WASI_CLOCKS_WALL_CLOCK_WIT_GENERATED_MANIFEST}
    ${WASI_IO_POLL_WIT_GENERATED_MANIFEST}
    ${WASI_IO_STREAMS_WIT_GENERATED_MANIFEST}
    ${WASI_IO_WORLD_WIT_GENERATED_MANIFEST}
    ${WASI_CLOCKS_MONOTONIC_CLOCK_WIT_GENERATED_MANIFEST}
    ${WASI_CLOCKS_WORLD_WIT_GENERATED_MANIFEST}
)
if(TARGET croft_menu_macos)
    list(APPEND CROFT_PUBLIC_GENERATED_HEADERS ${MENU_WIT_GENERATED_H})
    list(APPEND CROFT_PUBLIC_GENERATED_MANIFESTS ${MENU_WIT_GENERATED_MANIFEST})
endif()

set(CROFT_MACHINE_TARGET_REPORT_TEXT "# Croft current-machine artifact targets\n")
set(CROFT_MACHINE_TARGET_REPORT_TSV "target\n")
set(CROFT_SYSTEM_LINK_REPORT_TEXT "# Croft current-machine system link dependencies\n")
set(CROFT_SYSTEM_LINK_REPORT_TSV "target\tdependency\n")
foreach(target IN LISTS CROFT_INSTALL_TARGETS)
    if(NOT TARGET ${target})
        continue()
    endif()
    string(APPEND CROFT_MACHINE_TARGET_REPORT_TEXT "${target}\n")
    string(APPEND CROFT_MACHINE_TARGET_REPORT_TSV "${target}\n")

    set(target_system_deps "")
    foreach(prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(prop_value ${target} ${prop})
        if(NOT prop_value OR prop_value MATCHES "-NOTFOUND$")
            continue()
        endif()
        foreach(dep IN LISTS prop_value)
            if(dep STREQUAL "" OR dep MATCHES "^\\$<")
                continue()
            endif()
            if(TARGET ${dep} AND dep MATCHES "^(croft|sapling)_")
                continue()
            endif()
            list(APPEND target_system_deps "${dep}")
        endforeach()
    endforeach()
    if(target_system_deps)
        list(REMOVE_DUPLICATES target_system_deps)
        string(JOIN ", " target_system_deps_text ${target_system_deps})
        string(APPEND CROFT_SYSTEM_LINK_REPORT_TEXT "${target}: ${target_system_deps_text}\n")
        foreach(dep IN LISTS target_system_deps)
            string(APPEND CROFT_SYSTEM_LINK_REPORT_TSV "${target}\t${dep}\n")
        endforeach()
    endif()
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reports")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.txt"
    CONTENT "${CROFT_MACHINE_TARGET_REPORT_TEXT}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.tsv"
    CONTENT "${CROFT_MACHINE_TARGET_REPORT_TSV}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.txt"
    CONTENT "${CROFT_SYSTEM_LINK_REPORT_TEXT}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.tsv"
    CONTENT "${CROFT_SYSTEM_LINK_REPORT_TSV}")

file(GLOB_RECURSE CROFT_DEP_AUDIT_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.c"
)
list(APPEND CROFT_DEP_AUDIT_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dependency_report.sh")
file(GLOB_RECURSE CROFT_WASI_VENDOR_REPORT_INPUTS CONFIGURE_DEPENDS
    "${CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR}/*"
    "${CROFT_WASI_CURRENT_MACHINE_0_2_9_DIR}/*")
list(APPEND CROFT_WASI_VENDOR_REPORT_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_wasi_vendor_report.sh")
set(CROFT_LIBC_DEPENDENCY_REPORT_TXT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-libc-posix-usage.txt")
set(CROFT_LIBC_DEPENDENCY_REPORT_TSV "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-libc-posix-usage.tsv")
set(CROFT_WASI_VENDOR_REPORT_TXT "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-wasi-vendor-drift.txt")
set(CROFT_WASI_VENDOR_REPORT_JSON "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-wasi-vendor-drift.json")
add_custom_command(
    OUTPUT ${CROFT_LIBC_DEPENDENCY_REPORT_TXT} ${CROFT_LIBC_DEPENDENCY_REPORT_TSV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/reports"
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dependency_report.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CROFT_LIBC_DEPENDENCY_REPORT_TXT}"
            "${CROFT_LIBC_DEPENDENCY_REPORT_TSV}"
    DEPENDS ${CROFT_DEP_AUDIT_INPUTS}
    COMMENT "Generating Croft libc/POSIX dependency audit..."
)
add_custom_command(
    OUTPUT ${CROFT_WASI_VENDOR_REPORT_TXT} ${CROFT_WASI_VENDOR_REPORT_JSON}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/reports"
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_wasi_vendor_report.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CROFT_WASI_VENDOR_0_2_9_PROPOSALS_DIR}"
            "${CROFT_WASI_CURRENT_MACHINE_0_2_9_DIR}"
            "${CROFT_WASI_VENDOR_REPORT_TXT}"
            "${CROFT_WASI_VENDOR_REPORT_JSON}"
            "${CROFT_WASI_VENDOR_REPORT_EXTERNAL_ROOT}"
    DEPENDS ${CROFT_WASI_VENDOR_REPORT_INPUTS}
    COMMENT "Generating Croft WASI vendor drift report..."
)
add_custom_target(croft_dependency_report
    DEPENDS
        ${CROFT_LIBC_DEPENDENCY_REPORT_TXT}
        ${CROFT_LIBC_DEPENDENCY_REPORT_TSV}
        ${CROFT_WASI_VENDOR_REPORT_TXT}
        ${CROFT_WASI_VENDOR_REPORT_JSON}
        "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.tsv"
        "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.tsv"
)
if(TARGET test_wasi_vendor_report_smoke)
    add_dependencies(test_wasi_vendor_report_smoke croft_dependency_report)
endif()
if(CROFT_INSTALL_TARGETS)
    set(CROFT_FULL_MACHINE_DEPS ${CROFT_INSTALL_TARGETS} croft_dependency_report)
    if(TARGET croft_wasm_guests)
        list(APPEND CROFT_FULL_MACHINE_DEPS croft_wasm_guests)
    endif()
    add_custom_target(croft_full_machine ALL
        DEPENDS ${CROFT_FULL_MACHINE_DEPS}
    )
endif()

install(TARGETS ${CROFT_INSTALL_TARGETS}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

get_property(CROFT_EXAMPLE_ENTRIES GLOBAL PROPERTY CROFT_EXAMPLE_ENTRIES)
if(NOT CROFT_EXAMPLE_ENTRIES)
    set(CROFT_EXAMPLE_ENTRIES "")
endif()
string(JOIN ",\n" CROFT_EXAMPLE_ENTRIES_JSON ${CROFT_EXAMPLE_ENTRIES})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-examples.json"
    CONTENT "{\n  \"examples\": [\n${CROFT_EXAMPLE_ENTRIES_JSON}\n  ]\n}\n")

get_property(CROFT_EXAMPLE_TARGETS GLOBAL PROPERTY CROFT_EXAMPLE_TARGETS)
if(CROFT_EXAMPLE_TARGETS)
    add_custom_target(croft_examples DEPENDS ${CROFT_EXAMPLE_TARGETS})
    string(JOIN "\n" CROFT_EXAMPLE_TARGETS_TEXT ${CROFT_EXAMPLE_TARGETS})
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-example-targets.txt"
        CONTENT "${CROFT_EXAMPLE_TARGETS_TEXT}\n")
else()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/croft-example-targets.txt" CONTENT "")
endif()

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

install(FILES ${CROFT_PUBLIC_GENERATED_HEADERS} DESTINATION include/generated)
install(FILES ${CROFT_PUBLIC_GENERATED_MANIFESTS} DESTINATION share/croft/wit-manifests)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/croft-artifacts.json" DESTINATION share/croft)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/croft-xpi.json" DESTINATION share/croft)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/croft-wasm-guests.json" DESTINATION share/croft)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-current-machine-targets.tsv"
    "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/reports/croft-system-links.tsv"
    "${CROFT_LIBC_DEPENDENCY_REPORT_TXT}"
    "${CROFT_LIBC_DEPENDENCY_REPORT_TSV}"
    "${CROFT_WASI_VENDOR_REPORT_TXT}"
    "${CROFT_WASI_VENDOR_REPORT_JSON}"
    DESTINATION share/croft/reports
)

# CPack configuration
set(CPACK_PACKAGE_NAME "croft-host")
set(CPACK_PACKAGE_VENDOR "Lambkin-Lang")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Croft modular runtime artifacts")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_GENERATOR "ZIP;TGZ")

include(CPack)
