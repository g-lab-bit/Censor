// censor_abi.h — Public C ABI for the Censor semantic sidecar DLL.
//
// This header defines the six exported entry points and the POD types they
// use. Rapida includes it when resolving Censor's exported symbols via
// GetProcAddress / dlsym. Censor includes it when implementing them.
//
// ABI discipline: only C types cross the binary boundary. No C++ classes,
// no STL, no exceptions. The DLL boundary is plain C or nothing.
//
// Censor ABI version: 1.0.0 — censor_abi_version() == 0x00010000.
// Compatibility rule: host accepts any DLL whose major version matches.

#ifndef CENSOR_ABI_H
#define CENSOR_ABI_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Export / import visibility
// ---------------------------------------------------------------------------

#ifdef _WIN32
#  ifdef CENSOR_BUILDING_DLL
#    define CENSOR_API __declspec(dllexport)
#  else
#    define CENSOR_API __declspec(dllimport)
#  endif
#else
#  define CENSOR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Return codes
// ---------------------------------------------------------------------------

#define CENSOR_OK  0   // Success — used by censor_init / censor_attach_engine

// ---------------------------------------------------------------------------
// Host callbacks (Rapida → Censor, passed at init)
// ---------------------------------------------------------------------------

// RapidaHostCallbacks — Rapida fills this struct and passes it to
// censor_init(). Censor routes all logging and fatal-error reporting
// through these function pointers. The struct must remain valid from
// censor_init() until censor_shutdown() returns.
typedef struct RapidaHostCallbacks {
    // ABI version of this struct itself. Must equal 1 for Censor ABI v1.x.
    uint32_t struct_version;

    // Log sink. Censor routes all log output through this function.
    //   level:    0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=CRITICAL
    //   category: UTF-8 C string, may be NULL (treat as "censor").
    //   message:  UTF-8 C string, NUL-terminated, never NULL.
    // May be called from any thread; Rapida's logger is thread-safe.
    void (*log)(void* user_data, int level, const char* category,
                const char* message);

    // Crash capture. Called by Censor when a worker thread encounters a
    // fatal error it cannot recover from. Rapida responds by:
    //   1. Writing a CRITICAL log entry with the reason string.
    //   2. Setting an atomic censor_poisoned flag — subsequent API calls
    //      become no-ops.
    //   3. Displaying a non-blocking banner ("Semantic features unavailable
    //      for this session").
    // Not called for normal (recoverable) errors; those return non-zero
    // codes from the entry point functions.
    void (*on_censor_fatal)(void* user_data, const char* reason);

    // Opaque user data forwarded unchanged to log() and on_censor_fatal().
    void* user_data;
} RapidaHostCallbacks;

// ---------------------------------------------------------------------------
// Engine handle
// ---------------------------------------------------------------------------

// Opaque handle to a Rapida vector engine session. Internally a
// rapida::engine::IVectorEngine* cast to void*. Censor must treat it as
// opaque and read vector data only through rapida_vector_engine_c.h.
typedef void* RapidaVectorEngineHandle;

// ---------------------------------------------------------------------------
// Exported entry points
// ---------------------------------------------------------------------------

// Returns the packed ABI version: (major << 16) | (minor << 8) | patch.
// Current: 1.0.0 → 0x00010000.
// Rapida calls this immediately after loading the DLL. If the major
// component does not match the host's expected major, the DLL is unloaded
// and Rapida runs without Censor.
CENSOR_API uint32_t censor_abi_version(void);

// Called once per Rapida process, after the DLL loads and the version check
// passes. host must remain valid until censor_shutdown() returns.
// Returns CENSOR_OK (0) on success; non-zero causes Rapida to unload the DLL
// and run without Censor for this process lifetime.
CENSOR_API int32_t censor_init(const RapidaHostCallbacks* host);

// Called once per Rapida process, during shutdown, after the last document
// closes. Must be idempotent — safe to call even if censor_init() failed.
CENSOR_API void censor_shutdown(void);

// Called when a document session begins. engine is valid until
// censor_detach_engine() returns. Censor must not retain the handle across
// detach calls. Returns CENSOR_OK on success; non-zero on failure.
CENSOR_API int32_t censor_attach_engine(RapidaVectorEngineHandle engine);

// Called when a document session ends. Censor must release any outstanding
// work referencing the engine before returning.
CENSOR_API void censor_detach_engine(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CENSOR_ABI_H
