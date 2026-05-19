// split into 3 partial-class TUs (this file + MixerChannel_Bindings.cpp +
// MixerChannel_Insert.cpp) — was 1107 lines, the largest file in the codebase.
// This TU keeps the ctor, paint/resized layout code, and the small status helpers.
// kInsertDefaults table moved to MixerChannel_Insert.cpp (its only consumer).

#include "MixerChannel.h"
#include "../Plugin/PluginProcessor.h"
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

        auto noVal = [](double) { return juce::String(); };
        for (auto* k : { &insDrive, &insDrive2 })
        {
            k->setRange(0.0, 100.0, 0.1);
            k->setValue(0.0);
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insOutput, &insOutput2 })
        {
            k->setRange(-24.0, 0.0, 0.1);
            k->setValue(0.0);
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insTone, &insTone2 })
        {
            k->setRange(20.0, 20000.0, 1.0);
            k->setValue(20000.0);
            k->getSlider().setSkewFactorFromMidPoint(640.0);
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insExtra, &insExtra2 })
        {
            k->setRange(200.0, 8000.0, 1.0);
            k->getSlider().setSkewFactorFromMidPoint(1000.0);
            k->setValue(1000.0);
            k->getSlider().textFromValueFunction = noVal;
            k->setVisible(false);
            addAndMakeVisible(*k);
        }
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
    const int w      = getWidth();
    const int h      = getHeight();

    // Master: right portion is the insert panel; everything else uses the strip width.
    const int insW   = hasInsert() ? kInsertPanelW : 0;
    const int stripW = w - insW;

    const int nameBottom = kColourBarH + kNameH;   // y=25

    // ── Sidechain section (Rhythm + Returns): ~20% of strip height, min = kSidechainH ──
    const int scH = hasSidechainControls()
        ? juce::jmax(kSidechainH, juce::roundToInt(h * 0.20f))
        : 0;
    if (hasSidechainControls())
    {
        const int scRemain  = scH - kScSrcH;
        const int scAmtH_l  = juce::jmax(kScAmtH, juce::roundToInt(scRemain * 0.55f));
        const int scEnvH_l  = scH - kScSrcH - scAmtH_l;

        scSourceBox.setBounds(0, nameBottom, stripW, kScSrcH);
        scAmount   .setBounds(0, nameBottom + kScSrcH, stripW, scAmtH_l);
        const int hw = stripW / 2;
        scAttack .setBounds(0,  nameBottom + kScSrcH + scAmtH_l, hw,          scEnvH_l);
        scRelease.setBounds(hw, nameBottom + kScSrcH + scAmtH_l, stripW - hw, scEnvH_l);

        sidechainPaneBounds = { 1, nameBottom + 1, stripW - 2, scH - 2 };
    }
    else
    {
        sidechainPaneBounds = {};
    }

    // ── Sends + pan: ~35% of strip height ────────────────────────────────────
    const int sendY  = nameBottom + scH;
    const int spH    = juce::jmax(4 * 36, juce::roundToInt(h * 0.35f));
    const int sendH  = spH / 4;
    const int panH   = spH - 3 * sendH;
    const int faderY = sendY + spH;

    // A return channel cannot send to itself (would feedback-loop). #429.
    sendEffect.setVisible(hasSends() && channelType == Type::Rhythm);
    sendDelay .setVisible(hasSends() && channelType != Type::DelayReturn
                                     && channelType != Type::ReverbReturn);
    sendReverb.setVisible(hasSends() && channelType != Type::ReverbReturn);

    sendEffect.setBounds(0, sendY,             stripW, sendH);
    sendDelay .setBounds(0, sendY + sendH,     stripW, sendH);
    sendReverb.setBounds(0, sendY + sendH * 2, stripW, sendH);
    panKnob   .setBounds(0, sendY + sendH * 3, stripW, panH);

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
    const int muteY   = hasMuteSolo() ? h - kButtonH : h;
    const int dbY     = muteY - kDbH;
    // outBus sits just above the dB label on Rhythm channels.
    const int busY    = hasOutputBus() ? dbY - kOutBusH : dbY;
    const int faderEnd = hasOutputBus() ? busY - 2 : dbY - 2;
    const int faderH  = juce::jmax(40, faderEnd - faderY);

    const int grW = hasSidechainControls() ? kGRW : 0;
    fader.setBounds  (0,                        faderY, stripW - kVUW - grW, faderH);
    if (hasSidechainControls())
        grMeter.setBounds(stripW - kVUW - grW,  faderY, grW,                 faderH);
    vuMeter.setBounds(stripW - kVUW,            faderY, kVUW,                faderH);

    if (hasOutputBus())
        outBusBox.setBounds(0, busY, stripW, kOutBusH);

    dbLabel.setBounds(0, dbY, stripW, kDbH);

    if (hasMuteSolo())
    {
        const int hw = stripW / 2;
        muteBtn.setBounds(0,  muteY, hw,          kButtonH);
        soloBtn.setBounds(hw, muteY, stripW - hw, kButtonH);
    }

    faderPaneBounds = { 1, faderY - 2, stripW - 2, h - faderY + 1 };

    // ── Insert panels (Master channel, right of strip) — two slots stacked top/bottom ─
    if (hasInsert())
    {
        const int pad    = 4;
        const int ipX    = stripW + pad;
        const int ipW    = insW - 2 * pad;
        const int insTop = nameBottom;
        const int insH   = h - insTop - pad;
        const int halfH  = insH / 2;
        insertMidY = insTop + halfH;

        // Both slots reserve kNameH for their own label, giving equal knob space.
        const int slot1CharY = insTop + kNameH + pad;
        const int slot2CharY = insertMidY + kNameH + pad;

        auto layoutSlot = [&](int charBoxY, int endY,
                               juce::ComboBox& charBox,
                               KnobWithLabel& drv, KnobWithLabel& out,
                               KnobWithLabel& ton, KnobWithLabel& ext)
        {
            charBox.setBounds(ipX, charBoxY, ipW, kInsCharH);
            const int ky      = charBoxY + kInsCharH + 2;
            const int availH  = endY - ky - 2;

            if (charBox.getSelectedId() == 7) // EQ: single column High/Mid/MidHz/Low
            {
                const int rowH = juce::jmin(60, availH / 4);
                KnobWithLabel* const eqOrder[] = { &ton, &out, &ext, &drv };
                for (int i = 0; i < 4; ++i)
                    eqOrder[i]->setBounds(ipX, ky + i * rowH, ipW, rowH);
            }
            else
            {
                KnobWithLabel* vis[4];
                int nVis = 0;
                KnobWithLabel* const knobs[] = { &drv, &out, &ton, &ext };
                for (auto* k : knobs)
                    if (k->isVisible()) vis[nVis++] = k;

                if (nVis > 0)
                {
                    const int nRows = (nVis + 1) / 2;
                    const int rowH  = juce::jmin(60, availH / nRows);
                    const int halfW = ipW / 2;

                    for (int i = 0; i < nVis; ++i)
                    {
                        const bool isLastOdd = (i == nVis - 1) && (nVis % 2 == 1);
                        const int kw = isLastOdd ? ipW : halfW;
                        const int kx = ipX + (i % 2) * halfW;
                        vis[i]->setBounds(kx, ky + (i / 2) * rowH, kw, rowH);
                    }
                }
            }
        };

        layoutSlot(slot1CharY, insertMidY,  insCharBox,  insDrive,  insOutput,  insTone,  insExtra);
        layoutSlot(slot2CharY, h - pad,     insCharBox2, insDrive2, insOutput2, insTone2, insExtra2);
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

    // Colour bar and name
    g.setColour(channelColour);
    g.fillRect(0, 0, w, kColourBarH);

    const int stripW = w - (hasInsert() ? kInsertPanelW : 0);

    g.setColour(active ? MuClidLookAndFeel::colour(Id::headingText)
                       : MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(channelName, 0, kColourBarH, stripW, kNameH,
               juce::Justification::centred, true);

    // Right-edge channel divider
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float)(w - 1), 0.0f, (float)(w - 1), (float)h, 0.5f);

    // ── Section pane borders ──────────────────────────────────────────────────
    const juce::Colour borderCol = MuClidLookAndFeel::colour(Id::segmentInactiveBorder)
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
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    if (!sidechainPaneBounds.isEmpty() && hasOutputBus())  // "SC" tag only on rhythm strips (others have no output bus label)
        g.drawText("SC", sidechainPaneBounds.getRight() - 14, sidechainPaneBounds.getY() + 1,
                   12, 10, juce::Justification::centredRight, false);

    // ── Insert panel (Master: right portion) ─────────────────────────────────
    if (hasInsert())
    {
        // Vertical separator between strip and insert panel
        g.setColour(borderCol.withAlpha(0.6f));
        g.drawLine((float)stripW, (float)kColourBarH, (float)stripW, (float)h, 1.0f);

        // Colour bar continuation
        g.setColour(channelColour);
        g.fillRect(stripW, 0, kInsertPanelW, kColourBarH);

        // "Main Insert 1/2" labels sit inside each half; shared name row is clear.
        const juce::Colour labelCol = active ? MuClidLookAndFeel::colour(Id::headingText)
                                             : MuClidLookAndFeel::colour(Id::mutedText);
        const int insTop = kColourBarH + kNameH;  // start of insert content area
        g.setColour(labelCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("Main Insert 1", stripW, insTop, kInsertPanelW, kNameH,
                   juce::Justification::centred, false);
        if (insertMidY > 0)
        {
            // Horizontal divider between the two insert slots
            g.setColour(borderCol.withAlpha(0.5f));
            g.drawLine((float)stripW, (float)insertMidY,
                       (float)(stripW + kInsertPanelW), (float)insertMidY, 1.0f);
            // "Main Insert 2" label at the top of slot 2
            g.setColour(labelCol);
            g.drawText("Main Insert 2", stripW, insertMidY, kInsertPanelW, kNameH,
                       juce::Justification::centred, false);
        }
    }

    // Inactive overlay
    if (!active)
    {
        g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::backgroundMixerStripDim));
        g.fillRect(0, kColourBarH + kNameH, w, h - kColourBarH - kNameH);
    }
}
