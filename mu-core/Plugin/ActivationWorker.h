#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>

#include "License/OnlineActivation.h"   // OnlineActivationOutcome (decls only)

namespace mu_core
{
// Runs one online-activation call on a thread this object owns and joins, so the call (which captures the processor
// and blocks on the network) can never outlive the processor or the plugin module. The owner calls stop() from its
// destructor path before anything the call captures goes. Message thread only.
class ActivationWorker
{
public:
    using Fn   = std::function<OnlineActivationOutcome(const juce::String& licenseKey)>;
    using Done = std::function<void(const OnlineActivationOutcome&)>;

    ~ActivationWorker() { stop(); }

    // Starts `fn(key)` off the message thread; `done` then runs on the message thread with the outcome, and not at
    // all once stop() has run. One request at a time: false when `fn` is empty or one is already running.
    bool start(const Fn& fn, const juce::String& key, Done done)
    {
        if (! fn) return false;
        if (job != nullptr)
        {
            if (job->isThreadRunning()) return false;
            job.reset();   // a finished one: nothing to wait for
        }
        job = std::make_unique<Job>(fn, key, std::move(done), alive);
        job->startThread();
        return true;
    }

    // Joins the worker (bounded by the network timeout) and drops a reply already queued. Safe to call twice.
    void stop() noexcept
    {
        alive->store(false, std::memory_order_release);
        if (job != nullptr)
        {
            job->stopThread(kJoinMs);
            job.reset();
        }
    }

private:
    static constexpr int kJoinMs = 12000;   // longer than the activation request's own 8 s connection timeout

    struct Job : juce::Thread
    {
        Job(Fn f, juce::String k, Done d, std::shared_ptr<std::atomic<bool>> aliveFlag)
            : juce::Thread("mu activation"), fn(std::move(f)), key(std::move(k)), done(std::move(d)), alive(std::move(aliveFlag)) {}

        void run() override
        {
            const auto outcome = fn(key);   // blocks on the network here
            if (threadShouldExit()) return;
            // The flag is only lowered on the message thread (stop), where this runs too.
            juce::MessageManager::callAsync([alive = alive, done = done, outcome]
            {
                if (alive->load(std::memory_order_acquire) && done) done(outcome);
            });
        }

        Fn fn;
        juce::String key;
        Done done;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    std::unique_ptr<Job>               job;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

} // namespace mu_core
