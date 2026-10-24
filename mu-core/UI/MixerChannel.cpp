// split into 2 partial-class TUs (this file + MixerChannel_Bindings.cpp). This TU keeps the ctor,
// paint/resized layout code, and the small status helpers. The master insert column is not part of
// the strip: it is mu_ui::MasterInsertPanel, placed beside the Master strip by the owner.

#include "MixerChannel.h"
#include "Audio/AlgorithmNames.h"   // mu-core: insert-algorithm selector list
#include "UI/InsertSlotUi.h"        // configureKnobFromSlot (client EQ stack)
#include <cmath>

MixerChannel::MixerChannel(Type t, const juce::String& name, juce::Colour col)
    : channelType(t), channelName(name), channelColour(col)
{
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setRange(0.0, 1.0, 0.001);
    fader.setValue(0.75, juce::dontSendNotification);
    // The family level colour for every strip: the shared linear-slider drawing reads the knob colour.
    fader.setColour(juce::Slider::rotarySliderFillColourId, MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(fader);
    fader.addListener(&stripStatus);
    addAndMakeVisible(vuMeter);
    if (hasSidechainControls())
        addAndMakeVisible(grMeter);

    panKnob.setRange(-1.0, 1.0, 0.01);
    panKnob.setValue(0.0);
    panKnob.getSlider().textFromValueFunction = [](double) { return juce::String(); };
    panKnob.statusTextFromValue = [](double v) -> juce::String
    {
        const int pct = (int) std::round(std::abs(v) * 100.0);
        return pct == 0 ? juce::String("Centre") : (v < 0.0 ? "Left " : "Right ") + juce::String(pct) + " %";
    };
    reportKnobStatus(panKnob, "Pan");
    if (! isClient())   // a client strip has no pan
        addAndMakeVisible(panKnob);

    if (hasSends())
    {
        sendEffect.setRange(0.0, 1.0, 0.01);
        sendDelay.setRange (0.0, 1.0, 0.01);
        sendReverb.setRange(0.0, 1.0, 0.01);

        auto noValue = [](double) { return juce::String(); };
        sendEffect.getSlider().textFromValueFunction = noValue;
        sendDelay.getSlider().textFromValueFunction  = noValue;
        sendReverb.getSlider().textFromValueFunction = noValue;
        for (auto* k : { &sendEffect, &sendDelay, &sendReverb }) k->setStatusUnit(mu_fmt::Unit::Percent01);
        reportKnobStatus(sendEffect, "Effect Send");
        reportKnobStatus(sendDelay,  "Delay Send");
        reportKnobStatus(sendReverb, "Reverb Send");

        addAndMakeVisible(sendEffect);
        addAndMakeVisible(sendDelay);
        addAndMakeVisible(sendReverb);
    }

    dbLabel.setJustificationType(juce::Justification::centred);
    dbLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    addAndMakeVisible(dbLabel);

    if (hasMuteSolo())
    {
        muteBtn.setClickingTogglesState(true);
        soloBtn.setClickingTogglesState(true);
        addAndMakeVisible(muteBtn);
        addAndMakeVisible(soloBtn);
        muteBtn.addListener(&stripStatus);
        soloBtn.addListener(&stripStatus);
    }

    if (hasOutputBus())
    {
        // Item id = value + 1: 0 -> Main. A product with group buses lists them ("Bus 1" ...); the others keep
        // the DAW direct outs ("Out 1" ... "Out 8").
        outBusBox.addItem("Main", 1);
        if (mu_limits::kMaxBuses > 0)
            for (int i = 0; i < mu_limits::kMaxBuses; ++i)
                outBusBox.addItem(busName(i), i + 2);
        else
            for (int i = 1; i <= 8; ++i)
                outBusBox.addItem("Out " + juce::String(i), i + 1);
        outBusBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(outBusBox);
    }

    if (hasSidechainControls())
        addSidechainControls();

    if (isClient())
        addClientControls();
}

// The sidechain source selector and amount / attack / release knobs (values show in the status bar).
void MixerChannel::addSidechainControls()
{
    scSourceBox.addItem(juce::String::charToString(0x2014), 1); // "—"
    scSourceBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(scSourceBox);

    auto noVal = [](double) { return juce::String(); };

    scAmount.setRange(0.0, 100.0, 1.0);
    scAmount.setValue(0.0);
    scAmount.getSlider().textFromValueFunction = noVal;
    scAmount.setStatusUnit(mu_fmt::Unit::Percent100);
    reportKnobStatus(scAmount, "Sidechain Amount");
    addAndMakeVisible(scAmount);

    scAttack.setRange(0.0, 500.0, 1.0);
    scAttack.getSlider().setSkewFactorFromMidPoint(22.0);
    scAttack.setValue(0.0);
    scAttack.getSlider().textFromValueFunction = noVal;
    scAttack.setStatusUnit(mu_fmt::Unit::Ms);
    reportKnobStatus(scAttack, "Sidechain Attack");
    addAndMakeVisible(scAttack);

    scRelease.setRange(0.0, 2000.0, 1.0);
    scRelease.getSlider().setSkewFactorFromMidPoint(141.0);
    scRelease.setValue(0.0);
    scRelease.getSlider().textFromValueFunction = noVal;
    scRelease.setStatusUnit(mu_fmt::Unit::Ms);
    reportKnobStatus(scRelease, "Sidechain Release");
    addAndMakeVisible(scRelease);
}

MixerChannel::~MixerChannel()
{
    fader.removeListener(&stripStatus);
    muteBtn.removeListener(&stripStatus);
    soloBtn.removeListener(&stripStatus);
}

void MixerChannel::reportStatus(const juce::String& what, const juce::String& value)
{
    if (onStatusUpdate) onStatusUpdate(channelName + " " + what, value, channelColour);
}

void MixerChannel::StripStatus::sliderValueChanged(juce::Slider* s)
{
    if (s != &owner.fader) return;
    const auto v = (float) owner.fader.getValue();
    const juce::String db = owner.isClient() ? owner.formatClientDb(v)
                                             : (v <= 0.0f ? juce::String("-inf dB") : juce::String(20.0f * std::log10(v), 1) + " dB");
    owner.reportStatus("Level", db);
}

void MixerChannel::StripStatus::buttonClicked(juce::Button* b)
{
    if (b == &owner.muteBtn)      owner.reportStatus("Mute", owner.muteBtn.getToggleState() ? "On" : "Off");
    else if (b == &owner.soloBtn) owner.reportStatus("Solo", owner.soloBtn.getToggleState() ? "On" : "Off");
}

// A strip knob reports to the status bar as "<strip> <what>  <value + unit>", in the strip's colour.
void MixerChannel::reportKnobStatus(KnobWithLabel& knob, const juce::String& what)
{
    knob.onStatusUpdate = [this, what](const juce::String&, const juce::String& value)
    {
        if (onStatusUpdate) onStatusUpdate(channelName + " " + what, value, channelColour);
    };
}

//==============================================================================
void MixerChannel::updateDbLabel(float level)
{
    if (level <= 0.0f)
        dbLabel.setText("-inf", juce::dontSendNotification);
    else
        dbLabel.setText(juce::String(20.0f * std::log10(level), 1) + "dB",
                        juce::dontSendNotification);
}

void MixerChannel::resized()
{
    if (isClient()) { layoutClient(); return; }
    if (callbackMaster) { layoutCallbackMaster(); return; }

    // Every literal/constant in setBounds wrapped in mu_ui::s(). The parent
    // (MixerOverlay) is expected to pass already-scaled width/height — i.e.
    // we treat `w` and `h` as physical pixels, then scale Medium-baseline
    // constants on top via s() so internal positions stay coherent.
    //
    // Vertical layout is uniform across channel types: every section
    // (sidechain / sends / fader / outBus / mute-solo) reserves its slot
    // regardless of whether the channel actually uses it. That way pans
    // align horizontally across the whole mixer, all faders have the same
    // height, and returns (no outBus) still bottom-align with rhythm
    // channels. Sections that don't apply to a channel simply don't render
    // their controls — the slot stays empty.
    using mu_ui::s;

    const int stripW = getWidth();

    // Name area total Y: top padding + name pill + bottom padding (equal room round the pill).
    const int nameBottom = s(kNamePadding * 2 + kNameH);

    const int scH    = layoutSidechain(stripW, nameBottom);
    const int faderY = layoutSends(stripW, nameBottom + scH);
    layoutFaderAndButtons(stripW, faderY);
}

// The sidechain section: source selector, amount, attack / release. Its slot is reserved on
// every strip so the sections below align across the mixer. Returns the slot's height.
int MixerChannel::layoutSidechain(int stripW, int nameBottom)
{
    using mu_ui::s;
    const int scH = s(kSidechainH);
    scSourceBox.setVisible(hasSidechainControls());
    scAmount   .setVisible(hasSidechainControls());
    scAttack   .setVisible(hasSidechainControls());
    scRelease  .setVisible(hasSidechainControls());
    if (hasSidechainControls())
    {
        // scAmount renders at the mixer-strip knob size (kMixerStripKnobW —
        // pinned to the strip width). scAttack/Release at Size 4.
        const int s3W = s(MuLookAndFeel::kMixerStripKnobW);
        const int s3H = s(MuLookAndFeel::kMixerStripKnobH);
        const int s4W = s(MuLookAndFeel::kKnobSize4W);
        const int s4H = s(MuLookAndFeel::kKnobSize4H);
        const int s3X = (stripW - s3W) / 2;
        const int envPairW = 2 * s4W;
        const int envStartX = (stripW - envPairW) / 2;
        const int scAmtAbsY = nameBottom + s(kScSrcH);
        const int scEnvAbsY = scAmtAbsY + s(kScAmtH);

        scSourceBox.setBounds(0, nameBottom, stripW, s(kScSrcH));
        scAmount   .setBounds(s3X, scAmtAbsY, s3W, s3H);
        scAttack .setBounds(envStartX,        scEnvAbsY, s4W, s4H);
        scRelease.setBounds(envStartX + s4W,  scEnvAbsY, s4W, s4H);

        sidechainPaneBounds = { 1, nameBottom + 1, stripW - 2, scH - 2 };
    }
    else
    {
        sidechainPaneBounds = {};
    }

    return scH;
}

// The sends (a return can't send to itself) and pan, at the mixer-strip knob size, from sendY.
// Returns the fader's top.
int MixerChannel::layoutSends(int stripW, int sendY)
{
    using mu_ui::s;
    const int spH   = s(kSendsAreaH);
    const int sendH = s(kSendKnobH);
    const int faderY = sendY + spH;

    // A return channel cannot send to itself (would create a feedback loop).
    sendEffect.setVisible(hasSends() && (channelType == Type::Channel || channelType == Type::Bus));
    sendDelay .setVisible(hasSends() && channelType != Type::DelayReturn
                                     && channelType != Type::ReverbReturn);
    sendReverb.setVisible(hasSends() && channelType != Type::ReverbReturn);

    // Sends + pan render at the mixer-strip knob size — pinned to strip width.
    const int s3W = s(MuLookAndFeel::kMixerStripKnobW);
    const int s3H = s(MuLookAndFeel::kMixerStripKnobH);
    const int s3X = (stripW - s3W) / 2;
    sendEffect.setBounds(s3X, sendY,             s3W, s3H);
    sendDelay .setBounds(s3X, sendY + sendH,     s3W, s3H);
    sendReverb.setBounds(s3X, sendY + sendH * 2, s3W, s3H);
    panKnob   .setBounds(s3X, sendY + sendH * 3, s3W, s3H);

    // Sends pane: from first visible send to end of pan.
    {
        const int firstOff = (channelType == Type::Channel || channelType == Type::Bus) ? 0
                           : (channelType == Type::EffectReturn) ? sendH
                           : (channelType == Type::DelayReturn)  ? sendH * 2
                           : sendH * 3;  // ReverbReturn / Master: pan only
        const int paneH = spH - firstOff;
        sendsPaneBounds = (paneH > 0) ? juce::Rectangle<int>{ 1, sendY + firstOff + 1,
                                                               stripW - 2, paneH - 2 }
                                      : juce::Rectangle<int>{};
    }

    return faderY;
}

// Fader, GR and VU meters, then the bottom-anchored output bus, dB readout and mute / solo.
// Their slots are reserved on every strip so pan / fader / mute align across channel types.
void MixerChannel::layoutFaderAndButtons(int stripW, int faderY)
{
    using mu_ui::s;
    const int h = getHeight();
    // Bottom-anchored sections always reserve their slot so pan/fader/mute
    // align across channel types. Master (no mute/solo) and returns (no
    // outBus) still leave the slot empty — controls just hide.
    const int muteY   = h - s(kButtonH);
    const int dbY     = muteY - s(kDbH);
    const int busY    = dbY - s(kOutBusH);
    const int faderEnd = busY - 2;
    const int faderH  = juce::jmax(s(40), faderEnd - faderY);

    placeFaderAndMeters(stripW, faderY, faderH, hasSidechainControls());

    outBusBox.setVisible(hasOutputBus());
    if (hasOutputBus())
        outBusBox.setBounds(0, busY, stripW, s(kOutBusH));

    dbLabel.setBounds(0, dbY, stripW, s(kDbH));

    muteBtn.setVisible(hasMuteSolo());
    soloBtn.setVisible(hasMuteSolo());
    if (hasMuteSolo())
    {
        const int hw = stripW / 2;
        muteBtn.setBounds(0,  muteY, hw,          s(kButtonH));
        soloBtn.setBounds(hw, muteY, stripW - hw, s(kButtonH));
    }

    faderPaneBounds = { 1, faderY - 2, stripW - 2, h - faderY + 1 };

}

void MixerChannel::setEffectSendLabel(const juce::String& name)
{
    sendEffect.setLabel(name);
}

void MixerChannel::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    if (isClient())
    {
        paintClientName(g);
        if (! faderPaneBounds.isEmpty())
        {
            g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.45f));
            g.drawRoundedRectangle(faderPaneBounds.toFloat(), 2.0f, 1.0f);
        }
        if (! active)   // an empty slot: dimmed below the name, still operable
        {
            const int nameEnd = mu_ui::s(kNamePadding * 2 + MuLookAndFeel::kClientNameH);
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::backgroundMixerStripDim));
            g.fillRect(0, nameEnd, w, h - nameEnd);
        }
        return;
    }

    // Rhythm colour goes JUST around the name — a rounded outline framing the
    // name area. kNamePadding gives equal breathing room on all four sides
    // of the kNameH-tall pill so the rhythm-colour border doesn't sit on
    // the strip edge.
    const int stripW = w;

    g.setColour(active ? MuLookAndFeel::colour(Id::headingText)
                       : MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(channelName, 0, kNamePadding, stripW, kNameH,
               juce::Justification::centred, true);

    // Rhythm-colour border around the name pill (rhythm strips only — returns
    // and master keep their fixed colour scheme since they're not per-channel).
    if (active && channelType == Type::Channel)
    {
        const juce::Rectangle<int> namePill {
            kNamePadding, kNamePadding, stripW - 2 * kNamePadding, kNameH };
        g.setColour(channelColour);
        g.drawRoundedRectangle(namePill.toFloat(), 3.0f, 1.5f);
    }

    // Right-edge channel divider
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float)(w - 1), 0.0f, (float)(w - 1), (float)h, 0.5f);

    // ── Section pane borders ──────────────────────────────────────────────────
    const juce::Colour borderCol = MuLookAndFeel::colour(Id::segmentInactiveBorder)
                                       .withAlpha(0.45f);
    g.setColour(borderCol);
    if (!sidechainPaneBounds.isEmpty())
        g.drawRoundedRectangle(sidechainPaneBounds.toFloat(), 2.0f, 1.0f);
    if (!sendsPaneBounds.isEmpty())
        g.drawRoundedRectangle(sendsPaneBounds.toFloat(), 2.0f, 1.0f);
    if (!faderPaneBounds.isEmpty())
        g.drawRoundedRectangle(faderPaneBounds.toFloat(), 2.0f, 1.0f);

    // Section labels (tiny, top-right of each pane)
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    if (!sidechainPaneBounds.isEmpty() && hasOutputBus())  // "SC" tag only on rhythm strips (others have no output bus label)
        g.drawText("SC", sidechainPaneBounds.getRight() - 14, sidechainPaneBounds.getY() + 1,
                   12, 10, juce::Justification::centredRight, false);

    // Inactive overlay — covers everything below the name area
    if (!active)
    {
        const int nameAreaEnd = kNamePadding * 2 + kNameH;
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::backgroundMixerStripDim));
        g.fillRect(0, nameAreaEnd, w, h - nameAreaEnd);
    }
}

