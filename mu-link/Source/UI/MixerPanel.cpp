#include "MixerPanel.h"
#include "Audio/InsertSlotConfig.h"   // normToActual / actualToNorm

namespace mu_link
{
namespace
{
constexpr int kHeaderSwitchW = 150;   // the Names | Presets switch (Medium baseline)
constexpr int kHeaderGap     = 4;     // header row to the strips

using Eq = MixerChannel::ClientEq;
constexpr Eq kBands[4] = { Eq::High, Eq::MidHz, Eq::Mid, Eq::Low };
}

// The EQ insert's own slot order is Low, Mid, Mid Hz, High; the strip shows High, Mid Hz, Mid, Low.
int MixerPanel::eqSlotOf(Eq band) noexcept
{
    switch (band)
    {
        case Eq::Low:   return 0;
        case Eq::Mid:   return 1;
        case Eq::MidHz: return 2;
        case Eq::High:  return 3;
    }
    return 0;
}

MixerPanel::MixerPanel(AudioServer& serverToDrive)
    : server(serverToDrive)
{
    shownActive.fill(-1);

    // Names | Presets: what each strip's name display shows.
    labels.onChange = [this](int) { updateNames(); };
    addAndMakeVisible(labels);
    hover.watch(labels, "Strip labels", "app name or loaded preset");

    // Eight client strips: each control writes straight to the server.
    for (int i = 0; i < kClients; ++i)
    {
        auto strip = std::make_unique<MixerChannel>(MixerChannel::Type::Client, "", MuLookAndFeel::channelPalette[i]);
        auto& st = *strip;
        st.setClientSlot(i);
        st.setActive(false);
        st.bindClientMeter(server.clientPeakMeter(i));

        const juce::String who = "Client " + juce::String(i + 1);
        const auto tag = MuLookAndFeel::channelPalette[i];
        st.onClientLevel = [this, i, who, tag](float v)
        {
            server.setClientGain(i, v);
            if (onEdited) onEdited();
            report(who + " level", MixerChannel::formatClientDb(v) + " dB", tag);
        };
        st.onClientMute = [this, i](bool m) { server.setClientMute(i, m); if (onEdited) onEdited(); };
        st.onClientSolo = [this, i](bool s) { server.setClientSolo(i, s); if (onEdited) onEdited(); };
        st.onClientEq = [this, i](Eq band, float actual)
        {
            const int slot = eqSlotOf(band);
            server.setClientEqParam(i, slot, mu_ui::actualToNorm(actual, kEqAlgo, slot));
            if (onEdited) onEdited();
        };
        for (auto band : kBands)
            st.getClientEqKnob(band).onStatusUpdate = [this, who, tag](const juce::String& n, const juce::String& v)
            { report(who + " " + n, v, tag); };

        addAndMakeVisible(st);
        strips[(size_t) i] = std::move(strip);
    }

    // Master column: the callback Master strip with its fader level to the client faders, then the inserts.
    master.useCallbackMaster();
    master.setFaderTop(MixerChannel::clientFaderTop());
    master.bindMasterMeter(server.masterPeakMeter());
    master.onMasterLevel = [this](float v)
    {
        server.setMasterGain(v);
        if (onEdited) onEdited();
        report("Master level", MixerChannel::formatClientDb(v) + " dB", juce::Colours::transparentBlack);
    };
    addAndMakeVisible(master);

    inserts.onAlgorithm = [this](int slot, int algorithm) { server.setMasterInsertAlgo(slot, algorithm); if (onEdited) onEdited(); };
    inserts.onParam     = [this](int slot, int param, float norm) { server.setMasterInsertParam(slot, param, norm); if (onEdited) onEdited(); };
    addAndMakeVisible(inserts);

    hover.onStatus = [this](const juce::String& n, const juce::String& v) { report(n, v, juce::Colours::transparentBlack); };

    refreshFromServer();
    updateNames();
    startTimerHz(12);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
}

void MixerPanel::report(const juce::String& name, const juce::String& value, juce::Colour tag)
{
    if (onStatus) onStatus(name, value, tag);
}

// Server state into the controls (no callbacks fire from the setters).
void MixerPanel::refreshFromServer()
{
    for (int i = 0; i < kClients; ++i)
    {
        auto& st = *strips[(size_t) i];
        st.setClientLevel(server.clientGainValue(i));
        st.setClientMuteSolo(server.clientMuted(i), server.clientSoloed(i));
        for (auto band : kBands)
        {
            const int slot = eqSlotOf(band);
            st.setClientEq(band, mu_ui::normToActual(server.clientEqValue(i, slot), kEqAlgo, slot));
        }
    }

    master.setMasterLevel(server.masterGainValue());

    for (int w = 0; w < mu_ui::MasterInsertPanel::kSlots; ++w)
    {
        inserts.setAlgorithm(w, server.masterInsertAlgo(w));
        for (int p = 0; p < mu_ui::MasterInsertPanel::kParams; ++p)
            inserts.setParam(w, p, server.masterInsertParam(w, p));
    }
}

// Live attach and detach: the app name (or its preset name) lit in the slot colour, or the empty ghost.
void MixerPanel::updateNames()
{
    if (! server.hasBus())
        return;   // the shared-memory bus could not be created: there is no registry to read

    auto& reg = server.registry();
    for (int i = 0; i < kClients; ++i)
    {
        const bool active = reg.slots[i].active.load(std::memory_order_acquire) != 0;
        juce::String shown;
        if (active)
        {
            shown = juce::String(reg.slots[i].name);
            if (showsPresetNames())
            {
                const juce::String preset(reg.slots[i].presetName);   // falls back to the app name until one is published
                if (preset.isNotEmpty()) shown = preset;
            }
        }

        auto& st = *strips[(size_t) i];
        if (shown != shownNames[(size_t) i]) { st.setChannelName(shown); shownNames[(size_t) i] = shown; }
        if ((int) active != shownActive[(size_t) i]) { st.setActive(active); shownActive[(size_t) i] = (int) active; }
    }
}

// Header row, then the eight strips, the master strip and the insert panel at one height.
void MixerPanel::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const auto cols = ClientColumns::forWidth(getWidth());
    const int inset = s(LF::kChannelInset);
    const int headerH = s(LF::kMixerHeaderH);

