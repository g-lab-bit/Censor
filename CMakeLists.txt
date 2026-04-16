cmake_minimum_required(VERSION 3.20)

project(Censor
    VERSION 1.0.0
    DESCRIPTION "AEC vector element classifier — Rapida semantic sidecar DLL"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Shared library — the Censor DLL / .so / .dylib
# ---------------------------------------------------------------------------

add_library(censor SHARED
    src/censor.cpp
)

target_include_directories(censor PRIVATE
    include
)

target_compile_definitions(censor PRIVATE
    CENSOR_BUILDING_DLL
)

# Hide non-exported symbols on non-Windows platforms so only the six
# CENSOR_API symbols appear in the dynamic symbol table.
if(NOT WIN32)
    target_compile_options(censor PRIVATE -fvisibility=hidden)
endif()

# Output naming follows the spec:
#   Windows : censor.dll     (CMake default — no lib prefix)
#   Linux   : libcensor.so   (CMake default)
#   macOS   : libcensor.dylib (CMake default)
# No PREFIX override needed.

# ---------------------------------------------------------------------------
# Install targets
# ---------------------------------------------------------------------------

install(TARGETS censor
    RUNTIME DESTINATION bin    # Windows: censor.dll
    LIBRARY DESTINATION lib    # Linux/macOS: libcensor.so / libcensor.dylib
)

install(FILES include/censor_abi.h
    DESTINATION include
)
