#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"

namespace mu_core
{
// Maps the shared mixer / global-FX APVTS parameters onto the mixer engine and the FX chain: `ch{N}_*`, `bus{B}_*`,
// `ret_*`, `mstr_*`, `mst_ins*`, `eff_*`, `eff2*`, `dly_*`, `rev_*`, `echo_*` (the set is declared by
// mu_mixfx::addGlobalFxParams plus the product's strip). Has no product knowledge. ProcessorBase owns one and forwards to it.
// Threads: start / syncAll / stop on the message thread; onChanged and syncGlobal may run on the audio thread (host
// automation), so they allocate nothing and every engine write is a relaxed atomic store or a bounded slot call.
class FxParamSync
{
public:
    FxParamSync(juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& apvts, MixerEngine& mixer,
                FXChain& fx, std::atomic<bool>& muteChanged);
    ~FxParamSync() { stop(); }

    // True for the ids this class syncs: the channel strips (`ch...`) and the global FX / master ids.
    static bool isFxParamId(const juce::String& id);

    // Listen to every such id, then seed the engines from the current values. Once per processor.
    void start();
    // Re-seed the engines from the current values (JUCE fires no change for a value that is already set).
    void syncAll();
    // Maps one id and value onto the engines; unrecognised ids no-op.
    void syncGlobal(const juce::String& id, float v);
    // Stops listening. Safe to call twice.
    void stop();

private:
    // The listener's one entry (any thread, audio thread included): the engine sync, plus a flag for the message
    // thread when a strip mute moved. No allocation.
    void onChanged(const juce::String& id, float v);

    // syncGlobal dispatch helpers: one per id family, so the entry point is a short prefix router. syncMaster returns
    // true when it handled the id. The channel / return helpers take a raw `const char*` suffix pointing into the
    // caller's id buffer (not a juce::String substring) so the audio-thread automation path allocates nothing.
    void syncChannelStripParam(int channel, const char* param, float v);
    void syncReturnStripParam (int retIndex,  const char* rest,  float v);
    void syncBusParam         (const juce::String& id, float v);
    bool syncMasterParam      (const juce::String& id, float v);
    void syncFxSlotParam      (const juce::String& id, float v);

    struct Listener : juce::AudioProcessorValueTreeState::Listener
    {
        explicit Listener(FxParamSync& o) : owner(o) {}
        void parameterChanged(const juce::String& id, float v) override { owner.onChanged(id, v); }
        FxParamSync& owner;
    };

    juce::AudioProcessor&               proc;
    juce::AudioProcessorValueTreeState& apvts;
    MixerEngine&                        mixerEngine;
    FXChain&                            fxChain;
    std::atomic<bool>&                  muteChanged;   // a strip mute moved: the owner's poll tells the live state
    std::unique_ptr<Listener>           fxParamListener;
};

} // namespace mu_core
