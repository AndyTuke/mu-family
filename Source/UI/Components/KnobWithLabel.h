#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// Rotary slider + category colour + label below + optional status bar callback.
class KnobWithLabel : public juce::Component, private juce::Timer
{
public:
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(double value)> onValueChanged;

    KnobWithLabel(const juce::String& label,
                  MuClidLookAndFeel::ColourIds categoryColour = MuClidLookAndFeel::knobEuclidean);
    ~KnobWithLabel() override { stopTimer(); }

    juce::Slider& getSlider() noexcept { return slider; }

    void setRange(double min, double max, double step = 0.0);
    void setValue(double v, juce::NotificationType n = juce::dontSendNotification);
    double getValue() const;
    void setLabel(const juce::String& newLabel);

    // Issue #133: modulation indicator. setIsModulated draws a static tinted ring
    // around the knob whenever any modulation assignment targets this destination.
    // setModulatedNorm (0..1) additionally draws an animated secondary arc tracking
    // the live modulated value; pass NaN to disable.
    void setIsModulated(bool b);
    void setModulatedNorm(float norm01);

    // GR meter overlay (compressor/limiter): set to audio-thread-written atomic;
    // knob polls at 30 Hz and draws an orange arc showing gain reduction (0..1,
    // where 1 ≡ 24 dB). Pass nullptr to disable.
    void setGRSource(const std::atomic<float>* gr);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    juce::Slider slider;
    juce::String labelText;
    MuClidLookAndFeel::ColourIds knobColour;

    bool  isModulated   = false;
    float modulatedNorm = std::numeric_limits<float>::quiet_NaN();

    // when true, suppress onValueChanged dispatch from the slider's
    // onValueChange lambda. Set transiently inside setRange so JUCE's setRange
    // value-clip cannot cascade into a spurious APVTS write.
    bool  settingRange  = false;

    const std::atomic<float>* grSource  = nullptr;
    float                      grDisplay = 0.0f;
    static constexpr float     kGRRelease = 0.85f;

    std::unique_ptr<juce::TextEditor> inlineEditor;

    void showInlineEditor();
    void timerCallback() override;
};
