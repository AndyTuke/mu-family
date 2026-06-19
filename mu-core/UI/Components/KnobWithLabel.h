#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include "MuLookAndFeel.h"
#include "Modulation/ModulationMatrix.h"

// Rotary slider + category colour + label below + optional status bar callback.
class KnobWithLabel : public juce::Component, private juce::Timer
{
public:
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(double value)> onValueChanged;

    KnobWithLabel(const juce::String& label,
                  MuLookAndFeel::ColourIds categoryColour = MuLookAndFeel::knobEuclidean);
    ~KnobWithLabel() override { stopTimer(); }

    juce::Slider& getSlider() noexcept { return slider; }

    void setRange(double min, double max, double step = 0.0);
    void setValue(double v, juce::NotificationType n = juce::dontSendNotification);
    double getValue() const;
    void setLabel(const juce::String& newLabel);

    // Modulation indicator. setIsModulated draws a static tinted ring
    // around the knob whenever any modulation assignment targets this destination.
    // setModulatedNorm (0..1) additionally draws an animated secondary arc tracking
    // the live modulated value; pass NaN to disable.
    //
    // setModulatedActual takes the modulator's actual (un-normalised) value in
    // the slider's display range — internally converts via
    // `slider.valueToProportionOfLength(...)` so the arc respects whatever skew
    // the slider uses (log / midPoint / linear) and lines up with the needle.
    // Prefer this over setModulatedNorm whenever the snapshot stores a real
    // value rather than a pre-normalised 0..1 — guarantees the arc matches the
    // visual slider scale even on log-skewed knobs (cutoff, LPF, rate, etc.).
    void setIsModulated(bool b);
    void setModulatedNorm(float norm01);
    void setModulatedActual(float actualValue) noexcept;

    // Declarative mod binding — the knob polls the matrix at 30 Hz and drives
    // its own rings without any external refreshModulatedIndicators() call.
    //
    // destId:      the ModulationMatrix destination string for this knob.
    // matrix:      the matrix to query for assignment state; may be null (rings stay off).
    // liveValueFn: returns the current modulated value each timer tick.
    //              Use actual display units (Hz, dB, semitones, etc.) unless normMode=true.
    //              Return NaN when the sequencer isn't playing — clears the live arc.
    // normMode:    when true liveValueFn returns 0..1 proportion → setModulatedNorm;
    //              when false (default) it returns actual display units → setModulatedActual.
    //
    // Calling bindModulation() again re-binds (e.g. when setRhythm / setVoice fires).
    // clearModBinding() removes the binding and stops the ring.
    void bindModulation(const char*             destId,
                        const ModulationMatrix* matrix,
                        std::function<float()>  liveValueFn,
                        bool                    normMode = false);
    void clearModBinding() noexcept;

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
    MuLookAndFeel::ColourIds knobColour;

    bool  isModulated   = false;
    float modulatedNorm = std::numeric_limits<float>::quiet_NaN();

    bool                    hasModBind   = false;
    bool                    modNormMode  = false;
    std::string             modDestId;
    const ModulationMatrix* modMatrix    = nullptr;
    std::function<float()>  modLiveValue;
    int                     lastModRevision   = -1;     // matrix revision the flag below was computed at
    bool                    modAssignedCached = false;  // cached "is this destination modulated?"

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
