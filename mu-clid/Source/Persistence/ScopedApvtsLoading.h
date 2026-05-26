#pragma once

// mu-core family-shared utility. Originally lived in mu-clid's private
// `PluginProcessor_Internal.h` under namespace `mu_pp` — lifted here when the
// pattern proved general: any plugin with an APVTS that does bulk-load paths
// (state restore, swap commit, batch parameter push) needs the same "atomic
// flag + RAII reset on exception" guard to keep UI / engine listeners from
// silently dropping per-param syncs during the push. See #391 for the original
// rationale.
//
// Usage:
//   std::atomic<bool> apvtsLoading { false };
//   ...
//   { mu_core::ScopedApvtsLoading guard(apvtsLoading); doBulkPush(); }
//
// Pair with an `isApvtsLoading()` public getter on the host class so UI
// listeners can early-out during the push.

#include <atomic>

namespace mu_core {

struct ScopedApvtsLoading
{
    std::atomic<bool>& flag;
    explicit ScopedApvtsLoading(std::atomic<bool>& f) noexcept : flag(f)
    {
        flag.store(true, std::memory_order_release);
    }
    ~ScopedApvtsLoading() noexcept
    {
        flag.store(false, std::memory_order_release);
    }
    ScopedApvtsLoading(const ScopedApvtsLoading&) = delete;
    ScopedApvtsLoading& operator=(const ScopedApvtsLoading&) = delete;
};

} // namespace mu_core
