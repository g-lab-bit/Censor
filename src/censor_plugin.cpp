/*
 * censor_plugin.cpp — Censor DLL entry points (v1.1 — Censor-x7d real pipeline).
 *
 * Replaces the Phase-3 stub.  censor_attach_engine now:
 *   1. Stores the engine handle.
 *   2. Hydrates the in-memory classifier from the SQLite DB (ce-f4i).
 *   3. Optionally runs process_page(0) as a warm start (behind the
 *      CENSOR_HAVE_RAPIDA_SHIM guard; on-demand lazy activation is ra-zmcgs).
 *
 * Process-global state follows the existing g_host/g_engine/g_poisoned pattern.
 * Heavy work runs on the BackgroundWorker thread; attach returns promptly.
 *
 * Threading invariants:
 *   ce-8eo  — g_host_mu guards host callback access
 *   ce-fml  — worker stop() called before shared state it references is destroyed
 *   Censor-owf — KNNClassifier uses internal shared_mutex for predict/add_example
 */

#include "censor_abi.h"
#include "censor_types.h"
#include "pipeline/page_pipeline.h"

#include "classifier/knn_classifier.h"
#include "classifier/background_worker.h"
#include "overlay/confidence_overlay.h"
#include "storage/censor_db.h"

#include <atomic>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

/* ---------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------*/

/* ce-8eo — g_host is read by censor_log/fire_fatal from ANY thread while
 * censor_init/censor_shutdown write it.  Readers hold the shared lock ACROSS
 * the callback invocation so shutdown's unique-lock zeroing waits for
 * in-flight callbacks to finish. */
static std::shared_mutex     g_host_mu;
static RapidaHostCallbacks   g_host      = {};
static RapidaVectorEngineHandle g_engine = nullptr;
static std::atomic<bool>     g_initialized{false};

/* Set atomically on first call to fire_fatal().  Once poisoned, all entry
 * points except censor_shutdown() become no-ops. */
static std::atomic<bool>     g_poisoned{false};

/* Pipeline singletons — constructed in censor_init, torn down in
 * censor_shutdown.  ce-fml: stop the worker BEFORE destroying what it
 * references. */
static std::shared_ptr<censor::KNNClassifier>     g_classifier;
static std::shared_ptr<censor::ConfidenceOverlay> g_overlay;
static std::unique_ptr<censor::BackgroundWorker>  g_worker;
static std::unique_ptr<censor::CensorDb>          g_db;

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

