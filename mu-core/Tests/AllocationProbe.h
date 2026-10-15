#pragma once

// AllocationProbe: counts heap allocations and frees made by the calling thread while it is armed, so a
// test can assert that an audio-thread region (processBlock, a layer render) allocates and frees nothing.
//
// TEST-ONLY. Never include this from plugin code and never link it into a plugin or standalone: it
// replaces the global operator new / delete of the executable it is built into.
//
// Use: exactly one translation unit per test executable defines MU_ALLOCATION_PROBE_DEFINE_OPERATORS before
// including this header (that TU carries the replaced operators); every other TU just includes it.
//   mu_alloc::Armed region;            // counts from here
//   ... call under test ...
//   region.stop();  region.allocs(), region.frees()
// mu_alloc::Paused lifts the count for a scope (a message-thread drain the test runs inside the region).
//
// Only the arming thread is counted (thread_local), so a worker or message thread never pollutes a count.
// operator new / delete are counted directly; on the MSVC debug CRT a hook also counts plain malloc / free
// (JUCE's HeapBlock and AudioBuffer use those), skipping the calls the operators already counted.
// A backtrace of the first offender can be captured (captureBudget) so a finding names its call site.

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace mu_alloc
{
    struct ThreadState
    {
        bool armed = false;      // counting on this thread
        bool inOperator = false; // inside one of our operators: the CRT hook must not double count
        bool capturing = false;  // building a backtrace: its own allocations are not counted
        bool countMalloc = false; // also count plain malloc / free (debug CRT); set by Armed, off for raw arming
        int  allocs = 0;
        int  frees = 0;
        const char* firstAllocKind = nullptr;   // "new" or "malloc"
        juce::String firstAllocStack, firstFreeStack;
    };

    inline thread_local ThreadState tls;

    // How many backtraces (first alloc / first free of a region) may still be built; they are slow.
    inline int captureBudget = 0;

    inline void captureInto(juce::String& dest)
    {
        if (captureBudget <= 0 || dest.isNotEmpty()) return;
        --captureBudget;
        tls.capturing = true;
        dest = juce::SystemStats::getStackBacktrace();
        tls.capturing = false;
    }

    inline void noteAlloc(const char* kind) noexcept
    {
        if (! tls.armed || tls.capturing) return;
        if (tls.allocs++ == 0) { tls.firstAllocKind = kind; captureInto(tls.firstAllocStack); }
    }

    inline void noteFree() noexcept
    {
        if (! tls.armed || tls.capturing) return;
        if (tls.frees++ == 0) captureInto(tls.firstFreeStack);
    }

    // Counts the calling thread's allocations and frees from construction to stop() (or destruction).
    class Armed
    {
    public:
        Armed()
        {
            tls.allocs = 0; tls.frees = 0; tls.firstAllocKind = nullptr;
            tls.firstAllocStack = {}; tls.firstFreeStack = {};
            tls.armed = true;
            tls.countMalloc = true;
        }
        ~Armed() { stop(); }
        Armed(const Armed&) = delete;
        Armed& operator=(const Armed&) = delete;

        void stop() noexcept { tls.armed = false; tls.countMalloc = false; }
        int allocs() const noexcept { return tls.allocs; }
        int frees() const noexcept { return tls.frees; }
        const char* firstAllocKind() const noexcept { return tls.firstAllocKind; }
        juce::String firstAllocStack() const { return tls.firstAllocStack; }
        juce::String firstFreeStack() const { return tls.firstFreeStack; }
    };

    // Suspends counting for a scope, then restores the previous state.
    class Paused
    {
    public:
        Paused() noexcept : was(tls.armed) { tls.armed = false; }
        ~Paused() { tls.armed = was; }
        Paused(const Paused&) = delete;
        Paused& operator=(const Paused&) = delete;
    private:
        bool was;
    };
}

#ifdef MU_ALLOCATION_PROBE_DEFINE_OPERATORS

#if defined(_MSC_VER) && defined(_DEBUG)
 #include <crtdbg.h>
#endif

#if defined(_WIN32) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
 #define MU_ALLOC_SKIP_DEBUG_PROXIES 1
 extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long, unsigned long, void**, unsigned long*);
#endif

namespace mu_alloc::detail
{
    struct OpScope
    {
        OpScope() noexcept : prev(tls.inOperator) { tls.inOperator = true; }
        ~OpScope() { tls.inOperator = prev; }
        bool prev;
    };

   #ifdef MU_ALLOC_SKIP_DEBUG_PROXIES
    // The MSVC debug STL gives every std::string / vector / map a heap "container proxy" (iterator
    // debugging). It is a debug-build artefact, absent from a release build, and would bury real findings,
    // so those allocations and their frees are recognised by their call chain and left out of the count.
    // The chains are learned at start-up by constructing and destroying a vector, so no STL internals are
    // hard-coded here; skipped proxies are tallied in ignoredDebugProxies.
    constexpr int kChainDepth = 8, kChainFirst = 3, kAllocChain = 4, kFreeChain = 3;
    struct ProxyChains
    {
        void* alloc[kAllocChain] {};
        void* release[kFreeChain] {};
        int   calibrating = 0;        // 1: waiting for the proxy allocation, 2: waiting for its free
        void* calibratedPtr = nullptr;
        bool  known = false;
    };
    inline ProxyChains proxy;
    inline thread_local int ignoredDebugProxies = 0;

