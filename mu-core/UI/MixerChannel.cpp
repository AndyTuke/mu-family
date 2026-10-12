// split into 3 partial-class TUs (this file + MixerChannel_Bindings.cpp +
// MixerChannel_Insert.cpp). This TU keeps the ctor, paint/resized layout code,
// and the small status helpers. kInsertDefaults lives in MixerChannel_Insert.cpp
// (its only consumer).

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
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(fader);
    addAndMakeVisible(vuMeter);
    if (hasSidechainControls())
        addAndMakeVisible(grMeter);

    panKnob.setRange(-1.0, 1.0, 0.01);
    panKnob.setValue(0.0);
    panKnob.getSlider().textFromValueFunction = [](double) { return juce::String(); };
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
    }

    if (hasOutputBus())
    {
        // descriptive labels — "Main" + "Out 1" … "Out 8".
        outBusBox.addItem("Main", 1);              // 0 -> Master
        for (int i = 1; i <= 8; ++i)
            outBusBox.addItem("Out " + juce::String(i), i + 1);
        outBusBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(outBusBox);
    }

    if (hasSidechainControls())
        addSidechainControls();

    if (hasInsert())
        addInsertControls();

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
    addAndMakeVisible(scAmount);

    scAttack.setRange(1.0, 500.0, 1.0);
    scAttack.getSlider().setSkewFactorFromMidPoint(22.0);
    scAttack.setValue(5.0);
    scAttack.getSlider().textFromValueFunction = noVal;
    addAndMakeVisible(scAttack);

    scRelease.setRange(10.0, 2000.0, 1.0);
    scRelease.getSlider().setSkewFactorFromMidPoint(141.0);
    scRelease.setValue(100.0);
    scRelease.getSlider().textFromValueFunction = noVal;
    addAndMakeVisible(scRelease);
}

// The master's two insert slots: algorithm selectors and the 8 slot knobs.
void MixerChannel::addInsertControls()
{
    auto addInsertCombo = [](juce::ComboBox& box) {
        mu_audio::populateInsertAlgoDropdown([&box](const char* n, int id) { box.addItem(n, id); });
        box.setSelectedId(1, juce::dontSendNotification);
    };
    addInsertCombo(insCharBox);
    addInsertCombo(insCharBox2);
    addAndMakeVisible(insCharBox);
    addAndMakeVisible(insCharBox2);

    // Just register the 8 generic Param knobs (4 per master slot) as
    // children. Their range / label / formatter / callbacks are populated
    // each time configureInsertAlgorithm runs — driven by the per-algo
    // config table in mu_ui::kInsertAlgoSlots.
    for (auto* k : { &insParam1, &insParam2, &insParam3, &insParam4,
                     &insParam1_2, &insParam2_2, &insParam3_2, &insParam4_2 })
        addAndMakeVisible(*k);
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

    // Master: right portion is the insert panel; everything else uses the strip width.
    const int insW   = insertShown() ? s(kInsertPanelW) : 0;
    const int stripW = getWidth() - insW;

    // Name area total Y: top padding + name pill + bottom padding (equal room round the pill).
    const int nameBottom = s(kNamePadding * 2 + kNameH);

    const int scH    = layoutSidechain(stripW, nameBottom);
    const int faderY = layoutSends(stripW, nameBottom + scH);
    layoutFaderAndButtons(stripW, faderY);
    if (insertShown())
        layoutInsertPanel(stripW, insW, nameBottom);
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
    sendEffect.setVisible(hasSends() && channelType == Type::Channel);
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
        const int firstOff = (channelType == Type::Channel)      ? 0
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

    const int vuW = s(kVUW);
    const int grW = hasSidechainControls() ? s(kGRW) : 0;
    fader.setBounds  (0,                       faderY, stripW - vuW - grW, faderH);
    if (hasSidechainControls())
        grMeter.setBounds(stripW - vuW - grW,  faderY, grW,                faderH);
    vuMeter.setBounds(stripW - vuW,            faderY, vuW,                faderH);

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

// The master's insert column, right of the strip: two slots stacked, each a rotated
// "Main Insert N" label (painted), the algorithm selector and its knobs.
void MixerChannel::layoutInsertPanel(int stripW, int insW, int nameBottom)
{
    using mu_ui::s;
    const int h = getHeight();
    // Reserve a narrow strip on the LEFT of the insert column for the
    // rotated "Main Insert 1/2" labels (drawn in paint()). Dropdown +
    // knobs occupy the remaining horizontal width. Removes the previous
    // horizontal name-label row, recovering ~22 px vertical so the four
    // EQ knobs fit inside each slot.
    const int pad     = s(4);
    const int labelW  = s(kInsertLabelW);
    const int ipX     = stripW + labelW + pad;
    const int ipW     = insW - labelW - 2 * pad;
    const int insTop  = nameBottom;
    const int insH    = h - insTop - pad;
    const int halfH   = insH / 2;
    insertMidY = insTop + halfH;

    // No horizontal title row — title is rotated and lives in the labelW strip.
    const int slot1CharY = insTop + pad;
    const int slot2CharY = insertMidY + pad;

    auto layoutSlot = [&](int charBoxY, int endY,
                           juce::ComboBox& charBox,
                           KnobWithLabel& p1, KnobWithLabel& p2,
                           KnobWithLabel& p3, KnobWithLabel& p4)
    {
        charBox.setBounds(ipX, charBoxY, ipW, s(kInsCharH));
        const int ky = charBoxY + s(kInsCharH) + 2;
        (void) endY;

        const int s2W = s(MuLookAndFeel::kKnobSize2W);
        const int s2H = s(MuLookAndFeel::kKnobSize2H);

        // Every algorithm, EQ included, packs into the same 2x2 grid; an odd last knob is centred.
        KnobWithLabel* vis[4];
        int nVis = 0;
        for (auto* k : { &p1, &p2, &p3, &p4 })
            if (k->isVisible()) vis[nVis++] = k;

        if (nVis > 0)
        {
            const int halfW = ipW / 2;
            const int halfKnobX = (halfW - s2W) / 2;
            for (int i = 0; i < nVis; ++i)
            {
                const bool isLastOdd = (i == nVis - 1) && (nVis % 2 == 1);
                const int kx = isLastOdd
                               ? ipX + (ipW - s2W) / 2
                               : ipX + (i % 2) * halfW + halfKnobX;
                vis[i]->setBounds(kx, ky + (i / 2) * s2H, s2W, s2H);
            }
        }

    };

    layoutSlot(slot1CharY, insertMidY,  insCharBox,  insParam1,   insParam2,   insParam3,   insParam4);
    layoutSlot(slot2CharY, h - pad,     insCharBox2, insParam1_2, insParam2_2, insParam3_2, insParam4_2);
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
    const int stripW = w - (insertShown() ? kInsertPanelW : 0);

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

    // ── Insert panel (Master: right portion) ─────────────────────────────────
    if (insertShown())
    {
        // Vertical separator between strip and insert panel
        g.setColour(borderCol.withAlpha(0.6f));
        g.drawLine((float)stripW, (float)kNamePadding, (float)stripW, (float)h, 1.0f);

        // "Main Insert 1/2" labels — rotated 90° CCW in the left strip of
        // each slot (same pattern as the section labels on the left of the
        // mixer channel area). Uses the same colour as knob value text so it
        // reads at the same weight as the knob values next to it.
        const juce::Colour labelCol = active ? MuLookAndFeel::colour(Id::valueText)
                                             : MuLookAndFeel::colour(Id::mutedText);
        const int insTop = kNamePadding * 2 + kNameH;  // start of insert content area — matches nameBottom in resized()
        g.setColour(labelCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));

        auto drawSlotLabel = [&](const juce::String& text, int top, int bottom)
        {
            const int hSlot = bottom - top;
            if (hSlot < 14) return;
            const juce::Rectangle<int> r { stripW, top, kInsertLabelW, hSlot };
            const float cx = (float) r.getCentreX();
            const float cy = (float) r.getCentreY();
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(
                -juce::MathConstants<float>::halfPi, cx, cy));
            g.drawFittedText(text,
                (int)(cx - r.getHeight() * 0.5f), (int)(cy - r.getWidth() * 0.5f),
                r.getHeight(), r.getWidth(),
                juce::Justification::centred, 1, 0.75f);
            g.restoreState();
        };

        const int slot1Bottom = (insertMidY > 0) ? insertMidY : (h - 4);
        drawSlotLabel("Main Insert 1", insTop, slot1Bottom);
        if (insertMidY > 0)
        {
            // Horizontal divider between the two insert slots
            g.setColour(borderCol.withAlpha(0.5f));
            g.drawLine((float)stripW, (float)insertMidY,
                       (float)(stripW + kInsertPanelW), (float)insertMidY, 1.0f);
            g.setColour(labelCol);
            drawSlotLabel("Main Insert 2", insertMidY, h - 4);
        }
    }

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
    const int n = MuLookAndFeel::kChannelPaletteSize;
    channelColour = MuLookAndFeel::channelPalette[((slotIndex % n) + n) % n];
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

