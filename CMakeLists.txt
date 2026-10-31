cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(network_bridge
    VERSION 0.2.0
    DESCRIPTION "Heterogeneous network communication framework (RPC, pub/sub, streaming)"
    HOMEPAGE_URL "https://github.com/zero-pipe/network_bridge"
    LANGUAGES C)

include(GNUInstallDirs)

if(MSVC)
    add_compile_options(/utf-8)
    # Bundled Windows GoogleTest archives use the static MSVC runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 基础依赖：network-tool-kit
set(NTK_ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/ntk)
add_subdirectory(3rdparty/elog)

# Public options use the NB_ prefix so they can be embedded with
# add_subdirectory() without colliding with the parent project. The legacy
# unprefixed names below are still honoured for one release cycle.
option(NB_BUILD_TESTS "Build tests" ON)
option(NB_BUILD_TOOLS "Build benchmark tools" ON)
option(NB_BUILD_EXAMPLES "Build examples" ON)
option(NB_BUILD_GTESTS "Build GoogleTest module tests" OFF)
option(NB_BUILD_BENCHMARKS "Build Google Benchmark microbenchmarks" OFF)
option(NB_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(NB_ENABLE_DDS "Enable the Cyclone DDS transport provider" ON)

if(DEFINED BUILD_TESTS)
    message(WARNING "BUILD_TESTS is deprecated; use NB_BUILD_TESTS")
    set(NB_BUILD_TESTS "${BUILD_TESTS}" CACHE BOOL "" FORCE)
    unset(BUILD_TESTS CACHE)
endif()
if(DEFINED BUILD_TOOLS)
    message(WARNING "BUILD_TOOLS is deprecated; use NB_BUILD_TOOLS")
    set(NB_BUILD_TOOLS "${BUILD_TOOLS}" CACHE BOOL "" FORCE)
    unset(BUILD_TOOLS CACHE)
endif()
if(DEFINED BUILD_EXAMPLES)
    message(WARNING "BUILD_EXAMPLES is deprecated; use NB_BUILD_EXAMPLES")
    set(NB_BUILD_EXAMPLES "${BUILD_EXAMPLES}" CACHE BOOL "" FORCE)
    unset(BUILD_EXAMPLES CACHE)
endif()
if(DEFINED BUILD_GTESTS)
    message(WARNING "BUILD_GTESTS is deprecated; use NB_BUILD_GTESTS")
    set(NB_BUILD_GTESTS "${BUILD_GTESTS}" CACHE BOOL "" FORCE)
    unset(BUILD_GTESTS CACHE)
endif()
if(DEFINED BUILD_BENCHMARKS)
    message(WARNING "BUILD_BENCHMARKS is deprecated; use NB_BUILD_BENCHMARKS")
    set(NB_BUILD_BENCHMARKS "${BUILD_BENCHMARKS}" CACHE BOOL "" FORCE)
    unset(BUILD_BENCHMARKS CACHE)
endif()
if(DEFINED BUILD_FUZZERS)
    message(WARNING "BUILD_FUZZERS is deprecated; use NB_BUILD_FUZZERS")
    set(NB_BUILD_FUZZERS "${BUILD_FUZZERS}" CACHE BOOL "" FORCE)
    unset(BUILD_FUZZERS CACHE)
endif()

set(NB_SANITIZE "" CACHE STRING "Sanitizer set: address|undefined|thread|address,undefined")
if(DEFINED ZERO_RPC_SANITIZE AND NOT NB_SANITIZE)
    message(WARNING "ZERO_RPC_SANITIZE is deprecated; use NB_SANITIZE")
    set(NB_SANITIZE "${ZERO_RPC_SANITIZE}" CACHE STRING "" FORCE)
    unset(ZERO_RPC_SANITIZE CACHE)
endif()

if(NB_ENABLE_DDS)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NB_CYCLONEDDS_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cyclonedds/windows_amd64")
        set(NB_CYCLONEDDS_LIB "${NB_CYCLONEDDS_ROOT}/lib/ddsc.lib")
        set(NB_CYCLONEDDS_DLL "${NB_CYCLONEDDS_ROOT}/bin/ddsc.dll")
    elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(NB_CYCLONEDDS_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cyclonedds/linux_arm64")
        set(NB_CYCLONEDDS_LIB "${NB_CYCLONEDDS_ROOT}/lib/libddsc.so")
    elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "loongarch64")
        set(NB_CYCLONEDDS_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cyclonedds/linux_loongarch64")
        set(NB_CYCLONEDDS_LIB "${NB_CYCLONEDDS_ROOT}/lib/libddsc.so")
    elseif(UNIX)
        set(NB_CYCLONEDDS_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/cyclonedds/linux_amd64")
        set(NB_CYCLONEDDS_LIB "${NB_CYCLONEDDS_ROOT}/lib/libddsc.so")
    endif()

    if(NB_CYCLONEDDS_ROOT AND EXISTS "${NB_CYCLONEDDS_ROOT}/include/dds/dds.h")
        add_library(nb_cyclonedds INTERFACE)
        target_include_directories(nb_cyclonedds INTERFACE "${NB_CYCLONEDDS_ROOT}/include")
        target_link_libraries(nb_cyclonedds INTERFACE "${NB_CYCLONEDDS_LIB}")
        target_compile_definitions(nb_cyclonedds INTERFACE NB_ENABLE_DDS=1)
    else()
        message(WARNING "Cyclone DDS SDK not found; disabling NB_ENABLE_DDS")
        set(NB_ENABLE_DDS OFF CACHE BOOL "" FORCE)
    endif()
endif()

if(NB_SANITIZE)
    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR
            "NB_SANITIZE=${NB_SANITIZE} is not supported by MSVC; "
            "use GCC/Clang (e.g. on Linux) or clang-cl for sanitizer builds.")
    endif()
    add_compile_options(-fsanitize=${NB_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${NB_SANITIZE})
endif()

# The prebuilt Cyclone DDS package is linked by absolute path. Add its lib
# directory to the build-tree RPATH so test and example binaries find
# libddsc.so without a manual LD_LIBRARY_PATH.
if(NB_ENABLE_DDS AND UNIX AND NOT APPLE)
    list(APPEND CMAKE_BUILD_RPATH "${NB_CYCLONEDDS_ROOT}/lib")
endif()

# The prebuilt Cyclone DDS package keeps ddsc.dll next to its import library,
# not on PATH. Copy it next to every executable that links nb.
function(nb_copy_runtime_deps target)
    if(WIN32 AND NB_ENABLE_DDS AND NB_CYCLONEDDS_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NB_CYCLONEDDS_DLL}"
                "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

if(NB_BUILD_GTESTS OR NB_BUILD_BENCHMARKS)
    enable_language(CXX)
endif()

add_subdirectory(src)

if(NB_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NB_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

if(NB_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(NB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(NB_BUILD_GTESTS OR NB_BUILD_BENCHMARKS)
    add_subdirectory(tests/google)
endif()

# Unified quality gates. Build the target first, then the gate runs:
#   cmake --build build --target gate-unit
#   cmake --build build --target gate-e2e
#   cmake --build build --target gate-perf
#   cmake --build build --target gate-stability
#   cmake --build build --target gate-all
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(NB_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/tools/gate.py)
    set(NB_GATE_ARGS --build-dir ${CMAKE_BINARY_DIR} --no-build)
    if(CMAKE_CONFIGURATION_TYPES)
        list(APPEND NB_GATE_ARGS --config $<CONFIG>)
    endif()

    if(TARGET test_framing)
        add_custom_target(gate-unit
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} unit ${NB_GATE_ARGS}
            DEPENDS test_framing test_config test_metrics test_registry
                    test_reliable test_poller_alloc test_iobuf test_headers test_buffer
                    test_buf_concurrency test_slotpool test_poolset test_contracts
            USES_TERMINAL VERBATIM)
    endif()

    if(TARGET bench_server AND TARGET loadgen AND TARGET test_loopback)
        add_custom_target(gate-e2e
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} e2e ${NB_GATE_ARGS}
            DEPENDS test_loopback test_stream test_api test_worker test_semantics
                    test_move bench_server loadgen
                    $<$<TARGET_EXISTS:test_dds_local>:test_dds_local>
            USES_TERMINAL VERBATIM)
    endif()

    if(TARGET bench_server AND TARGET loadgen)
        add_custom_target(gate-perf
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} perf ${NB_GATE_ARGS}
            DEPENDS bench_server loadgen
            USES_TERMINAL VERBATIM)
        add_custom_target(gate-stability
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} stability ${NB_GATE_ARGS}
            DEPENDS bench_server loadgen
            USES_TERMINAL VERBATIM)
    endif()

    if(TARGET fuzz_envelope)
        add_custom_target(gate-fuzz
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} fuzz ${NB_GATE_ARGS}
            DEPENDS fuzz_envelope fuzz_metadata fuzz_framing
            USES_TERMINAL VERBATIM)
    endif()

    if(TARGET gate-unit AND TARGET gate-e2e AND TARGET gate-perf AND TARGET gate-stability)
        add_custom_target(gate-all
            COMMAND ${Python3_EXECUTABLE} ${NB_GATE_SCRIPT} all ${NB_GATE_ARGS}
            DEPENDS test_framing test_config test_metrics test_registry
                    test_reliable test_poller_alloc test_iobuf test_headers test_buffer
                    test_buf_concurrency test_slotpool test_poolset test_contracts
                    test_loopback test_stream test_api test_move test_worker test_semantics
                    bench_server loadgen
            USES_TERMINAL VERBATIM)
    endif()
endif()
