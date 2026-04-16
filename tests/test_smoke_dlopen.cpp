/*
 * test_smoke_dlopen.cpp — CI smoke harness for the Censor DLL.
 *
 * Loads censor.so / censor.dll via dlopen / LoadLibraryEx (no link-time
 * dependency on censor), resolves all five C ABI exports, exercises the
 * documented lifecycle, and verifies the no-DLL fallback path.
 *
 * CENSOR_DLL_PATH is injected by CMake via target_compile_definitions so the
 * test always finds the DLL built in the same CMake build tree.
 *
 * Two test cases — both must pass in CI:
 *   SmokeDlopen.LoadAndExerciseAllExports — happy path
 *   SmokeDlopen.MissingDllFallback        — graceful no-DLL fallback
 *
 * Pairs with: ce-nup
 * Spec ref: SPEC-censor-integration.md §"Lifecycle"
 */

#include "censor_abi.h"
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* ---------------------------------------------------------------------------
 * Platform DLL loading shim
 * -------------------------------------------------------------------------*/

#if defined(_WIN32)
#  include <windows.h>
   using DllHandle = HMODULE;
   static DllHandle dll_open(const char* path)
   {
       return LoadLibraryExA(path, NULL,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                             LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
   }
   static void* dll_sym(DllHandle h, const char* sym)
   {
       return reinterpret_cast<void*>(GetProcAddress(h, sym));
   }
   static void dll_close(DllHandle h) { FreeLibrary(h); }
#else
#  include <dlfcn.h>
   using DllHandle = void*;
   static DllHandle dll_open(const char* path)
   {
       (void)dlerror(); /* clear previous error */
       return dlopen(path, RTLD_NOW | RTLD_LOCAL);
   }
   static void* dll_sym(DllHandle h, const char* sym) { return dlsym(h, sym); }
   static void  dll_close(DllHandle h)                { dlclose(h); }
#endif

/* ---------------------------------------------------------------------------
 * Function pointer types — match the censor_abi.h signatures exactly
 * -------------------------------------------------------------------------*/

using FnAbiVersion   = uint32_t (*)(void);
using FnInit         = int32_t  (*)(const RapidaHostCallbacks*);
using FnShutdown     = void     (*)(void);
using FnAttachEngine = int32_t  (*)(RapidaVectorEngineHandle);
using FnDetachEngine = void     (*)(void);

/* ---------------------------------------------------------------------------
 * CENSOR_DLL_PATH — injected by CMake (see CMakeLists.txt)
 * -------------------------------------------------------------------------*/

#ifndef CENSOR_DLL_PATH
#  error "CENSOR_DLL_PATH must be defined by the CMake build system"
#endif

/* ---------------------------------------------------------------------------
 * Mock host callbacks
 * -------------------------------------------------------------------------*/

static std::atomic<int> g_fatal_count{0};
static std::atomic<int> g_log_count{0};

static void mock_log(void* /*ud*/, int /*level*/,
                     const char* /*cat*/, const char* /*msg*/)
{
    g_log_count.fetch_add(1, std::memory_order_relaxed);
}

static void mock_fatal(void* /*ud*/, const char* /*reason*/)
{
    g_fatal_count.fetch_add(1, std::memory_order_relaxed);
}

static RapidaHostCallbacks make_callbacks()
{
    RapidaHostCallbacks cb{};
    cb.struct_version  = 1;
    cb.log             = mock_log;
    cb.on_censor_fatal = mock_fatal;
    cb.user_data       = nullptr;
    return cb;
}

/* ---------------------------------------------------------------------------
 * Tests
 * -------------------------------------------------------------------------*/

/* Happy path: load the DLL, resolve all exports, exercise the full lifecycle.
 *
 * Lifecycle under test (SPEC-censor-integration.md §"Lifecycle"):
 *   dlopen → censor_abi_version → censor_init → censor_attach_engine
 *   → censor_detach_engine → censor_shutdown → dlclose
 */
TEST(SmokeDlopen, LoadAndExerciseAllExports)
{
    g_fatal_count.store(0, std::memory_order_relaxed);
    g_log_count.store(0, std::memory_order_relaxed);

    /* 1. Load the DLL. */
    DllHandle h = dll_open(CENSOR_DLL_PATH);
    ASSERT_NE(h, static_cast<DllHandle>(nullptr))
        << "dlopen/LoadLibraryEx failed for: " << CENSOR_DLL_PATH;

    /* 2. Resolve all five C ABI symbols. */
    auto fn_ver    = reinterpret_cast<FnAbiVersion>  (dll_sym(h, "censor_abi_version"));
    auto fn_init   = reinterpret_cast<FnInit>        (dll_sym(h, "censor_init"));
    auto fn_shut   = reinterpret_cast<FnShutdown>    (dll_sym(h, "censor_shutdown"));
    auto fn_attach = reinterpret_cast<FnAttachEngine>(dll_sym(h, "censor_attach_engine"));
    auto fn_detach = reinterpret_cast<FnDetachEngine>(dll_sym(h, "censor_detach_engine"));

    ASSERT_NE(fn_ver,    nullptr) << "censor_abi_version symbol not found";
    ASSERT_NE(fn_init,   nullptr) << "censor_init symbol not found";
    ASSERT_NE(fn_shut,   nullptr) << "censor_shutdown symbol not found";
    ASSERT_NE(fn_attach, nullptr) << "censor_attach_engine symbol not found";
    ASSERT_NE(fn_detach, nullptr) << "censor_detach_engine symbol not found";

    /* 3. ABI version check — major must match.
     *    Per spec: "host accepts any DLL whose major version matches." */
    uint32_t dll_ver      = fn_ver();
    uint32_t header_ver   = CENSOR_ABI_VERSION;
    uint32_t dll_major    = (dll_ver    >> 16) & 0xFFu;
    uint32_t header_major = (header_ver >> 16) & 0xFFu;

    EXPECT_EQ(dll_ver, header_ver)
        << "Full ABI version mismatch: DLL=0x" << std::hex << dll_ver
        << " header=0x" << header_ver;
    EXPECT_EQ(dll_major, header_major)
        << "ABI major version mismatch — host and DLL are incompatible";

    /* 4. Exercise the full lifecycle. */
    auto cb = make_callbacks();

    /* censor_init — must succeed (return 0). */
    int32_t rc = fn_init(&cb);
    EXPECT_EQ(rc, 0) << "censor_init returned error " << rc;

    /* censor_attach_engine — Phase 3 stub fires on_censor_fatal here.
     * Return value must still be 0: the fatal is signalled via callback,
     * not via return code. */
    void* fake_engine = reinterpret_cast<void*>(static_cast<uintptr_t>(0xdeadbeef));
    rc = fn_attach(fake_engine);
    EXPECT_EQ(rc, 0) << "censor_attach_engine returned error " << rc;

    /* censor_detach_engine — no return value, must not crash. */
    fn_detach();

    /* censor_shutdown — must not crash, must be idempotent. */
    fn_shut();
    fn_shut(); /* second call — idempotency check */

    /* 5. Callback accounting.
     *    log must have been called at least once (init logs an INFO message).
     *    on_censor_fatal must have been called exactly once (Phase 3 stub). */
    EXPECT_GT(g_log_count.load(), 0)
        << "log callback was never called — expected at least one INFO message";
    EXPECT_EQ(g_fatal_count.load(), 1)
        << "on_censor_fatal should fire exactly once during attach_engine "
           "(Phase 3 stub behaviour)";

    /* 6. Unload. */
    dll_close(h);
}

/* Fallback path: loading from a non-existent path must return a null handle.
 * The host (Rapida) gracefully continues without Censor in this case.
 * Per SPEC-censor-integration.md §"Graceful fallback". */
TEST(SmokeDlopen, MissingDllFallback)
{
#if defined(_WIN32)
    const char* bad_path = "C:\\nonexistent\\censor_missing.dll";
#else
    const char* bad_path = "/nonexistent/path/censor_missing.so";
#endif

    DllHandle h = dll_open(bad_path);
    EXPECT_EQ(h, static_cast<DllHandle>(nullptr))
        << "Expected null handle for missing DLL path; graceful fallback required";

    /* If somehow it loaded (very unlikely), release it. */
    if (h != static_cast<DllHandle>(nullptr)) {
        dll_close(h);
    }
}
