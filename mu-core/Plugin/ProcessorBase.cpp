#include "Plugin/ProcessorBase.h"

ProcessorBase::ProcessorBase(const BusesProperties& props,
                             juce::AudioProcessorValueTreeState::ParameterLayout layout,
                             const juce::Identifier& stateTreeType)
    : juce::AudioProcessor(props),
      apvts(*this, nullptr, stateTreeType, std::move(layout))
{}

bool ProcessorBase::scanMidiProgramChanges(const juce::MidiBuffer& midi)
{
    const uint8_t chMask           = midiPresetMap.getChannelMask();
    const bool    fullPresetEnabled = midiFullPresetMap.isEnabled();
    if (chMask == 0 && ! fullPresetEnabled) return false;

    bool needPC = false;
    for (const auto& msgRef : midi)
    {
        const auto& m = msgRef.getMessage();
        if (! m.isProgramChange()) continue;
        const int ch = m.getChannel();      // 1-based, 1..16

        int  slot   = -1;
        bool isFull = false;
        if (ch >= 1 && ch <= 8)
        {
            if (! (chMask & (1 << (ch - 1)))) continue;
            slot = ch - 1;
        }
        else if (ch == MidiFullPresetMap::Channel)
        {
            if (! fullPresetEnabled) continue;
            isFull = true;
        }
        else
        {
            continue;
        }

        int start1, size1, start2, size2;
        pcFifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 > 0)
        {
            const int dst = (size1 > 0) ? start1 : start2;
            pcQueue[(size_t) dst] = { slot, m.getProgramChangeNumber(), isFull };
            pcFifo.finishedWrite(1);
            needPC = true;
        }
    }
    return needPC;
}

void ProcessorBase::drainPendingMidiProgramChanges()
{
    const int ready = pcFifo.getNumReady();
    if (ready <= 0) return;

    int start1, size1, start2, size2;
    pcFifo.prepareToRead(ready, start1, size1, start2, size2);
    const int activeChannels = getNumActiveChannels();

    auto handle = [this, activeChannels](const ProgramChangeEvent& ev)
    {
        if (ev.fullPreset)
        {
            if (! midiFullPresetMap.hasPreset(ev.presetIndex)) return;
            const juce::File f { midiFullPresetMap.getPresetPath(ev.presetIndex) };
            if (f.existsAsFile())
                applyFullMidiPreset(f);
            return;
        }
        if (ev.slot < 0 || ev.slot >= activeChannels) return;
        if (! midiPresetMap.hasPreset(ev.presetIndex))   return;
        const juce::File f { midiPresetMap.getPresetPath(ev.presetIndex) };
        if (f.existsAsFile())
            applyMidiPresetSlot(ev.slot, f);
    };
    for (int i = 0; i < size1; ++i) handle(pcQueue[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) handle(pcQueue[(size_t)(start2 + i)]);
    pcFifo.finishedRead(ready);
}

void ProcessorBase::processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                                     std::unique_ptr<VoiceEngine>*            voices,
                                     int                                      numVoices,
                                     int                                      numSamples,
                                     double                                   effectiveBpm,
                                     std::array<juce::AudioBuffer<float>*, 8>* directOuts,
                                     juce::AudioBuffer<float>*                fxReturnsOut,
                                     const RetiredVoices*                     retired)
{
    fxChain.setHostBpm(effectiveBpm);
    mixerEngine.processBlock(masterBus, numVoices, voices, fxChain, numSamples,
                             directOuts, fxReturnsOut, retired);
}
