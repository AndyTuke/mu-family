// split into 3 partial-class TUs (this file + MixerChannel_Bindings.cpp +
// MixerChannel_Insert.cpp). This TU keeps the ctor, paint/resized layout code,
// and the small status helpers. kInsertDefaults lives in MixerChannel_Insert.cpp
// (its only consumer).

#include "MixerChannel.h"
#include "Plugin/ProcessorBase.h"
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

    if (hasInsert())
    {
        auto addInsertCombo = [](juce::ComboBox& box) {
            box.addItem("None",        1);
            box.addItem("3-Band EQ",   7);
            box.addItem("Bitcrusher",  5);
            box.addItem("Clipper",     6);
            box.addItem("Compressor",  8);
            box.addItem("Fold",        4);
            box.addItem("Hard Clip",   3);
            box.addItem("Karplus",    12);
            box.addItem("Limiter",     9);
            box.addItem("Ring Mod",   10);
            box.addItem("Soft Clip",   2);
            box.addItem("Tape Sat",   11);
            box.addItem("Vocoder",    13);
            box.addItem("Vocoder St", 14);
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
    const int w      = getWidth();
    const int h      = getHeight();

    // Master: right portion is the insert panel; everything else uses the strip width.
    const int insW   = hasInsert() ? s(kInsertPanelW) : 0;
    const int stripW = w - insW;

    // Name area total Y: top padding + name pill + bottom padding. Symmetric
    // so the rhythm-colour pill border has equal breathing room above and
    // below.
    const int nameBottom = s(kNamePadding * 2 + kNameH);

    // ── Sidechain section — slot always reserved for vertical alignment ──
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

    // ── Sends + pan — fixed Medium-baseline height ───────────────────────────
    const int sendY  = nameBottom + scH;
    const int spH   = s(kSendsAreaH);
    const int sendH = s(kSendKnobH);
    const int faderY = sendY + spH;

    // A return channel cannot send to itself (would feedback-loop). #429.
    sendEffect.setVisible(hasSends() && channelType == Type::Rhythm);
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
        const int firstOff = (channelType == Type::Rhythm)      ? 0
                           : (channelType == Type::EffectReturn) ? sendH
                           : (channelType == Type::DelayReturn)  ? sendH * 2
                           : sendH * 3;  // ReverbReturn / Master: pan only
        const int paneH = spH - firstOff;
        sendsPaneBounds = (paneH > 0) ? juce::Rectangle<int>{ 1, sendY + firstOff + 1,
                                                               stripW - 2, paneH - 2 }
                                      : juce::Rectangle<int>{};
    }

    // ── Fader + VU + GR ───────────────────────────────────────────────────────
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

    // ── Insert panels (Master channel, right of strip) — two slots stacked top/bottom ─
    if (hasInsert())
    {
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

            if (charBox.getSelectedId() == 7) // EQ: stacked top→bottom = P4 / P2 / P3 / P1
            {
                const int knobX = ipX + (ipW - s2W) / 2;  // centre horizontally
                // Frequency-descending: P4(High dB) / P2(Mid dB) / P3(Mid Hz) / P1(Low dB)
                KnobWithLabel* const eqOrder[] = { &p4, &p2, &p3, &p1 };
                for (int i = 0; i < 4; ++i)
                    eqOrder[i]->setBounds(knobX, ky + i * s2H, s2W, s2H);
            }
            else
            {
                // Pack the visible slots into a 2×2 grid; centre any odd last knob.
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
            }
        };

        layoutSlot(slot1CharY, insertMidY,  insCharBox,  insParam1,   insParam2,   insParam3,   insParam4);
        layoutSlot(slot2CharY, h - pad,     insCharBox2, insParam1_2, insParam2_2, insParam3_2, insParam4_2);
    }
}
void MixerChannel::setEffectSendLabel(const juce::String& name)
{
    sendEffect.setLabel(name);
}

void MixerChannel::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    // Rhythm colour goes JUST around the name — a rounded outline framing the
    // name area. kNamePadding gives equal breathing room on all four sides
    // of the kNameH-tall pill so the rhythm-colour border doesn't sit on
    // the strip edge.
    const int stripW = w - (hasInsert() ? kInsertPanelW : 0);

    g.setColour(active ? MuLookAndFeel::colour(Id::headingText)
                       : MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(channelName, 0, kNamePadding, stripW, kNameH,
               juce::Justification::centred, true);

    // Rhythm-colour border around the name pill (rhythm strips only — returns
    // and master keep their fixed colour scheme since they're not per-rhythm).
    if (active && channelType == Type::Rhythm)
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
    if (hasInsert())
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