//==============================================================================
// Client strip (mu-link): name LCD, EQ stack, fader + VU, dB readout, mute / solo.

// The four EQ knobs are the 3-Band EQ insert's own slots (Low, Mid, Mid Hz, High) shown top to
// bottom as High, Mid Hz, Mid, Low; their labels, ranges and value text come from that table.
void MixerChannel::addClientControls()
{
    constexpr int kEqAlgo = 6;                       // the EQ row of mu_ui::kInsertAlgoSlots
    constexpr int kSlotOf[4] = { 3, 2, 1, 0 };       // ClientEq (High, Mid Hz, Mid, Low) -> table slot
    for (size_t i = 0; i < clientEq.size(); ++i)
    {
        const int slot = kSlotOf[i];
        const float flat = mu_ui::kInsertAlgoDefaults[kEqAlgo][slot];
        auto& k = clientEq[i];
        mu_ui::configureKnobFromSlot(k, kEqAlgo, slot, mu_ui::actualToNorm(flat, kEqAlgo, slot),
            [this, i](float) { if (onClientEq) onClientEq((ClientEq) i, (float) clientEq[i].getValue()); });
        // Ctrl+click returns the knob to flat (a plain double-click types a value, as everywhere).
        k.getSlider().setDoubleClickReturnValue(true, (double) flat, juce::ModifierKeys::ctrlModifier);
        addAndMakeVisible(k);
    }

    // The fader runs 0 .. kClientFaderMax linear gain; double-click returns to unity.
    fader.setRange(0.0, (double) MuLookAndFeel::kClientFaderMax, 0.001);
    fader.setValue((double) MuLookAndFeel::kClientFaderUnity, juce::dontSendNotification);
    fader.setDoubleClickReturnValue(true, (double) MuLookAndFeel::kClientFaderUnity);
    fader.onValueChange = [this]
    {
        const auto v = (float) fader.getValue();
        dbLabel.setText(formatClientDb(v), juce::dontSendNotification);
        if (onClientLevel) onClientLevel(v);
    };
    dbLabel.setText(formatClientDb(MuLookAndFeel::kClientFaderUnity), juce::dontSendNotification);
    dbLabel.setColour(juce::Label::textColourId, MuLookAndFeel::colour(Id::labelText));

    muteBtn.onClick = [this] { clientMuted  = muteBtn.getToggleState(); if (onClientMute) onClientMute(clientMuted); };
    soloBtn.onClick = [this] { clientSoloed = soloBtn.getToggleState(); if (onClientSolo) onClientSolo(clientSoloed); };

    // The Dropouts lamp under mute / solo; a click clears a latched dropout.
    dropoutLamp.setLabel("Dropouts");
    dropoutLamp.onClick = [this] { if (onClientDropoutClear) onClientDropoutClear(); };
    addAndMakeVisible(dropoutLamp);
    setClientDropout(Dropout::Clean);
}

