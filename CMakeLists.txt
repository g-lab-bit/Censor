cmake_minimum_required(VERSION 3.16)
project(Censor VERSION 1.0.0 LANGUAGES C CXX)

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
    # /wd4251 /wd4275: dll-interface warnings for std:: members of CENSOR_API
    # classes — standard cross-platform-DLL suppression; the DLL and its tests
    # are always built together with the same toolchain/CRT (ce-hcy).
    add_compile_options(/W4 /WX /wd4251 /wd4275)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Symbol visibility — default hidden so only CENSOR_EXPORT symbols are public.
if(NOT MSVC)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# ---------------------------------------------------------------------------
# SQLite amalgamation — compiled as C (not C++) to match upstream expectations.
# ---------------------------------------------------------------------------
add_library(sqlite3_obj OBJECT third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3_obj PUBLIC third_party/sqlite)
set_target_properties(sqlite3_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(sqlite3_obj PRIVATE
    SQLITE_API=
    SQLITE_OMIT_LOAD_EXTENSION=1
)
if(MSVC)
    # Third-party amalgamation is not /W4-clean; don't let /WX block the build.
    target_compile_options(sqlite3_obj PRIVATE /w /WX-)
endif()

# ---------------------------------------------------------------------------
# censor shared library (the DLL)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CENSOR_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(censor SHARED ${CENSOR_SOURCES} $<TARGET_OBJECTS:sqlite3_obj>)

target_include_directories(censor
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite>
)

set_target_properties(censor PROPERTIES
    OUTPUT_NAME "censor"
    PREFIX      ""   # No "lib" prefix on Linux either — matches "censor.dll"
)

# ce-hcy: CENSOR_API resolves to dllexport while building the DLL on MSVC.
# ce-882: match SQLITE_API= so censor_db.cpp declarations agree with sqlite3_obj.
target_compile_definitions(censor PRIVATE CENSOR_BUILD_DLL SQLITE_API=)

# ---------------------------------------------------------------------------
# ra-vbj53 — Rapida public C ABI headers (rapida_vector_engine_c.h,
# rapida_host_callbacks.h). Censor consumes them when the real pipeline lands
# (Censor-x7d); per SPEC-censor-integration §Shim header location, Censor never
# includes anything from Rapida's src/. The checkout path is machine-specific
# (Windows: C:/Users/gdilo/Rapida, linux: ~/gt/Rapida), so it is supplied via
# -DRAPIDA_PUBLIC_INCLUDE=<path> or the RAPIDA_PUBLIC_INCLUDE env var.
# OPTIONAL: when absent, Censor still builds fully standalone.
# ---------------------------------------------------------------------------
set(RAPIDA_PUBLIC_INCLUDE "$ENV{RAPIDA_PUBLIC_INCLUDE}" CACHE PATH
    "Path to Rapida's libs/rapida/include/public directory (C ABI shim headers)")
if(RAPIDA_PUBLIC_INCLUDE AND EXISTS "${RAPIDA_PUBLIC_INCLUDE}/rapida_vector_engine_c.h")
    target_include_directories(censor PRIVATE "${RAPIDA_PUBLIC_INCLUDE}")
    target_compile_definitions(censor PRIVATE CENSOR_HAVE_RAPIDA_SHIM=1)
    message(STATUS "Censor: Rapida shim headers found at ${RAPIDA_PUBLIC_INCLUDE}")
else()
    message(STATUS "Censor: RAPIDA_PUBLIC_INCLUDE not set or headers absent — standalone build (ra-vbj53)")
endif()

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
    # test_clustering — internal module; compiles sources directly to access
    # symbols hidden by the shared-library visibility preset.
    # ---------------------------------------------------------------------------
    add_executable(test_clustering
        tests/test_clustering.cpp
        src/clustering/feature_vector.cpp
        src/clustering/spatial_clustering.cpp
    )
    target_include_directories(test_clustering PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_clustering PRIVATE
        GTest::gtest_main
    )
    gtest_discover_tests(test_clustering)

    # ---------------------------------------------------------------------------
    # Per-module test executables
    # Each links against the censor shared library + GTest.
    # To add a module test: follow this foreach pattern.
    # ---------------------------------------------------------------------------
    # ---------------------------------------------------------------------------
    # test_grid — internal module; compiles sources directly.
    # ---------------------------------------------------------------------------
    add_executable(test_grid
        tests/test_grid.cpp
        src/grid/grid_system.cpp
        src/grid/grid_overlay.cpp
    )
    target_include_directories(test_grid PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_grid PRIVATE
        GTest::gtest_main
    )
    gtest_discover_tests(test_grid)

    foreach(module storage classifier label_palette active_learning)
        add_executable(test_${module}
            tests/test_${module}.cpp
        )
        target_include_directories(test_${module} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        # ce-hcy: these tests link the DLL — CENSOR_API resolves to dllimport.
        target_compile_definitions(test_${module} PRIVATE CENSOR_USE_DLL)
        target_link_libraries(test_${module} PRIVATE
            censor
            GTest::gtest_main
        )
        gtest_discover_tests(test_${module})
    endforeach()

    # ---------------------------------------------------------------------------
    # test_overlay — internal module; compiles sources directly to access
    # symbols hidden by the shared-library visibility preset.
    # Covers: completeness, discovery_queue, debug_viz_data.
    # ---------------------------------------------------------------------------
    add_executable(test_overlay
        tests/test_overlay.cpp
        src/clustering/feature_vector.cpp
        src/clustering/spatial_clustering.cpp
        src/classifier/knn_classifier.cpp
        src/classifier/background_worker.cpp
        src/overlay/completeness.cpp
        src/overlay/confidence_overlay.cpp
        src/overlay/debug_viz_data.cpp
        src/overlay/discovery_queue.cpp
    )
    target_include_directories(test_overlay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_overlay PRIVATE
        GTest::gtest_main
    )
    gtest_discover_tests(test_overlay)

    # ---------------------------------------------------------------------------
    # test_confusable_popover — confusable negative popover + rejection toast.
    # ---------------------------------------------------------------------------
    add_executable(test_confusable_popover
        tests/test_confusable_popover.cpp
        src/overlay/confusable_popover.cpp
        src/overlay/rejection_toast.cpp
    )
    target_include_directories(test_confusable_popover PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_confusable_popover PRIVATE
        GTest::gtest_main
    )
    gtest_discover_tests(test_confusable_popover)

    # ---------------------------------------------------------------------------
    # test_test_utils — exercises the synthetic data generators in test_utils.h.
    # Header-only; no censor shared library needed — only GTest.
    # ---------------------------------------------------------------------------
    add_executable(test_test_utils
        tests/test_test_utils.cpp
    )
    target_include_directories(test_test_utils PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_test_utils PRIVATE
        GTest::gtest_main
    )
    gtest_discover_tests(test_test_utils)

endif()
