// censor.cpp — Hello-world Censor DLL scaffold.
//
// Exports all six C ABI entry points as sentinel stubs. No real
// classification work is performed. Goal: prove that the DLL loads cleanly
// via LoadLibraryEx / dlopen and that all six symbols resolve correctly.
//
// Phase 3 deliverable — SPEC-censor-integration.md.
// Pairs with: ce-nup (CI smoke harness).
//
// CENSOR_BUILDING_DLL is set by CMakeLists.txt via target_compile_definitions,
// which flips censor_abi.h's CENSOR_API macro to dllexport / visibility=default.

#include "censor_abi.h"

// ---------------------------------------------------------------------------
// Module-level state (POD only — no constructors before DllMain / init)
// ---------------------------------------------------------------------------

namespace {

// Deep copy of the host callback struct. Valid between censor_init() and
// censor_shutdown(). Zero-initialized at process start by the C runtime.
RapidaHostCallbacks g_host{};

// True after a successful censor_init(), false after censor_shutdown().
bool g_initialized = false;

// Engine handle for the active document session. Valid between
// censor_attach_engine() and censor_detach_engine().
RapidaVectorEngineHandle g_engine = nullptr;

// Route a log message through the host callback if one is installed.
// Inline so the compiler can eliminate it when logging is unnecessary.
inline void host_log(int level, const char* message) {
    if (g_host.log) {
        g_host.log(g_host.user_data, level, "censor", message);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Exported entry points — all use C linkage via the header's extern "C".
// ---------------------------------------------------------------------------

uint32_t censor_abi_version(void) {
    // Packed version: (major << 16) | (minor << 8) | patch
    // 1.0.0 → 0x00010000
    return (1u << 16) | (0u << 8) | 0u;
}

int32_t censor_init(const RapidaHostCallbacks* host) {
    if (!host) {
        return 1;  // Null callbacks struct — reject.
    }

    g_host        = *host;  // Deep copy — we own this from now on.
    g_initialized = true;
    g_engine      = nullptr;

    // Announce via the log sink so the CI smoke harness can grep for this.
    host_log(2 /* INFO */, "Censor 1.0.0 init — hello-world scaffold");

    return CENSOR_OK;
}

void censor_shutdown(void) {
    if (!g_initialized) {
        return;  // Idempotent — safe to call after a failed init.
    }

    host_log(2 /* INFO */, "Censor shutdown");

    g_engine      = nullptr;
    g_initialized = false;
    g_host        = {};  // Zero out — no dangling callback pointers.
}

int32_t censor_attach_engine(RapidaVectorEngineHandle engine) {
    g_engine = engine;
    host_log(2 /* INFO */, "Censor engine attached");
    return CENSOR_OK;
}

void censor_detach_engine(void) {
    host_log(2 /* INFO */, "Censor engine detached");
    g_engine = nullptr;
}