    labels.setBounds(cols.contentRight - s(kHeaderSwitchW), inset, s(kHeaderSwitchW), headerH);

    const int stripY = inset + headerH + s(kHeaderGap);
    const int stripH = getHeight() - stripY - inset;
    for (int i = 0; i < kClients; ++i)
        strips[(size_t) i]->setBounds(cols.stripX(i), stripY, cols.stripW, stripH);
    master.setBounds(cols.masterX, stripY, s(LF::kMixerMasterW), stripH);
    inserts.setBounds(cols.masterX + s(LF::kMixerMasterW), stripY, s(LF::kMixerInsertPanelW), stripH);
}

void MixerPanel::paint(juce::Graphics& g)
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    g.fillAll(LF::colour(LF::panelBackground));
    LF::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(), LF::appAccent(*this));

    const auto cols = ClientColumns::forWidth(getWidth());
    const int inset = s(LF::kChannelInset);
    const float plateH = (float) s(LF::kSectionPlateH);
    const float plateW = LF::namePlateWidth("Clients", plateH);
    const float plateY = (float) inset + ((float) s(LF::kMixerHeaderH) - plateH) * 0.5f;
    LF::drawNamePlate(g, { (float) cols.x0, plateY, plateW, plateH }, "Clients");

    // An engraved groove between the client strips and the master column.
    const int stripY = inset + s(LF::kMixerHeaderH) + s(kHeaderGap);
    const int y1 = stripY + s(4), y2 = getHeight() - inset - s(4);
    LF::drawGroove(g, { cols.dividerX + s(LF::kMixerDivW) / 2, y1 }, y2 - y1, true);
}

void MixerPanel::paintOverChildren(juce::Graphics& g)
{
    if (MuLookAndFeel::hasScrews(*this))
        MuLookAndFeel::drawPanelScrews(g, getLocalBounds().reduced(2).toFloat());
}

} // namespace mu_link
