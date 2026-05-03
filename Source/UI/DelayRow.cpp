#include "DelayRow.h"

// modeDropdown IDs: 1=Free, 2=1/32, 3=1/16, 4=1/8, 5=1/4
static int denomToDropdownId(int denom)
{
    switch (denom)
    {
        case 32: return 2;
        case 16: return 3;
        case  8: return 4;
        case  4: return 5;
        default: return 5;  // fall back to 1/4
    }
}

static int dropdownIdToDenom(int id)
{
    switch (id)
    {
        case 2: return 32;
        case 3: return 16;
        case 4: return  8;
        case 5: return  4;
        default: return 4;
    }
}

DelayRow::DelayRow()
{
    enableButton.setClickingTogglesState(true);
    enableButton.setToggleState(true, juce::dontSendNotification);
    enableButton.onClick = [this]
    {
        isEnabled = enableButton.getToggleState();
        if (onEnabledChanged) onEnabledChanged(isEnabled);
        repaint();
    };
    addAndMakeVisible(enableButton);

    modeDropdown.addItem("Free", 1);
    modeDropdown.addItem("1/32", 2);
    modeDropdown.addItem("1/16", 3);
    modeDropdown.addItem("1/8",  4);
    modeDropdown.addItem("1/4",  5);
    modeDropdown.setSelectedId(1, false);
    modeDropdown.onChange = [this](int id)
    {
        const bool nowSync = (id != 1);
        if (nowSync != syncMode)
        {
            syncMode = nowSync;
            updateModeVisibility();
            if (onSyncChanged) onSyncChanged(syncMode);
        }
        if (syncMode)
            fireSyncParams();
    };
    addAndMakeVisible(modeDropdown);

    modifierSegment.onChange = [this](int) { fireSyncParams(); };
    addAndMakeVisible(modifierSegment);

    countKnob.setRange(1.0, 8.0, 1.0);
    countKnob.setValue(1.0, juce::dontSendNotification);
    countKnob.onValueChanged = [this](double) { fireSyncParams(); };
    countKnob.onStatusUpdate = [this](const juce::String& n, const juce::String& v)
    {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    addAndMakeVisible(countKnob);

    msKnob.setRange(1.0, 4000.0, 1.0);
    msKnob.setValue(250.0, juce::dontSendNotification);
    msKnob.getSlider().setNumDecimalPlacesToDisplay(0);
    msKnob.onValueChanged = [this](double v)
    {
        if (onFreeMsChanged) onFreeMsChanged(static_cast<float>(v));
    };
    msKnob.onStatusUpdate = [this](const juce::String& n, const juce::String& v)
    {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    addAndMakeVisible(msKnob);

    feedbackKnob.setRange(0.0, 100.0, 1.0);
    feedbackKnob.setValue(40.0, juce::dontSendNotification);
    feedbackKnob.onValueChanged = [this](double v)
    {
        if (onFeedbackChanged) onFeedbackChanged(static_cast<float>(v) / 100.0f);
    };
    feedbackKnob.onStatusUpdate = [this](const juce::String& n, const juce::String& v)
    {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    addAndMakeVisible(feedbackKnob);

    spreadKnob.setRange(0.0, 100.0, 0.1);
    spreadKnob.setValue(0.0, juce::dontSendNotification);
    spreadKnob.onValueChanged = [this](double v)
    {
        if (onSpreadChanged) onSpreadChanged(static_cast<float>(v) / 100.0f);
    };
    spreadKnob.onStatusUpdate = [this](const juce::String& n, const juce::String& v)
    {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    addAndMakeVisible(spreadKnob);

    dirtKnob.setRange(0.0, 100.0, 0.1);
    dirtKnob.setValue(0.0, juce::dontSendNotification);
    dirtKnob.onValueChanged = [this](double v)
    {
        if (onDirtChanged) onDirtChanged(static_cast<float>(v) / 100.0f);
    };
    dirtKnob.onStatusUpdate = [this](const juce::String& n, const juce::String& v)
    {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    addAndMakeVisible(dirtKnob);

    updateModeVisibility();
}

void DelayRow::setEnabled(bool e, juce::NotificationType n)
{
    isEnabled = e;
    enableButton.setToggleState(e, n);
    repaint();
}

void DelayRow::setSyncMode(bool sync)
{
    syncMode = sync;
    if (!sync)
        modeDropdown.setSelectedId(1, false);
    else if (modeDropdown.getSelectedId() == 1)
        modeDropdown.setSelectedId(5, false);  // default to 1/4
    updateModeVisibility();
}

void DelayRow::setFreeMs(float ms)
{
    msKnob.setValue(static_cast<double>(ms), juce::dontSendNotification);
}

void DelayRow::setSyncParams(int denominator, bool dotted, bool triplet, int count)
{
    modeDropdown.setSelectedId(denomToDropdownId(denominator), false);
    modifierSegment.setSelectedIndex(dotted ? 1 : triplet ? 2 : 0, false);
    countKnob.setValue(static_cast<double>(count), juce::dontSendNotification);
}

void DelayRow::setFeedback(float v) { feedbackKnob.setValue(v * 100.0, juce::dontSendNotification); }
void DelayRow::setSpread(float v)   { spreadKnob.setValue(v * 100.0, juce::dontSendNotification); }
void DelayRow::setDirt(float v)     { dirtKnob.setValue(v * 100.0, juce::dontSendNotification); }

void DelayRow::updateModeVisibility()
{
    modifierSegment.setVisible(syncMode);
    countKnob      .setVisible(syncMode);
    msKnob         .setVisible(!syncMode);
    resized();
    repaint();
}

void DelayRow::fireSyncParams()
{
    if (!onSyncParamChanged) return;
    const int id          = modeDropdown.getSelectedId();
    const int denominator = dropdownIdToDenom(id);
    const int mod         = modifierSegment.getSelectedIndex();
    const bool dotted     = (mod == 1);
    const bool triplet    = (mod == 2);
    const int count       = juce::jmax(1, static_cast<int>(countKnob.getValue()));
    onSyncParamChanged(denominator, dotted, triplet, count);
}

void DelayRow::resized()
{
    const int h = getHeight();
    int x = kPad;

    enableButton.setBounds(x, (h - 22) / 2, kToggleW, 22);
    x += kToggleW + kPad;
    x += kNameW + kPad;  // name label is drawn, not a component

    modeDropdown.setBounds(x, (h - 24) / 2, kDropdownW, 24);
    x += kDropdownW + kPad;

    if (syncMode)
    {
        modifierSegment.setBounds(x, (h - 24) / 2, kModifierW, 24);  x += kModifierW + kPad;
        countKnob      .setBounds(x, 0,             kCountW,    h);   x += kCountW    + kPad;
    }
    else
    {
        msKnob.setBounds(x, 0, kMsW, h);
        x += kMsW + kPad;
    }

    feedbackKnob.setBounds(x,              0, kKnobW, h);
    spreadKnob  .setBounds(x + kKnobW,     0, kKnobW, h);
    dirtKnob    .setBounds(x + kKnobW * 2, 0, kKnobW, h);
}

void DelayRow::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillRect(getLocalBounds());

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.drawLine(0.0f, static_cast<float>(getHeight() - 1),
               static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1), 0.5f);

    const int nameX = kPad + kToggleW + kPad;
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    g.drawText("Delay", nameX, 0, kNameW, getHeight(), juce::Justification::centredLeft);

    if (!isEnabled)
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRect(getLocalBounds());
    }
}
