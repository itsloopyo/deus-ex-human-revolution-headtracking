cmake_minimum_required(VERSION 3.20)
project(DeusExHumanRevolutionHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "DeusExHumanRevolutionHeadTracking must be built as Win32 (x86). Configure with -A Win32.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

set(MOD_SOURCES
    src/dllmain.cpp
    src/config.cpp
    src/path_utils.cpp
    src/tracking_runtime.cpp
    src/hotkeys.cpp
    src/build_profile.cpp
    src/camera_hook.cpp
    src/lean_trace.cpp
    src/reticle_hook.cpp
    src/ads.cpp
    src/aim_marker.cpp
)

add_library(DeusExHumanRevolutionHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${MOD_SOURCES}
)

set_target_properties(DeusExHumanRevolutionHeadTracking PROPERTIES
    OUTPUT_NAME "DeusExHumanRevolutionHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(DeusExHumanRevolutionHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

target_link_libraries(DeusExHumanRevolutionHeadTracking PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(DeusExHumanRevolutionHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
endif()

if(WIN32)
    target_link_libraries(DeusExHumanRevolutionHeadTracking PRIVATE ws2_32)
endif()

# Opt-in native unit tests for the logic that is decidable without the game: the
# config-sanitization boundary, and the ADS transition and entry pose.
# OFF by default so the default build and the release packager never produce a
# test binary in bin/. Enable with -DDXHR_BUILD_TESTS=ON.
option(DXHR_BUILD_TESTS "Build DeusExHumanRevolutionHeadTracking unit tests" OFF)
if(DXHR_BUILD_TESTS)
    enable_testing()
    add_executable(dxhr_config_sanitize_tests tests/config_sanitize_tests.cpp)
    target_include_directories(dxhr_config_sanitize_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(dxhr_config_sanitize_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(dxhr_config_sanitize_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME config_sanitize COMMAND dxhr_config_sanitize_tests)

    add_executable(dxhr_ads_tests tests/ads_tests.cpp)
    target_include_directories(dxhr_ads_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(dxhr_ads_tests PRIVATE cameraunlock)
    set_target_properties(dxhr_ads_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(dxhr_ads_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ads COMMAND dxhr_ads_tests)
endif()
