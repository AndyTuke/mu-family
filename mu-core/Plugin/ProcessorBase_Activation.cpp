// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Online activation worker: one call off the message thread, owned and joined by the processor.
#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Sequencer/ClipClipboard.h"         // the process-wide clip clipboard
#include "Persistence/PresetFiles.h"         // mu_pp full-preset files
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare
#include <map>
#include <memory>
#include <mutex>

namespace
{
// One online-activation call off the message thread. Owned and joined by the processor, so `fn` may capture it.
struct ActivationWorker : juce::Thread
{
    using Done = std::function<void(const mu_core::OnlineActivationOutcome&)>;

    ActivationWorker(std::function<mu_core::OnlineActivationOutcome(const juce::String&)> f, juce::String k, Done d,
                     std::shared_ptr<std::atomic<bool>> aliveFlag)
        : juce::Thread("mu activation"), fn(std::move(f)), key(std::move(k)), done(std::move(d)), alive(std::move(aliveFlag)) {}

    void run() override
    {
        const auto outcome = fn(key);   // blocks on the network here
        if (threadShouldExit()) return;
        // The flag is only lowered on the message thread (prepareForDestruction), where this runs too.
        juce::MessageManager::callAsync([alive = alive, done = done, outcome]
        {
            if (alive->load(std::memory_order_acquire) && done) done(outcome);
        });
    }

    std::function<mu_core::OnlineActivationOutcome(const juce::String&)> fn;
    juce::String key;
    Done done;
    std::shared_ptr<std::atomic<bool>> alive;
};

constexpr int kActivationJoinMs = 12000;   // longer than the activation request's own 8 s connection timeout
}

bool ProcessorBase::activateOnlineAsync(const juce::String& key,
                                        std::function<void(const mu_core::OnlineActivationOutcome&)> done)
{
    if (! activateOnlineFn) return false;
    if (activationWorker != nullptr)
    {
        if (activationWorker->isThreadRunning()) return false;
        activationWorker.reset();   // a finished one: nothing to wait for
    }
    activationWorker = std::make_unique<ActivationWorker>(activateOnlineFn, key, std::move(done), activationAlive);
    activationWorker->startThread();
    return true;
}

void ProcessorBase::stopActivationWorker() noexcept
{
    activationAlive->store(false, std::memory_order_release);   // a reply already queued is dropped
    if (activationWorker != nullptr)
    {
        activationWorker->stopThread(kActivationJoinMs);
        activationWorker.reset();
    }
}