void MixerChannel::bindClientMeter(std::atomic<float>& peak)
{
    vuMeter.getLevel = [&peak] { return peak.load(); };
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

    const int muteY = h - s(LF::kClientBottomGap) - s(LF::kClientButtonH);
    const int dbY   = muteY - s(LF::kClientDbGap) - s(LF::kClientDbH);
    const int faderH = juce::jmax(s(LF::kClientFaderMinH), dbY - y);

    const int vuW = s(kVUW);
    fader  .setBounds(0,       y, w - vuW, faderH);
    vuMeter.setBounds(w - vuW, y, vuW,     faderH);
    dbLabel.setBounds(0, dbY, w, s(LF::kClientDbH));

    const int hw = w / 2;
    muteBtn.setBounds(0,  muteY, hw,     s(LF::kClientButtonH));
    soloBtn.setBounds(hw, muteY, w - hw, s(LF::kClientButtonH));

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
    setShowInsertPanel(false);
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

void MixerChannel::setShowInsertPanel(bool show)
{
    if (! hasInsert()) return;
    showInsertPanel = show;

    // The insert controls exist from construction; hiding them keeps them out of the strip entirely.
    // Knob visibility on show again is re-derived by the next algorithm configure.
    insCharBox.setVisible(show);
    insCharBox2.setVisible(show);
    for (auto* k : { &insParam1, &insParam2, &insParam3, &insParam4,
                     &insParam1_2, &insParam2_2, &insParam3_2, &insParam4_2 })
        k->setVisible(show);
    resized();
    repaint();
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
    const int dbY = h - s(LF::kClientBottomGap) - s(LF::kClientButtonH) - s(LF::kClientDbGap) - s(LF::kClientDbH);
    const int faderH = juce::jmax(s(LF::kClientFaderMinH), dbY - top);

    const int vuW = s(kVUW);
    fader  .setBounds(0,       top, w - vuW, faderH);
    vuMeter.setBounds(w - vuW, top, vuW,     faderH);
    dbLabel.setBounds(0, dbY, w, s(LF::kClientDbH));

    sidechainPaneBounds = {};
    sendsPaneBounds     = {};
    faderPaneBounds     = { 1, top - 2, w - 2, h - top + 1 };
}