    inline bool proxyCalibrating() noexcept { return proxy.calibrating != 0; }

    // Frames from inside grab: [0] grab, [1] allocate / release, [2] the operator, then the STL chain.
    inline bool grab(void** f) noexcept
    {
        return RtlCaptureStackBackTrace(0, kChainDepth, f, nullptr) >= kChainFirst + kAllocChain;
    }

    inline bool isProxyAlloc(std::size_t n, void* result) noexcept
    {
        if (n != 16) return false;
        void* f[kChainDepth] {};
        if (! grab(f)) return false;
        if (proxy.calibrating == 1)
        {
            for (int i = 0; i < kAllocChain; ++i) proxy.alloc[i] = f[kChainFirst + i];
            proxy.calibratedPtr = result; proxy.calibrating = 2;
            return true;
        }
        if (! proxy.known) return false;
        for (int i = 0; i < kAllocChain; ++i) if (f[kChainFirst + i] != proxy.alloc[i]) return false;
        ++ignoredDebugProxies;
        return true;
    }

    inline bool isProxyFree(void* p) noexcept
    {
        void* f[kChainDepth] {};
        if (! grab(f)) return false;
        if (proxy.calibrating == 2 && p == proxy.calibratedPtr)
        {
            for (int i = 0; i < kFreeChain; ++i) proxy.release[i] = f[kChainFirst + i];
            proxy.calibrating = 0; proxy.known = true;
            return true;
        }
        if (! proxy.known) return false;
        for (int i = 0; i < kFreeChain; ++i) if (f[kChainFirst + i] != proxy.release[i]) return false;
        return true;
    }

    inline const int calibrated = []
    {
        proxy.calibrating = 1;
        { std::vector<int> probe; }
        proxy.calibrating = 0;
        return proxy.known ? 1 : 0;
    }();
   #else
    inline bool proxyCalibrating() noexcept { return false; }
    inline bool isProxyAlloc(std::size_t, void*) noexcept { return false; }
    inline bool isProxyFree(void*) noexcept { return false; }
   #endif

    inline void* allocate(std::size_t n) noexcept
    {
        OpScope s;
        void* p = std::malloc(n == 0 ? 1 : n);
        if ((tls.armed || proxyCalibrating()) && ! isProxyAlloc(n, p)) noteAlloc("new");
        return p;
    }

    inline void release(void* p) noexcept
    {
        if (p == nullptr) return;
        OpScope s;
        if ((tls.armed || proxyCalibrating()) && ! isProxyFree(p)) noteFree();
        std::free(p);
    }

    inline void* allocateAligned(std::size_t n, std::size_t align) noexcept
    {
        OpScope s;
        noteAlloc("new");
       #if defined(_WIN32)
        return _aligned_malloc(n == 0 ? 1 : n, align);
       #else
        const std::size_t size = ((n == 0 ? 1 : n) + align - 1) / align * align;
        return std::aligned_alloc(align, size);
       #endif
    }

    inline void releaseAligned(void* p) noexcept
    {
        if (p == nullptr) return;
        OpScope s;
        noteFree();
       #if defined(_WIN32)
        _aligned_free(p);
       #else
        std::free(p);
       #endif
    }

   #if defined(_MSC_VER) && defined(_DEBUG)
    // Debug CRT hook: counts malloc / realloc / free made outside our operators (HeapBlock, AudioBuffer).
    inline int __cdecl crtHook(int what, void* userData, std::size_t, int blockType, long, const unsigned char*, int)
    {
        if (! tls.armed || ! tls.countMalloc || tls.inOperator || tls.capturing || blockType == _CRT_BLOCK) return 1;
        if (what == _HOOK_ALLOC) noteAlloc("malloc");
        else if (what == _HOOK_REALLOC) { noteAlloc("malloc"); if (userData != nullptr) noteFree(); }
        else if (what == _HOOK_FREE && userData != nullptr) noteFree();
        return 1;
    }
    inline const int installCrtHook = (_CrtSetAllocHook(crtHook), 0);
   #endif
}

void* operator new(std::size_t n)                       { if (void* p = mu_alloc::detail::allocate(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n)                     { if (void* p = mu_alloc::detail::allocate(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept   { return mu_alloc::detail::allocate(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return mu_alloc::detail::allocate(n); }
void* operator new(std::size_t n, std::align_val_t a)   { if (void* p = mu_alloc::detail::allocateAligned(n, (std::size_t) a)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = mu_alloc::detail::allocateAligned(n, (std::size_t) a)) return p; throw std::bad_alloc(); }

void operator delete(void* p) noexcept                                  { mu_alloc::detail::release(p); }
void operator delete[](void* p) noexcept                                { mu_alloc::detail::release(p); }
void operator delete(void* p, std::size_t) noexcept                     { mu_alloc::detail::release(p); }
void operator delete[](void* p, std::size_t) noexcept                   { mu_alloc::detail::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept           { mu_alloc::detail::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept         { mu_alloc::detail::release(p); }
void operator delete(void* p, std::align_val_t) noexcept                { mu_alloc::detail::releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept              { mu_alloc::detail::releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept   { mu_alloc::detail::releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { mu_alloc::detail::releaseAligned(p); }

#endif   // MU_ALLOCATION_PROBE_DEFINE_OPERATORS
