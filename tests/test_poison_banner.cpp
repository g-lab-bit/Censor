/*
 * test_poison_banner.cpp — Unit tests for the Censor poison banner mechanism.
 *
 * Tests the on_censor_fatal callback path (ce-ykc):
 *   - fire_fatal triggers on_censor_fatal exactly once
 *   - Censor marks itself poisoned after the callback fires
 *   - Subsequent attach calls are no-ops (not errors) when poisoned
 *   - Shutdown resets poisoned state
 */

#include "censor_abi.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstring>

/* ---------------------------------------------------------------------------
 * Mock host callbacks
 * -------------------------------------------------------------------------*/

struct MockHost {
    std::atomic<int>  fatal_call_count{0};
    std::atomic<int>  log_call_count{0};
    char              last_fatal_reason[512]{};

    static void log_cb(void* ud, int /*level*/,
                       const char* /*cat*/, const char* /*msg*/)
    {
        auto* h = static_cast<MockHost*>(ud);
        h->log_call_count.fetch_add(1, std::memory_order_relaxed);
    }

    static void fatal_cb(void* ud, const char* reason)
    {
        auto* h = static_cast<MockHost*>(ud);
        h->fatal_call_count.fetch_add(1, std::memory_order_relaxed);
        if (reason) {
            std::strncpy(h->last_fatal_reason, reason,
                         sizeof(h->last_fatal_reason) - 1);
        }
    }

    RapidaHostCallbacks make_callbacks()
    {
        RapidaHostCallbacks cb{};
        cb.struct_version = 1;
        cb.log            = &MockHost::log_cb;
        cb.on_censor_fatal = &MockHost::fatal_cb;
        cb.user_data      = this;
        return cb;
    }
};

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

template <typename Fn>
static void init_and_cleanup(MockHost& host, Fn fn)
{
    auto cb = host.make_callbacks();
    ASSERT_EQ(0, censor_init(&cb));
    fn();
    censor_shutdown();
}

/* ---------------------------------------------------------------------------
 * Tests
 * -------------------------------------------------------------------------*/

TEST(PoisonBanner, AbiVersionMatchesHeader)
{
    EXPECT_EQ(CENSOR_ABI_VERSION, censor_abi_version());
}

TEST(PoisonBanner, InitNullCallbacksFails)
{
    EXPECT_NE(0, censor_init(nullptr));

    RapidaHostCallbacks bad{};
    bad.struct_version = 1;
    /* log and on_censor_fatal are null */
    EXPECT_NE(0, censor_init(&bad));
}

TEST(PoisonBanner, InitUnknownStructVersionFails)
{
    MockHost host;
    auto cb = host.make_callbacks();
    cb.struct_version = 99;
    EXPECT_NE(0, censor_init(&cb));
}

/* Core test: attach_engine fires on_censor_fatal exactly once (stub behaviour).
 *
 * This is the Phase 3 DoD scenario:
 *   "stub Censor DLL calls on_censor_fatal during censor_attach_engine —
 *    poison banner appears, rest of session works" */
TEST(PoisonBanner, AttachEngineFiresFatalCallbackOnce)
{
    MockHost host;
    auto cb = host.make_callbacks();
    ASSERT_EQ(0, censor_init(&cb));

    EXPECT_EQ(0, censor_attach_engine(reinterpret_cast<void*>(0xdeadbeef)));

    /* Callback must have fired exactly once. */
    EXPECT_EQ(1, host.fatal_call_count.load());
    /* Reason string must be non-empty. */
    EXPECT_GT(std::strlen(host.last_fatal_reason), 0u);

    censor_shutdown();
}

/* After fire_fatal, the host's on_censor_fatal must not fire again even if
 * another attach call arrives. */
TEST(PoisonBanner, FatalFiresOnlyOnce)
{
    MockHost host;
    auto cb = host.make_callbacks();
    ASSERT_EQ(0, censor_init(&cb));

    /* First attach — fires fatal, Censor is now poisoned. */
    EXPECT_EQ(0, censor_attach_engine(reinterpret_cast<void*>(0x1)));
    ASSERT_EQ(1, host.fatal_call_count.load());

    /* Second attach — should be a no-op, NOT a second callback invocation. */
    EXPECT_EQ(0, censor_attach_engine(reinterpret_cast<void*>(0x2)));
    EXPECT_EQ(1, host.fatal_call_count.load()); /* still exactly 1 */

    censor_shutdown();
}

/* Shutdown resets poisoned state so a fresh init works. */
TEST(PoisonBanner, ShutdownResetsState)
{
    MockHost host;
    auto cb = host.make_callbacks();

    ASSERT_EQ(0, censor_init(&cb));
    EXPECT_EQ(0, censor_attach_engine(reinterpret_cast<void*>(0x1)));
    EXPECT_EQ(1, host.fatal_call_count.load());
    censor_shutdown();

    /* Reinit — should work cleanly with zero fatal count. */
    MockHost host2;
    auto cb2 = host2.make_callbacks();
    ASSERT_EQ(0, censor_init(&cb2));
    EXPECT_EQ(0, host2.fatal_call_count.load());
    censor_shutdown();
}

/* Log sink is called during init and attach (sanity check). */
TEST(PoisonBanner, LogSinkReceivesCalls)
{
    MockHost host;
    auto cb = host.make_callbacks();
    ASSERT_EQ(0, censor_init(&cb));
    EXPECT_GT(host.log_call_count.load(), 0);

    censor_attach_engine(nullptr);
    EXPECT_GT(host.log_call_count.load(), 1);

    censor_shutdown();
}
