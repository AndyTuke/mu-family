#pragma once

#include "Plugin/UpdateCheckPolicy.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <vector>

namespace mu_core::update_check
{

// The one fetch of latest.json per process, shared by every editor (and every plugin instance) in
// it through juce::SharedResourcePointer. The request runs on this thread, never on the message or
// audio thread: a plain HTTPS GET with no query string, cookie or identifier, a 3 s connection
// timeout, a 64 KB cap and no retry. Any failure is silent. The thread is joined when the last user
// goes, so it cannot outlive the plugin DLL.
class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker() : juce::Thread("mu update check") {}
    ~UpdateChecker() override { stopThread(4000); }

    // `onBody` runs once on the message thread with the raw JSON, or with an empty string if the
    // fetch failed. Callers guard their own lifetime (a WeakReference) inside the callback.
    void request(std::function<void(const juce::String&)> onBody)
    {
        {
            const juce::ScopedLock sl(lock);
            if (fetched)
            {
                const auto body = cachedBody;
                juce::MessageManager::callAsync([onBody, body] { onBody(body); });
                return;
            }
            waiting.push_back(std::move(onBody));
            if (running) return;   // the fetch in flight serves this waiter too
            running = true;
        }
        startThread();
    }

private:
    void run() override
    {
        const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs(3000);
        std::unique_ptr<juce::InputStream> stream(juce::URL(kUrl).createInputStream(opts));
        if (stream == nullptr || threadShouldExit()) { clearWaiting(); return; }

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock(data, 64 * 1024);   // capped read
        if (threadShouldExit()) { clearWaiting(); return; }
        const juce::String body = data.toString();

        std::vector<std::function<void(const juce::String&)>> toCall;
        {
            const juce::ScopedLock sl(lock);
            cachedBody = body;
            fetched    = true;
            running    = false;
            toCall.swap(waiting);
        }
        for (auto& cb : toCall)
            juce::MessageManager::callAsync([cb, body] { cb(body); });
    }

    // A failed fetch tells its waiters with an empty body and caches nothing, so a later request tries again.
    void clearWaiting()
    {
        std::vector<std::function<void(const juce::String&)>> toCall;
        {
            const juce::ScopedLock sl(lock);
            running = false;
            toCall.swap(waiting);
        }
        for (auto& cb : toCall)
            juce::MessageManager::callAsync([cb] { cb({}); });
    }

    juce::CriticalSection lock;
    bool fetched = false;
    bool running = false;   // a fetch has been started and has not yet delivered; guarded by `lock`
    juce::String cachedBody;
    std::vector<std::function<void(const juce::String&)>> waiting;
};

} // namespace mu_core::update_check
