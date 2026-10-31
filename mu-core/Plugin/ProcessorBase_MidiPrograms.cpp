// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// MIDI program-change scan and the drain that applies them (message thread / audio thread hand-off).
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

bool ProcessorBase::scanMidiProgramChanges(const juce::MidiBuffer& midi)
{
    const uint8_t chMask           = midiPresetMap.getChannelMask();
    const bool    fullPresetEnabled = midiFullPresetMap.isEnabled();
    if (chMask == 0 && ! fullPresetEnabled) return false;

    bool needPC = false;
    for (const auto meta : midi)
    {
        // Raw bytes, not a juce::MidiMessage: constructing one allocates for messages over 8 bytes (SysEx).
        if (meta.numBytes < 2 || meta.numBytes > 3 || (meta.data[0] & 0xf0) != 0xc0) continue;
        const int ch = (meta.data[0] & 0x0f) + 1;      // 1-based, 1..16

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
            pcQueue[(size_t) dst] = { slot, meta.data[1] & 0x7f, isFull };
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
    const int activeChannels = getNumActiveLayers();

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
            applyMidiPresetLayer(ev.slot, f);
    };
    for (int i = 0; i < size1; ++i) handle(pcQueue[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) handle(pcQueue[(size_t)(start2 + i)]);
    pcFifo.finishedRead(ready);
}