static void fire_fatal(const char* reason)
{
    bool expected = false;
    if (!g_poisoned.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
        return; /* already poisoned — don't double-fire */
    }

    {
        std::shared_lock<std::shared_mutex> lk(g_host_mu);  /* ce-8eo */
        if (g_host.on_censor_fatal) {
            g_host.on_censor_fatal(g_host.user_data, reason);
        }
        /* Belt-and-suspenders log (inline — cannot call censor_log, which
         * would attempt to acquire the shared lock we already hold). */
        if (g_host.log) {
            g_host.log(g_host.user_data, 5 /* CRITICAL */, "censor", reason);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Exported entry points
 * -------------------------------------------------------------------------*/

/* censor_abi_version cannot throw — ce-m21 wrapping intentionally omitted. */
uint32_t censor_abi_version(void)
{
    return CENSOR_ABI_VERSION;  /* 0x00010100 = v1.1.0 */
}

int32_t censor_init(const RapidaHostCallbacks* host)
{
    try {
        if (!host)                                return 1; /* null callbacks  */
        if (host->struct_version < 1)             return 2; /* unknown version */
        if (!host->log || !host->on_censor_fatal) return 3; /* required callbacks missing */

        {
            std::unique_lock<std::shared_mutex> lk(g_host_mu);  /* ce-8eo */
            g_host = *host;
        }
        g_poisoned.store(false, std::memory_order_release);
        g_engine = nullptr;

        /* Construct pipeline singletons. */
        g_classifier = std::make_shared<censor::KNNClassifier>();
        g_overlay    = std::make_shared<censor::ConfidenceOverlay>();
        g_worker     = std::make_unique<censor::BackgroundWorker>(
                            g_classifier, g_overlay);
        g_db         = std::make_unique<censor::CensorDb>();

        g_initialized.store(true, std::memory_order_release);

        censor_log(2 /* INFO */, "Censor initialized (v1.1 — Censor-x7d)");
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

        /* ce-fml: stop the worker before destroying anything it references. */
        if (g_worker) {
            g_worker->stop();
            g_worker.reset();
        }

        g_overlay.reset();
        g_classifier.reset();

        if (g_db) {
            g_db->close();
            g_db.reset();
        }

        g_engine = nullptr;

        {
            std::unique_lock<std::shared_mutex> lk(g_host_mu);  /* ce-8eo */
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
        if (!g_initialized.load(std::memory_order_acquire)) return 1;
        if (g_poisoned.load(std::memory_order_acquire))     return 0; /* no-op */

        g_engine = engine;
        censor_log(2 /* INFO */, "Censor engine attached");

        /* Hydrate classifier from DB (ce-f4i).
         * g_db may not have an open connection (no path provided yet) — that is
         * fine; hydrate_classifier is a no-op when db.is_open() is false. */
        if (g_db && g_classifier) {
            int loaded = censor::hydrate_classifier(*g_db, *g_classifier);
            if (loaded > 0) {
                censor_log(2 /* INFO */,
                    ("Censor: hydrated " + std::to_string(loaded)
                     + " examples from DB").c_str());
            }
        }

#if defined(CENSOR_HAVE_RAPIDA_SHIM)
        /* Retrieve the v2 vector_engine_api (null for v1.0 hosts). */
        const RapidaVectorEngineApi* api = nullptr;
        {
            std::shared_lock<std::shared_mutex> lk(g_host_mu);
            if (g_host.struct_version >= 2) {
                api = g_host.vector_engine_api;
            }
        }

        if (api && engine && g_classifier && g_overlay) {
            /* Snapshot the log sink for use in the worker lambda
             * (cannot hold g_host_mu inside the worker loop). */
            censor::pipeline::LogFn log_fn = nullptr;
            void*                   log_ud = nullptr;
            {
                std::shared_lock<std::shared_mutex> lk(g_host_mu);
                log_fn = g_host.log;
                log_ud = g_host.user_data;
            }

            /* Start the BackgroundWorker.  It stays alive until
             * censor_detach_engine() calls stop(). */
            if (g_worker) {
                g_worker->start();
            }

            /* Warm-start: run page 0 through the pipeline inline.
             * This exercises the full data path on attach and gives the
             * classifier its first prediction pass.
             *
             * PathStream lifetime: process_page finishes the page before
             * returning — safe to call directly here.
             *
             * For on-demand lazy page activation (the production path for
             * pages > 0) see censor_activate_cell — ra-zmcgs (next bead). */
            int32_t n_pages = api->page_count(engine);
            if (n_pages > 0) {
                censor::pipeline::process_page(
                    engine, api, 0,
                    *g_classifier, *g_overlay,
                    log_fn, log_ud);
            }
        } else {
            censor_log(1 /* DEBUG */,
                "Censor: no vector_engine_api from host — pipeline deferred");
        }
#else
        censor_log(1 /* DEBUG */,
            "Censor: built without Rapida shim — pipeline is a no-op stub");
#endif /* CENSOR_HAVE_RAPIDA_SHIM */

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
            /* ce-fml: stop the worker BEFORE releasing the engine handle so no
             * in-flight job can dereference it after this returns. */
            if (g_worker) {
                g_worker->stop();
            }

            censor_log(2 /* INFO */, "Censor engine detached");
            g_overlay->clear();
            g_engine = nullptr;
        }
    } catch (const std::exception& e) {
        fire_fatal(e.what());
    } catch (...) {
        fire_fatal("censor: unknown exception at ABI boundary");
    }
}
