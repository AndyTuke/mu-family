#include "FXRow.h"

FXRow::FXRow(const juce::String& name,
             const std::vector<FXAlgorithmDef>& algorithms,
             MuClidLookAndFeel::ColourIds colour)
    : slotName(name), algorithmDefs(algorithms), knobColour(colour)
{
    addAndMakeVisible(enableButton);
    enableButton.setClickingTogglesState(true);
    enableButton.setToggleState(true, juce::dontSendNotification);
    enableButton.onClick = [this]
    {
        isEnabled = enableButton.getToggleState();
        if (onEnabledChanged) onEnabledChanged(isEnabled);
    };

    addAndMakeVisible(algorithmDropdown);
    int id = 1;
    for (auto& def : algorithmDefs)
        algorithmDropdown.addItem(def.name, id++);

    algorithmDropdown.setSelectedId(1, juce::dontSendNotification);
    algorithmDropdown.onChange = [this](int selectedId)
    {
        currentAlgorithm = selectedId - 1;
        rebuildKnobs(currentAlgorithm);
        if (onAlgorithmChanged) onAlgorithmChanged(currentAlgorithm);
        resized();
        repaint();
    };

    if (!algorithmDefs.empty())
        rebuildKnobs(0);
}

void FXRow::setEnabled(bool enabled, juce::NotificationType n)
{
    isEnabled = enabled;
    enableButton.setToggleState(enabled, n);
}

void FXRow::setSelectedAlgorithm(int index, juce::NotificationType n)
{
    const int clamped = juce::jlimit(0, (int)algorithmDefs.size() - 1, index);

    // No-op when the algorithm hasn't actually changed. Critical: this method is
    // called from MixerOverlay::loadFromAPVTS on every APVTS change, and the old
    // unconditional rebuildKnobs() destroyed the slider the user was actively
    // dragging — the user's first turn would register, then the rebuild would
    // kill mouse capture and the rest of the drag went nowhere.
    if (clamped == currentAlgorithm) return;

    currentAlgorithm = clamped;
    algorithmDropdown.setSelectedId(currentAlgorithm + 1, false);
    rebuildKnobs(currentAlgorithm);

    if (n == juce::sendNotification && onAlgorithmChanged)
        onAlgorithmChanged(currentAlgorithm);

    resized();
    repaint();
}

void FXRow::setParamValue(const juce::String& id, float value)
{
    for (auto& k : knobs)
    {
        if (k->getSlider().getName() == id)
        {
            k->setValue(static_cast<double>(value), juce::dontSendNotification);
            return;
        }
    }
}

void FXRow::hideParameter(const juce::String& id)
{
    if (! hiddenParamIds.contains(id))
        hiddenParamIds.add(id);
    rebuildKnobs(currentAlgorithm);
    resized();
}

void FXRow::resized()
{
    const int h = getHeight();
    int x = kPadding;

    enableButton.setBounds(x, (h - 22) / 2, kToggleW, 22);
    x += kToggleW + kPadding;

    // Name label area is painted, not a component — advance x past it.
    x += kNameW + kPadding;

    algorithmDropdown.setBounds(x, (h - 24) / 2, kDropdownW, 24);
    x += kDropdownW + kPadding;

    for (auto& k : knobs)
    {
        k->setBounds(x, 0, kKnobW, h);
        x += kKnobW;
    }
}

void FXRow::paint(juce::Graphics& g)
{
    // Row background
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillRect(getLocalBounds());

    // Bottom separator line
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.drawLine(0.0f, static_cast<float>(getHeight() - 1),
               static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1), 0.5f);

    // Slot name label
    const int nameX = kPadding + kToggleW + kPadding;
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    g.drawText(slotName, nameX, 0, kNameW, getHeight(), juce::Justification::centredLeft);

    // Dim overlay when disabled
    if (!isEnabled)
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRect(getLocalBounds());
    }
}

void FXRow::rebuildKnobs(int algorithmIndex)
{
    for (auto& k : knobs)
        removeChildComponent(k.get());
    knobs.clear();

    if (algorithmIndex < 0 || algorithmIndex >= (int)algorithmDefs.size())
        return;

    const auto& def = algorithmDefs[algorithmIndex];
    for (auto& param : def.params)
    {
        if (hiddenParamIds.contains(param.id)) continue; // e.g. "mix" hidden in send/return rows
        auto knob = std::make_unique<KnobWithLabel>(param.name, knobColour);
        knob->setRange(param.minVal, param.maxVal);
        knob->setValue(param.defaultVal, juce::dontSendNotification);
        knob->getSlider().setName(param.id);

        if (param.units == "Hz")
        {
            knob->getSlider().textFromValueFunction = [](double v) -> juce::String {
                if (v < 1000.0)  return juce::String((int)v) + " Hz";
                if (v < 10000.0) return juce::String(v / 1000.0, 2) + " kHz";
                return juce::String(v / 1000.0, 1) + " kHz";
            };
            knob->getSlider().valueFromTextFunction = [](const juce::String& t) -> double {
                const juce::String s = t.trim().toLowerCase();
                if (s.containsIgnoreCase("khz"))
                    return s.getDoubleValue() * 1000.0;
                return s.getDoubleValue();
            };
        }
        else
        {
            knob->getSlider().setNumDecimalPlacesToDisplay(0);
        }

        knob->onStatusUpdate = [this](const juce::String& n, const juce::String& v)
        {
            if (onStatusUpdate) onStatusUpdate(n, v);
        };

        knob->onValueChanged = [this, id = param.id](double value)
        {
            if (onParamChanged) onParamChanged(id, static_cast<float>(value));
        };

        addAndMakeVisible(*knob);
        knobs.push_back(std::move(knob));
    }
}
