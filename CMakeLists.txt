cmake_minimum_required(VERSION 3.16)
project(Censor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Symbol visibility — default hidden so only CENSOR_EXPORT symbols are public.
if(NOT MSVC)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# ---------------------------------------------------------------------------
# censor shared library (the DLL)
# ---------------------------------------------------------------------------
add_library(censor SHARED
    src/censor_plugin.cpp
)

target_include_directories(censor
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
)

set_target_properties(censor PROPERTIES
    OUTPUT_NAME "censor"
    PREFIX      ""   # No "lib" prefix on Linux either — matches "censor.dll"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(CENSOR_BUILD_TESTS "Build Censor unit tests" ON)

if(CENSOR_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # Prevent overriding the parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(test_poison_banner
        tests/test_poison_banner.cpp
        # Include the plugin sources directly so tests can access internals
        # through the public header without needing the shared library load path.
        src/censor_plugin.cpp
    )

    target_include_directories(test_poison_banner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_poison_banner PRIVATE
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(test_poison_banner)
endif()
