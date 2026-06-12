/*
 * censor_plugin.cpp — Censor DLL entry points (Phase 3 stub).
 *
 * This is the stub implementation that proves the Rapida ↔ Censor ABI
 * contract works end-to-end before any real classifier work lands.
 *
 * Key behaviour implemented here:
 *
 *   Poison banner mechanism (ce-ykc)
 *   ─────────────────────────────────
 *   When Censor catches a fatal internal error it cannot recover from, it:
 *     1. Atomically sets g_poisoned (idempotent — first caller wins).
 *     2. Calls host_callbacks.on_censor_fatal(user_data, reason).
 *        Rapida's handler posts a non-blocking UI banner and stops
 *        calling into Censor for the rest of the process lifetime.
 *     3. Logs the reason at CRITICAL via the log sink.
 *
 *   Phase 3 stub fires this path during censor_attach_engine to let the
 *   Rapida-side CensorLoader verify the callback path before real classifier
 *   work lands (SPEC-censor-integration.md §Phase 3 DoD).
 */

#include "censor_abi.h"
#include "censor_types.h"

#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <shared_mutex>

/* ---------------------------------------------------------------------------
 * Module state
 *
 * All state is process-scoped.  censor_init / censor_shutdown are called once
 * per process.  censor_attach_engine / censor_detach_engine once per document.
 * -------------------------------------------------------------------------*/

/* ce-8eo — g_host is read by censor_log/fire_fatal from ANY thread (the fatal
 * path is documented thread-safe) while censor_init/censor_shutdown write it.
 * Readers hold the shared lock ACROSS the callback invocation, so shutdown's
 * unique-lock zeroing waits for in-flight callbacks to finish — no torn reads,
 * no null function-pointer calls, no callback executing after shutdown returns.
 * (Host callbacks must not re-enter Censor; the ABI doc already forbids it.) */
static std::shared_mutex     g_host_mu;
static RapidaHostCallbacks   g_host      = {};
static RapidaVectorEngineHandle g_engine = nullptr;
static std::atomic<bool>     g_initialized{false};

/* Set atomically on first call to fire_fatal().  Once poisoned, all entry
 * points except censor_shutdown() become no-ops.  This mirrors the state
 * Rapida tracks on its side after receiving on_censor_fatal. */
static std::atomic<bool>     g_poisoned{false};

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

static void censor_log(int level, const char* message)
{
    std::shared_lock<std::shared_mutex> lk(g_host_mu);  /* ce-8eo */
    if (g_host.log) {
        g_host.log(g_host.user_data, level, "censor", message);
    }
}

/* Fire the poison banner mechanism.
 *
 * Atomic and idempotent: the first call wins, subsequent calls are no-ops.
 * Safe to call from any thread.
 *
 * After this returns:
 *   - g_poisoned == true
 *   - Rapida's on_censor_fatal handler has been invoked with reason
 *   - A CRITICAL log entry has been written via the log sink
 */
