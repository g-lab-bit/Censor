/*
 * censor_abi.h — Public C ABI for the Censor sidecar DLL.
 *
 * Rapida loads censor.dll at startup, resolves these five entry points via
 * GetProcAddress/dlsym, and drives the lifecycle described in
 * SPEC-censor-integration.md.
 *
 * Rules:
 *  - All symbols here use C linkage.  No C++ types cross the DLL boundary.
 *  - Censor must never include anything from Rapida's src/ tree.
 *  - Engine interaction goes through rapida_vector_engine_c.h only.
 */

#ifndef CENSOR_ABI_H
#define CENSOR_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Version
 * -------------------------------------------------------------------------*/

/* ABI version packed as (major << 16) | (minor << 8) | patch.
 * Rapida's loader accepts any DLL whose major version matches its own.
 * Minor/patch differences are tolerated under the "add, never remove, never
 * reorder" discipline.  Initial version: 1.0.0. */
#define CENSOR_ABI_VERSION ((uint32_t)0x00010000u)

/* ---------------------------------------------------------------------------
 * Host callback table
 * -------------------------------------------------------------------------*/

/* Rapida fills this struct and passes a pointer to censor_init().
 * The pointer and all function pointers inside must remain valid until
 * censor_shutdown() returns.
 *
 * struct_version must equal 1 for ABI v1.x. */
typedef struct RapidaHostCallbacks {
    uint32_t struct_version;

    /* Log sink.  Censor routes all internal log output through this function.
     * Level: 0=TRACE  1=DEBUG  2=INFO  3=WARN  4=ERROR  5=CRITICAL
     * category: UTF-8 C string, may be NULL (treat as "censor")
     * message:  UTF-8 C string, NUL-terminated, never NULL */
    void (*log)(void* user_data, int level,
                const char* category, const char* message);

    /* Crash capture.  Called by Censor from a worker thread's top-level
     * catch block when the thread cannot recover.
     *
     * Rapida's handler must:
     *   1. Write a CRITICAL log entry with reason.
     *   2. Set an atomic censor_poisoned flag — all further Censor calls
     *      become no-ops on the Rapida side.
     *   3. Post a non-blocking UI banner:
     *      "Semantic features are unavailable for this session.
     *       Restart Rapida to retry."
     *   4. NOT attempt to restart Censor or reload the DLL.
     *   5. NOT crash Rapida — the rest of the session continues.
     *
     * reason: UTF-8 C string describing why the fatal occurred, non-NULL.
     *
     * Thread safety: Rapida's implementation must be callable from any thread.
     * Idempotency: Censor calls this at most once per process lifetime. */
    void (*on_censor_fatal)(void* user_data, const char* reason);

    /* Opaque pointer passed unchanged to log() and on_censor_fatal().
     * Rapida uses it to carry its logger instance through the C boundary. */
    void* user_data;
} RapidaHostCallbacks;

/* ---------------------------------------------------------------------------
 * Engine handle
 * -------------------------------------------------------------------------*/

/* Opaque handle Rapida passes to censor_attach_engine().
 * Censor must treat this as completely opaque — all interaction goes through
 * the rapida_vector_engine_c.h shim functions.  Never cast to a C++ type. */
typedef void* RapidaVectorEngineHandle;

/* ---------------------------------------------------------------------------
 * Symbol visibility
 * -------------------------------------------------------------------------*/

#if defined(_WIN32)
#  define CENSOR_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define CENSOR_EXPORT __attribute__((visibility("default")))
#else
#  define CENSOR_EXPORT
#endif

/* ---------------------------------------------------------------------------
 * Exported entry points
 *
 * NOTE: The spec describes "six total exported symbols" (SPEC-censor-integration.md
 * §"C ABI surface").  The current spec text explicitly defines five.  This header
 * implements the five that are spec-defined.  A sixth will be added and the ABI
 * version bumped when the spec is updated.
 * -------------------------------------------------------------------------*/

/* Returns CENSOR_ABI_VERSION.
 * Called immediately after LoadLibrary/dlopen, before any other entry point. */
CENSOR_EXPORT uint32_t censor_abi_version(void);

/* Called once per process after the ABI version check passes.
 * host must point to a valid RapidaHostCallbacks with struct_version == 1.
 * Returns 0 on success, non-zero on error.  On non-zero return, Rapida
 * unloads the DLL and continues without Censor. */
CENSOR_EXPORT int32_t censor_init(const RapidaHostCallbacks* host);

/* Called once per process during Rapida shutdown, after the last document
 * session is closed.  Must be idempotent. */
CENSOR_EXPORT void censor_shutdown(void);

/* Called when a document session begins.  engine is valid until
 * censor_detach_engine() returns; Censor must not retain it across that call.
 * Returns 0 on success.
 *
 * Phase 3 stub behaviour: this implementation calls on_censor_fatal to
 * exercise the end-to-end poison banner path before real classifier work
 * lands.  See SPEC-censor-integration.md §"Phase 3 deliverables" DoD item:
 * "stub Censor DLL calls on_censor_fatal during censor_attach_engine —
 *  poison banner appears, rest of session works". */
CENSOR_EXPORT int32_t censor_attach_engine(RapidaVectorEngineHandle engine);

/* Called when a document session ends.  Censor must complete any outstanding
 * work referencing engine before returning. */
CENSOR_EXPORT void censor_detach_engine(void);

#ifdef __cplusplus
}
#endif

#endif /* CENSOR_ABI_H */
