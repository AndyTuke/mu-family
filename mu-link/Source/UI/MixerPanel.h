#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "../Server/AudioServer.h"
#include "UI/MixerChannel.h"
#include "UI/MasterInsertPanel.h"
#include "UI/Components/SegmentControl.h"
#include "ClientColumns.h"
#include "UI/Components/HoverStatus.h"

namespace mu_link
{

// MixerPanel - the mu-link Mixer panel (docs/mu-link/design-ui.md section 3): a header with the
// "Clients" plate and the Names | Presets switch, eight client strips, a divider and the master
// column (the callback Master strip beside the shared insert panel). It drives the AudioServer
// directly, the way the old window did: a strip control writes to the server, and
// refreshFromServer() pushes the server's values back into the controls after a preset loads.
class MixerPanel : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kClients = ClientColumns::kClients;

    explicit MixerPanel(AudioServer& serverToDrive);
    ~MixerPanel() override;

    // Status line for the bar: a name, a value and the colour tag of the strip it concerns.
    std::function<void(const juce::String& name, const juce::String& value, juce::Colour tag)> onStatus;

    // A control was changed by the user (not by refreshFromServer): the owner marks the preset edited.
    std::function<void()> onEdited;

    // Pushes the server's mixer state (levels, mute, solo, EQ, master level, both inserts) into the
    // controls; no control writes back.
    void refreshFromServer();

    // Shows each slot's connection in its strip: the app name (or its preset name in Presets mode),
    // lit in the slot colour, or the ghost dash when the slot is empty. Runs on a timer as well.
    void updateNames();
    bool showsPresetNames() const noexcept { return labels.getSelectedIndex() == 1; }

    // The render-ahead depth asked of every client (frames; 0 = each client's own default). Sent again on each
    // timer tick while a slot is active, so a client that connects later gets it too. Changing it clears the
    // Dropouts lamps, since the old count no longer describes the new depth.
    void setLatencyFrames(int frames);
    int  getLatencyFrames() const noexcept { return latencyFrames; }
    int  getDropoutState(int slot) const noexcept { return dropouts[(size_t) slot].state; }   // 0 clean, 1 active, 2 latched
    void updateLatencyAndDropouts();   // runs on the timer; public so a test can step it

    // How long after a dropout the lamp stays red before it latches amber (ms).
    static constexpr int kDropoutHoldMs = 2000;
    // Underruns in this long after a slot connects are the empty ring filling, not a dropout (ms).
    static constexpr int kDropoutWarmupMs = 1000;

    MixerChannel&           getStrip(int slot)  { return *strips[(size_t) slot]; }
    MixerChannel&           getMaster()         { return master; }
    mu_ui::MasterInsertPanel& getInserts()      { return inserts; }
    SegmentControl&         getLabelModeControl() { return labels; }

    // The EQ band's slot in the 3-Band EQ insert (the server's band index), and its table index.
    static int eqSlotOf(MixerChannel::ClientEq band) noexcept;
    static constexpr int kEqAlgo = 6;   // "3-Band EQ" in the shared insert tables

    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;

private:
    void timerCallback() override { updateNames(); updateLatencyAndDropouts(); }
    void clearDropout(int slot);
    void report(const juce::String& name, const juce::String& value, juce::Colour tag);

    AudioServer& server;

    SegmentControl labels { { "Names", "Presets" } };
    std::array<std::unique_ptr<MixerChannel>, kClients> strips;
    MixerChannel master { MixerChannel::Type::Master, "Master", juce::Colours::white };
    mu_ui::MasterInsertPanel inserts;

    std::array<juce::String, kClients> shownNames;
    std::array<int, kClients> shownActive;
    int latencyFrames = 0;
    struct DropoutTrack { std::uint64_t seen = 0; juce::int64 lastMs = 0, graceUntilMs = 0; bool wasActive = false; int state = 0; };
    std::array<DropoutTrack, kClients> dropouts;
    mu_ui::HoverStatus hover;   // after the components it watches

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};

} // namespace mu_link