// Clean: unlit. Active: red and bright. Latched: amber and dim, until the lamp is clicked. The status text
// says what to do, never colour alone.
void MixerChannel::setClientDropout(Dropout state)
{
    using LF = MuLookAndFeel;
    const auto& L = LF::lighting();
    switch (state)
    {
        case Dropout::Clean:
            dropoutLamp.setState(LF::colour(Id::indicatorFault), L.lampOff);
            dropoutLamp.setStatus("Dropouts", "No dropouts");
            break;
        case Dropout::Active:
            dropoutLamp.setState(LF::colour(Id::indicatorFault), L.lampOn + L.lampPlayhead);
            dropoutLamp.setStatus("Dropouts", "Dropouts: raise Latency in Settings");
            break;
        case Dropout::Latched:
            dropoutLamp.setState(LF::colour(Id::segmentWarningBorder), L.lampDim);
            dropoutLamp.setStatus("Dropouts", "Dropped out earlier: raise Latency in Settings (click to clear)");
            break;
    }
}

juce::String MixerChannel::formatClientDb(float linearLevel)
{
    if (linearLevel <= 0.0f) return "-inf";
    const float db = std::round(20.0f * std::log10(linearLevel) * 10.0f) / 10.0f;   // to one decimal
    if (db == 0.0f) return "0.0";
    return (db > 0.0f ? "+" : "") + juce::String(db, 1);
}

