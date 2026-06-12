/*
 * censor_visibility.h — CENSOR_API symbol-visibility macro (ce-hcy).
 *
 * Marks internal C++ classes that must be reachable across the censor shared
 * library boundary (the test_storage/test_classifier/test_label_palette
 * executables link the DLL). Three states on MSVC because test linkage is
 * mixed (some tests compile sources directly and must see a plain class):
 *
 *   CENSOR_BUILD_DLL  defined when compiling the censor DLL itself -> dllexport
 *   CENSOR_USE_DLL    defined when linking against censor.dll      -> dllimport
 *   (neither)         sources compiled directly into a test exe    -> empty
 *
 * On GCC/Clang the visibility attribute both exports (DLL build) and is a
 * harmless no-op in direct-compile tests, so one state suffices.
 *
 * NOTE: bare __attribute__((visibility("default"))) is a HARD SYNTAX ERROR on
 * MSVC (discovered 2026-06-12 — Censor had never compiled on Windows). Always
 * use CENSOR_API, never the raw attribute. The C ABI entry points use the
 * separate CENSOR_EXPORT macro in include/censor_abi.h.
 */

#pragma once


#if defined(_MSC_VER)
#  if defined(CENSOR_BUILD_DLL)
#    define CENSOR_API __declspec(dllexport)
#  elif defined(CENSOR_USE_DLL)
#    define CENSOR_API __declspec(dllimport)
#  else
#    define CENSOR_API
#  endif
#else
#  define CENSOR_API __attribute__((visibility("default")))
#endif
