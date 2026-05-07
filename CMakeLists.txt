cmake_minimum_required(VERSION 3.16)
project(Censor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Module source layout
#
# To add a new source file: add it to src/<module>/ — GLOB_RECURSE picks it
# up automatically on the next build (CONFIGURE_DEPENDS re-triggers cmake on
# file-system changes so you don't need to re-run cmake manually).
#
# Modules: clustering  storage  classifier  grid  overlay
#
# To add a new test: follow the test_clustering pattern below:
#   add_executable(test_<module> tests/test_<module>.cpp)
#   target_include_directories(test_<module> PRIVATE
#       ${CMAKE_CURRENT_SOURCE_DIR}/src
#       ${CMAKE_CURRENT_SOURCE_DIR}/include)
#   target_link_libraries(test_<module> PRIVATE censor GTest::gtest_main)
#   gtest_discover_tests(test_<module>)
# ---------------------------------------------------------------------------

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
file(GLOB_RECURSE CENSOR_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(censor SHARED ${CENSOR_SOURCES})

target_include_directories(censor
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
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
    include(GoogleTest)

    add_executable(test_poison_banner
        tests/test_poison_banner.cpp
        # Include the plugin sources directly so tests can access internals
        # through the public header without needing the shared library load path.
        src/censor_plugin.cpp
    )

    target_include_directories(test_poison_banner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(test_poison_banner PRIVATE
        GTest::gtest_main
    )

    gtest_discover_tests(test_poison_banner)

    # ---------------------------------------------------------------------------
    # Smoke test: loads censor.so via dlopen and exercises all ABI exports.
    # No link-time dependency on censor — the DLL path is injected at build time.
    # ---------------------------------------------------------------------------
    add_executable(test_smoke_dlopen
        tests/test_smoke_dlopen.cpp
    )

    target_include_directories(test_smoke_dlopen PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Inject the full path to the censor shared library so the test can dlopen it.
    target_compile_definitions(test_smoke_dlopen PRIVATE
        CENSOR_DLL_PATH="$<TARGET_FILE:censor>"
    )

    target_link_libraries(test_smoke_dlopen PRIVATE
        GTest::gtest_main
        ${CMAKE_DL_LIBS}   # -ldl on Linux; empty on macOS/Windows
    )

    # censor must be built before the smoke test runs (dlopen needs the file).
    add_dependencies(test_smoke_dlopen censor)

    gtest_discover_tests(test_smoke_dlopen)

    # ---------------------------------------------------------------------------
    # Per-module test executables (test_clustering pattern)
    # Each links against the censor shared library + GTest.
    # To add a module test: follow this foreach pattern.
    # ---------------------------------------------------------------------------
    foreach(module clustering storage classifier grid overlay)
        add_executable(test_${module}
            tests/test_${module}.cpp
        )
        target_include_directories(test_${module} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(test_${module} PRIVATE
            censor
            GTest::gtest_main
        )
        gtest_discover_tests(test_${module})
    endforeach()
endif()