void MixerChannel::setClientSlot(int slotIndex)
{
    const int n = MuLookAndFeel::kLayerPaletteSize;
    channelColour = MuLookAndFeel::layerPalette[((slotIndex % n) + n) % n];
    repaint();
}

void MixerChannel::setClientLevel(float linear)
{
    fader.setValue((double) linear, juce::dontSendNotification);
    dbLabel.setText(formatClientDb((float) fader.getValue()), juce::dontSendNotification);
}

void MixerChannel::setClientEq(ClientEq band, float actual)
{
    clientEq[(size_t) band].setValue((double) actual, juce::dontSendNotification);
}

void MixerChannel::setClientMuteSolo(bool muted, bool soloed)
{
    clientMuted = muted;
    clientSoloed = soloed;
    muteBtn.setToggleState(muted,  juce::dontSendNotification);
    soloBtn.setToggleState(soloed, juce::dontSendNotification);
}

void MixerChannel::bindClientMeter(const std::atomic<float>& peak)
{
    vuMeter.getLevel  = [&peak] { return peak.load(); };
    vuMeter.getLevelR = nullptr;   // a single source drives both bars
}

void MixerChannel::bindClientMeter(const std::atomic<float>& peakL, const std::atomic<float>& peakR)
{
    vuMeter.getLevel  = [&peakL] { return peakL.load(); };
    vuMeter.getLevelR = [&peakR] { return peakR.load(); };
}

