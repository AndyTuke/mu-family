#pragma once

#include "Sequencer/Layer.h"   // mu-core: per-layer ControlSequences + ModulationMatrix
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"   // StepClock

#include <atomic>

namespace mu_on
{

class PluginProcessor;

// One mu-On layer, a track. The shared Layer (colour, modulation) plus what is the track's own and
// must move with it through add / delete / reorder: its step row and that row's clock, its Rumble volume envelope, the
// trigger counter the audio thread bumps and the editor polls to pulse the sidebar item. Which engine
// the track runs is a parameter (`t{N}_eng`), not a property of this object.
struct Track : Layer
{
    std::atomic<int> triggers { 0 };

    // The track's step row (the pattern points its slot at it) and, when the track runs Rumble, the
    // drawable bar-volume envelope the engine follows. The envelope is edited on the message thread and
    // read by the audio thread under `rumbleEnvLock`; both are kept when the engine changes.
    StepRow           row;
    StepClock         clock;   // the row's own step cursor: it moves with the track (audio thread)
    ControlSequence   rumbleEnv;
    mutable CopyableSpinLock rumbleEnvLock;

    // The slot this track sits in (kept current by the structural edits through onMoved).
    int slot = 0;
    OwnerLink<PluginProcessor> owner;

    Track();

    // Flat, full level, one bar long: the envelope of a track that has not drawn one.
    void resetRumbleEnv();
    bool rumbleEnvIsDefault() const;

    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
    bool isValidDest(const std::string& id) const override;
    void onMoved(int newIndex) override { slot = newIndex; }
};

} // namespace mu_on
