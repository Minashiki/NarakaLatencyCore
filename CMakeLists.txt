cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

project(NarakaLatencyCore VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "NarakaLatencyCore supports Windows x64 only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

set(NLC_WINDIVERT_ROOT "" CACHE PATH "Path to the extracted WinDivert-2.2.2-A directory")
option(NLC_FETCH_WINDIVERT "Download the official WinDivert 2.2.2 binary SDK" ON)
option(NLC_BUILD_TESTS "Build GoogleTest tests" ON)

if(NOT NLC_WINDIVERT_ROOT AND NLC_FETCH_WINDIVERT)
    FetchContent_Declare(
        windivert_sdk
        URL https://reqrypt.org/download/WinDivert-2.2.2-A.zip
        URL_HASH SHA256=63CB41763BB4B20F600B6DE04E991A9C2BE73279E317D4D82F237B150C5F3F15
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(windivert_sdk)
    set(NLC_WINDIVERT_ROOT "${windivert_sdk_SOURCE_DIR}")
endif()

find_path(WINDIVERT_INCLUDE_DIR windivert.h
    HINTS "${NLC_WINDIVERT_ROOT}/include")
find_library(WINDIVERT_LIBRARY WinDivert
    HINTS "${NLC_WINDIVERT_ROOT}/x64")

if(NOT WINDIVERT_INCLUDE_DIR OR NOT WINDIVERT_LIBRARY)
    message(FATAL_ERROR "WinDivert 2.2 SDK not found; set NLC_WINDIVERT_ROOT")
endif()

add_library(nlc_core
    src/async_csv_recorder.cpp
    src/delay_scheduler.cpp
    src/engine_controller.cpp
    src/engine_state_machine.cpp
    src/metrics.cpp
    src/packet_pool.cpp
    src/qpc_clock.cpp
    src/rotating_logger.cpp
    src/windivert_engine.cpp)

target_include_directories(nlc_core
    PUBLIC include "${WINDIVERT_INCLUDE_DIR}")
target_link_libraries(nlc_core PUBLIC "${WINDIVERT_LIBRARY}" advapi32)
target_compile_definitions(nlc_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(nlc_core PRIVATE /W4 /permissive- /EHsc)

add_library(nlc_native SHARED src/public_api.cpp)
target_link_libraries(nlc_native PRIVATE nlc_core)
target_include_directories(nlc_native PUBLIC include)
target_compile_definitions(nlc_native PRIVATE NL_NATIVE_EXPORTS)
set_target_properties(nlc_native PROPERTIES OUTPUT_NAME "NarakaLatency.Native")

add_executable(nlc_delay_console tools/delay_console.cpp)
target_link_libraries(nlc_delay_console PRIVATE nlc_native)

add_executable(nlc_lifecycle_probe tools/lifecycle_probe.cpp)
target_link_libraries(nlc_lifecycle_probe PRIVATE nlc_native)

add_executable(nlc_udp_echo_server tools/udp_echo_server.cpp)
target_link_libraries(nlc_udp_echo_server PRIVATE ws2_32)
target_compile_definitions(nlc_udp_echo_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(nlc_udp_echo_client tools/udp_echo_client.cpp)
target_link_libraries(nlc_udp_echo_client PRIVATE ws2_32)
target_compile_definitions(nlc_udp_echo_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(nlc_tcp_echo_server tools/tcp_echo_server.cpp)
target_link_libraries(nlc_tcp_echo_server PRIVATE ws2_32)
target_compile_definitions(nlc_tcp_echo_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(nlc_tcp_echo_client tools/tcp_echo_client.cpp)
target_link_libraries(nlc_tcp_echo_client PRIVATE ws2_32)
target_compile_definitions(nlc_tcp_echo_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(EXISTS "${NLC_WINDIVERT_ROOT}/x64/WinDivert.dll")
    foreach(target nlc_native nlc_delay_console nlc_lifecycle_probe)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NLC_WINDIVERT_ROOT}/x64/WinDivert.dll"
                "$<TARGET_FILE_DIR:${target}>")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NLC_WINDIVERT_ROOT}/x64/WinDivert64.sys"
                "$<TARGET_FILE_DIR:${target}>")
    endforeach()
endif()

if(NLC_BUILD_TESTS)
    enable_testing()
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        URL_HASH SHA256=40D4EC942217DCC84A9EBE2A68584ADA7D4A33A8EE958755763278EA1C5E18FF
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)

    add_executable(nlc_tests
        tests/delay_scheduler_tests.cpp
        tests/engine_state_tests.cpp
        tests/filter_tests.cpp
        tests/lifecycle_tests.cpp
        tests/metrics_tests.cpp
        tests/public_api_tests.cpp
        tests/settings_tests.cpp
        tests/stress_tests.cpp)
    target_link_libraries(nlc_tests PRIVATE nlc_core nlc_native GTest::gtest_main)
    target_compile_options(nlc_tests PRIVATE /W4 /permissive- /EHsc)
    include(GoogleTest)
    gtest_discover_tests(nlc_tests DISCOVERY_TIMEOUT 60)
endif()