void MixerChannel::placeFaderAndMeters(int stripW, int y, int h, bool withGR)
{
    using mu_ui::s;
    const int vuW = s(kVUW);
    const int grW = withGR ? s(kGRW) : 0;
    const int faderW = juce::jlimit(1, s(kMixerFaderW), stripW - vuW - grW);
    // Centre the fader slot on the strip's axis; keep the whole group inside the strip.
    const int x0 = juce::jlimit(0, juce::jmax(0, stripW - (faderW + grW + vuW)), (stripW - faderW) / 2);
    fader.setBounds(x0, y, faderW, h);
    if (withGR)
        grMeter.setBounds(x0 + faderW, y, grW, h);
    vuMeter.setBounds(x0 + faderW + grW, y, vuW, h);
}

// Top to bottom: name LCD, EQ stack, fader + VU taking all the height left (no cap), dB readout,
// mute / solo. The fixed parts are top- and bottom-anchored, so the fader absorbs the strip height.
void MixerChannel::layoutClient()
{
    using mu_ui::s;
    using LF = MuLookAndFeel;
    const int w = getWidth(), h = getHeight();

    const int pad = s(kNamePadding);
    int y = pad + s(LF::kClientNameH) + s(LF::kClientGap);

    const int kw = s(LF::kMixerStripKnobW), kh = s(LF::kMixerStripKnobH);
    for (auto& k : clientEq)
    {
        k.setBounds((w - kw) / 2, y, kw, kh);
        y += kh;
    }
    y += s(LF::kClientGap);

    // Bottom row: the Dropouts lamp, then mute / solo above it, then the dB readout, then the fader.
    const int lampY = h - s(LF::kClientBottomGap) - s(LF::kClientLampH);
    const int muteY = lampY - s(LF::kClientDbGap) - s(LF::kClientButtonH);
    const int dbY   = muteY - s(LF::kClientDbGap) - s(LF::kClientDbH);
    const int faderH = juce::jmax(s(LF::kClientFaderMinH), dbY - y);

    placeFaderAndMeters(w, y, faderH, false);
    dbLabel.setBounds(0, dbY, w, s(LF::kClientDbH));

    const int hw = w / 2;
    muteBtn.setBounds(0,  muteY, hw,     s(LF::kClientButtonH));
    soloBtn.setBounds(hw, muteY, w - hw, s(LF::kClientButtonH));
    dropoutLamp.setBounds(s(LF::kClientBottomGap), lampY, w - 2 * s(LF::kClientBottomGap), s(LF::kClientLampH));

    faderPaneBounds = { 1, y - 2, w - 2, h - y + 1 };
}

