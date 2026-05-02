#include "MixerOverlay.h"

MixerOverlay::MixerOverlay(PluginProcessor& p, MixerEngine& m)
    : proc(p), mixer(m),
      effectRow("Effect", FXAlgorithmRegistry::effectAlgorithms(), MuClidLookAndFeel::knobFxSend),
      reverbRow("Reverb", FXAlgorithmRegistry::reverbAlgorithms(), MuClidLookAndFeel::knobReverb)
{
    buildRhythmChannels();
    wireReturns();
    wireFXRows();

    addAndMakeVisible(effectReturn);
    addAndMakeVisible(delayReturn);
    addAndMakeVisible(reverbReturn);
    addAndMakeVisible(masterChannel);
    addAndMakeVisible(effectRow);
    addAndMakeVisible(delayRow);
    addAndMakeVisible(reverbRow);
}

void MixerOverlay::buildRhythmChannels()
{
    for (auto& ch : rhythmChannels)
        removeChildComponent(ch.get());
    rhythmChannels.clear();

    const auto& palette = MuClidLookAndFeel::rhythmPalette;
    for (int r = 0; r < proc.getNumRhythms(); ++r)
    {
        const Rhythm& rhythm = proc.getRhythm(r);
        juce::Colour col = palette[rhythm.colourIndex % 30];
        auto ch = std::make_unique<MixerChannel>(MixerChannel::Type::Rhythm,
                                                  juce::String(rhythm.name), col);
        ch->bindRhythm(mixer.channels[r], mixer.channelPeaks[r]);
        addAndMakeVisible(*ch);
        rhythmChannels.push_back(std::move(ch));
    }
}

void MixerOverlay::wireReturns()
{
    effectReturn.bindReturn(mixer.returns[0], mixer.returnPeaks[0]);
    delayReturn .bindReturn(mixer.returns[1], mixer.returnPeaks[1]);
    reverbReturn.bindReturn(mixer.returns[2], mixer.returnPeaks[2]);
    masterChannel.bindMaster(mixer);
}

void MixerOverlay::wireFXRows()
{
    auto& eff = proc.fxChain.effectSlot();
    effectRow.setEnabled(true);
    effectRow.setSelectedAlgorithm(eff.getAlgorithmIndex());
    effectRow.onAlgorithmChanged = [&eff](int idx) { eff.setAlgorithm(idx); };
    effectRow.onParamChanged     = [&eff](const juce::String& id, float v) { eff.setParam(id, v); };
    effectRow.onEnabledChanged   = [&eff](bool e) { juce::ignoreUnused(e); };  // EffectSlot always on

    auto& dly = proc.fxChain.delaySlot();
    delayRow.setEnabled(dly.isEnabled());
    delayRow.setSyncMode(dly.getTimeMode() == DelaySlot::TimeMode::Sync);
    delayRow.onEnabledChanged  = [&dly](bool e)  { dly.setEnabled(e); };
    delayRow.onSyncChanged     = [&dly](bool sync)
    {
        dly.setTimeMode(sync ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free);
    };
    delayRow.onSyncParamChanged = [&dly](int denom, bool dotted, bool triplet, int count)
    {
        dly.setTimeDivision(denom, dotted, triplet);
        dly.setTimeCount(count);
    };
    delayRow.onFreeMsChanged   = [&dly](float ms) { dly.setDelayMs(ms); };
    delayRow.onFeedbackChanged = [&dly](float v)  { dly.setFeedback(v); };
    delayRow.onSpreadChanged   = [&dly](float v)  { dly.setSpread(v); };
    delayRow.onDirtChanged     = [&dly](float v)  { dly.setDirt(v); };

    auto& rev = proc.fxChain.reverbSlot();
    reverbRow.setEnabled(rev.isEnabled());
    reverbRow.setSelectedAlgorithm(rev.getAlgorithmIndex());
    reverbRow.onAlgorithmChanged = [&rev](int idx) { rev.setAlgorithm(idx); };
    reverbRow.onEnabledChanged   = [&rev](bool e)  { rev.setEnabled(e); };
    reverbRow.onParamChanged     = [&rev](const juce::String& id, float v) { rev.setParam(id, v); };
}

void MixerOverlay::refresh()
{
    buildRhythmChannels();
    resized();
    repaint();
}

void MixerOverlay::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int fxAreaH = kFXRowH * 3;
    const int stripH  = juce::jmax(200, h - fxAreaH);

    // Channel strips: rhythms | divider | returns (3) | divider | master
    int x = 0;
    for (auto& ch : rhythmChannels)
    {
        ch->setBounds(x, 0, kChanW, stripH);
        x += kChanW;
    }

    x += kDivW;
    effectReturn .setBounds(x, 0, kChanW, stripH);  x += kChanW;
    delayReturn  .setBounds(x, 0, kChanW, stripH);  x += kChanW;
    reverbReturn .setBounds(x, 0, kChanW, stripH);  x += kChanW;

    x += kDivW;
    masterChannel.setBounds(x, 0, kMasterW, stripH);

    // FX rows below
    int fy = stripH;
    effectRow.setBounds(0, fy, w, kFXRowH);  fy += kFXRowH;
    delayRow .setBounds(0, fy, w, kFXRowH);  fy += kFXRowH;
    reverbRow.setBounds(0, fy, w, kFXRowH);
}

void MixerOverlay::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillAll();

    // Dividers between rhythm channels and returns, and before master
    const int stripH  = juce::jmax(200, getHeight() - kFXRowH * 3);
    int divX = (int)rhythmChannels.size() * kChanW;

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.fillRect(divX, 0, kDivW, stripH);

    divX += kDivW + kChanW * 3;
    g.fillRect(divX, 0, kDivW, stripH);
}
