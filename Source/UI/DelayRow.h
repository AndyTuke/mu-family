#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/KnobWithLabel.h"
#include "Components/DropdownSelect.h"
#include "Components/SegmentControl.h"

// Custom delay FX row for the mixer overlay.
// Mode dropdown: Free / 1/32 / 1/16 / 1/8 / 1/4.
// Sync mode: mode dropdown + Str/Dot/Trip modifier + count knob.
// Free mode: mode dropdown + millisecond time knob (modifier/count hidden, controls shift left).
// Feedback, spread, and dirt knobs are always visible on the right.
class DelayRow : public juce::Component
{
public:
    std::function<void(bool)>   onEnabledChanged;
    std::function<void(bool)>   onSyncChanged;
    std::function<void(int denominator, bool dotted, bool triplet, int count)> onSyncParamChanged;
    std::function<void(float ms)> onFreeMsChanged;
    std::function<void(float)>  onFeedbackChanged;
    std::function<void(float)>  onSpreadChanged;
    std::function<void(float)>  onDirtChanged;
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

    DelayRow();

    void setEnabled(bool e, juce::NotificationType n = juce::dontSendNotification);

    // When false, hides the On button and name label so the row can be positioned
    // immediately after another row's header (used for echoRow in Echo mode).
    void setShowHeader(bool show);

    void setSyncMode(bool sync);
    void setFreeMs(float ms);
    void setSyncParams(int denominator, bool dotted, bool triplet, int count);
    void setFeedback(float v);
    void setSpread(float v);
    void setDirt(float v);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using LafId = MuClidLookAndFeel::ColourIds;

    void updateModeVisibility();
    void fireSyncParams();

    juce::TextButton enableButton { "On" };

    // Mode dropdown: id 1=Free, 2=1/32, 3=1/16, 4=1/8, 5=1/4
    DropdownSelect modeDropdown;

    // Sync mode controls
    SegmentControl modifierSegment { { "Straight", "Dotted", "Triplet" } };
    KnobWithLabel  multipleKnob { "Multiple", LafId::knobFxSend };

    // Free mode control
    KnobWithLabel msKnob { "Time (ms)", LafId::knobFxSend };

    // Always-visible params
    KnobWithLabel feedbackKnob { "Feedback", LafId::knobFxSend };
    KnobWithLabel spreadKnob   { "Spread",   LafId::knobFxSend };
    KnobWithLabel dirtKnob     { "Dirt",     LafId::knobFxSend };

    bool syncMode   = false;
    bool isEnabled  = true;
    bool showHeader = true;

    static constexpr int kToggleW   = 36;
    static constexpr int kNameW     = 60;
    static constexpr int kDropdownW = 120;  // same as FXRow algorithm dropdowns
    static constexpr int kPad       = 4;
    // Knob width is computed at resize time via MuLookAndFeel::knobSizeLargeFor
    // so it tracks the plugin window. See DelayRow::resized().
};