// The name LCD: lit in the slot's colour when connected, an unlit ghost "-" when empty.
void MixerChannel::paintClientName(juce::Graphics& g) const
{
    using mu_ui::s;
    const auto& L = MuLookAndFeel::lighting();
    const int pad = s(kNamePadding);
    const juce::Rectangle<int> r(pad, pad, getWidth() - 2 * pad, s(MuLookAndFeel::kClientNameH));
    const bool connected = channelName.isNotEmpty();

    MuLookAndFeel::drawLcdGlass(g, r.toFloat(), channelColour, connected);

    g.setFont(MuLookAndFeel::lcdFont(mu_ui::sf((float) MuLookAndFeel::kLcdTextH)));
    const auto text = r.reduced(s(MuLookAndFeel::kLcdTextPadX), 0);
    if (connected)
    {
        g.setColour(channelColour.withAlpha(L.highlight(L.lcdGlow)));   // soft glow round the lit lettering
        for (auto off : { juce::Point<int>(-1, 0), juce::Point<int>(1, 0), juce::Point<int>(0, -1), juce::Point<int>(0, 1) })
            g.drawText(channelName, text.translated(off.x, off.y), juce::Justification::centred, true);
        g.setColour(channelColour.brighter(0.6f));
        g.drawText(channelName, text, juce::Justification::centred, true);
    }
    else
    {
        g.setColour(channelColour.withAlpha(L.lcdGhost));
        g.drawText("-", text, juce::Justification::centred, true);
    }
    MuLookAndFeel::drawLcdFront(g, r.toFloat());
}