static void fire_fatal(const char* reason)
{
    bool expected = false;
    if (!g_poisoned.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
        /* Already poisoned — don't double-fire the callback. */
        return;
    }

    /* Notify the host.  Rapida's handler will:
     *   - Log CRITICAL
     *   - Set its own censor_poisoned flag
     *   - Post the non-blocking UI banner
     * ce-8eo: shared lock held across the invocation so a concurrent
     * censor_shutdown cannot zero g_host mid-call. */
    {
        std::shared_lock<std::shared_mutex> lk(g_host_mu);
        if (g_host.on_censor_fatal) {
            g_host.on_censor_fatal(g_host.user_data, reason);
        }
        /* Belt-and-suspenders: log at CRITICAL via the log sink so the event
         * appears in Rapida's log file even if on_censor_fatal is a no-op stub.
         * (Inline rather than censor_log() — shared_mutex re-acquisition on the
         * same thread is not guaranteed safe while a writer waits.) */
        if (g_host.log) {
            g_host.log(g_host.user_data, 5 /* CRITICAL */, "censor", reason);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Exported entry points
 * -------------------------------------------------------------------------*/

/* censor_abi_version returns a compile-time constant and cannot throw —
 * ce-m21 exception wrapping is intentionally omitted here. */
uint32_t censor_abi_version(void)
{
    return CENSOR_ABI_VERSION;
}

int32_t censor_init(const RapidaHostCallbacks* host)
{
    try {
        if (!host) {
            return 1; /* null callbacks */
        }
        if (host->struct_version != 1) {
            return 2; /* unknown struct version */
        }
        if (!host->log || !host->on_censor_fatal) {
            return 3; /* required callbacks missing */
        }

        {
            std::unique_lock<std::shared_mutex> lk(g_host_mu);  /* ce-8eo */
            g_host = *host;
        }
        g_poisoned.store(false, std::memory_order_release);
        g_engine      = nullptr;
        g_initialized.store(true, std::memory_order_release);

        censor_log(2 /* INFO */, "Censor initialized (stub v1.0 — ce-ykc)");
        return 0;
    } catch (const std::exception& e) {
        fire_fatal(e.what());
        return -1;
    } catch (...) {
        fire_fatal("censor: unknown exception at ABI boundary");
        return -1;
    }
}

void censor_shutdown(void)
{
    try {
        if (g_initialized.exchange(false, std::memory_order_acq_rel)) {
            censor_log(2 /* INFO */, "Censor shutting down");
        }
        g_engine  = nullptr;
        /* ce-8eo — zero the host BEFORE resetting the poison flag, under the
         * unique lock (which waits for any in-flight callback holding the shared
         * lock). A fire_fatal racing past this point snapshots an empty host and
         * calls nothing; the poison reset below then safely re-arms for re-init. */
        {
            std::unique_lock<std::shared_mutex> lk(g_host_mu);
            g_host = {};
        }
        g_poisoned.store(false, std::memory_order_release);
    } catch (const std::exception& e) {
        fire_fatal(e.what());
    } catch (...) {
        fire_fatal("censor: unknown exception at ABI boundary");
    }
}

int32_t censor_attach_engine(RapidaVectorEngineHandle engine)
{
    try {
        if (!g_initialized.load(std::memory_order_acquire)) {
            return 1; /* not initialized */
        }
        if (g_poisoned.load(std::memory_order_acquire)) {
            return 0; /* already poisoned — no-op, not an error */
        }

        g_engine = engine;
        censor_log(2 /* INFO */, "Censor engine attached (stub)");

        /* -----------------------------------------------------------------------
         * Phase 3 stub: fire the on_censor_fatal callback path.
         *
         * This exercises the end-to-end poison banner mechanism so the
         * Rapida-side CensorLoader can verify it before any real classifier work
         * lands.  Per SPEC-censor-integration.md §Phase 3 DoD:
         *
         *   "Rapida runs to completion when a stub Censor DLL calls
         *    on_censor_fatal during censor_attach_engine — poison banner
         *    appears, rest of session works."
         *
         * After fire_fatal() returns:
         *   - g_poisoned == true
         *   - Rapida has received the on_censor_fatal callback
         *   - Rapida shows its non-blocking banner
         *   - Rapida's guard flag prevents further calls into Censor
         *
         * We return 0 (not an error code) because attach succeeded —
         * the fatal is signalled asynchronously via the callback, not via
         * the return value.  Rapida continues the session in degraded mode.
         * ---------------------------------------------------------------------- */
        fire_fatal(
            "Censor stub (Phase 3): on_censor_fatal callback path verification. "
            "Classification is unavailable in this stub build. "
            "Restart Rapida with a full Censor DLL to enable semantic features."
        );

        return 0;
    } catch (const std::exception& e) {
        fire_fatal(e.what());
        return -1;
    } catch (...) {
        fire_fatal("censor: unknown exception at ABI boundary");
        return -1;
    }
}

void censor_detach_engine(void)
{
    try {
        if (g_engine) {
            censor_log(2 /* INFO */, "Censor engine detached");
            g_engine = nullptr;
        }
    } catch (const std::exception& e) {
        fire_fatal(e.what());
    } catch (...) {
        fire_fatal("censor: unknown exception at ABI boundary");
    }
}