//==============================================================================
// Callback-driven Master (mu-link): the 80 px strip alone, no processor behind it.

void MixerChannel::useCallbackMaster()
{
    if (channelType != Type::Master) return;
    callbackMaster = true;
    panKnob.setVisible(false);

    // Same fader as a Client strip: 0 .. kClientFaderMax linear gain, double-click to unity.
    fader.setRange(0.0, (double) MuLookAndFeel::kClientFaderMax, 0.001);
    fader.setValue((double) MuLookAndFeel::kClientFaderUnity, juce::dontSendNotification);
    fader.setDoubleClickReturnValue(true, (double) MuLookAndFeel::kClientFaderUnity);
    fader.onValueChange = [this]
    {
        const auto v = (float) fader.getValue();
        dbLabel.setText(formatClientDb(v), juce::dontSendNotification);
        if (onMasterLevel) onMasterLevel(v);
    };
    dbLabel.setText(formatClientDb(MuLookAndFeel::kClientFaderUnity), juce::dontSendNotification);
    dbLabel.setColour(juce::Label::textColourId, MuLookAndFeel::colour(Id::labelText));
    resized();
}

void MixerChannel::setMasterLevel(float linear)
{
    fader.setValue((double) linear, juce::dontSendNotification);
    dbLabel.setText(formatClientDb((float) fader.getValue()), juce::dontSendNotification);
}

int MixerChannel::clientFaderTop()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    return s(kNamePadding) + s(LF::kClientNameH) + s(LF::kClientGap)
         + 4 * s(LF::kMixerStripKnobH) + s(LF::kClientGap);
}

// Name at the top, then the fader + VU from faderTopPx down to the dB readout, which sits where the
// Client strips' readouts do (their mute / solo row stays empty).
void MixerChannel::layoutCallbackMaster()
{
    using mu_ui::s;
    using LF = MuLookAndFeel;
    const int w = getWidth(), h = getHeight();

    const int top = faderTopPx >= 0 ? faderTopPx : s(kNamePadding * 2 + kNameH);
    const int dbY = h - s(LF::kClientBottomGap) - s(LF::kClientLampH) - s(LF::kClientDbGap) - s(LF::kClientButtonH)
                  - s(LF::kClientDbGap) - s(LF::kClientDbH);   // where the Client strips' readouts sit
    const int faderH = juce::jmax(s(LF::kClientFaderMinH), dbY - top);

    placeFaderAndMeters(w, top, faderH, false);
    dbLabel.setBounds(0, dbY, w, s(LF::kClientDbH));

    sidechainPaneBounds = {};
    sendsPaneBounds     = {};
    faderPaneBounds     = { 1, top - 2, w - 2, h - top + 1 };
}
