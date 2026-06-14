#include "MasterLoopSection.h"
#include "UI/Components/MuLookAndFeel.h"

MasterLoopSection::MasterLoopSection(ProcessorBase& p)
    : proc(p)
{
    loopLabel.setText("Loop:", juce::dontSendNotification);
    loopLabel.setJustificationType(juce::Justification::centredRight);
    loopLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(loopLabel);

    loopDropdown.addItem(juce::String::charToString(0x221E), 1); // ∞ = pattern reset length disabled
    for (int i = 1; i <= 16; ++i)
        loopDropdown.addItem(juce::String(i * 16) + " steps", i + 1);
    loopDropdown.setSelectedId(1, false); // default: off
    loopDropdown.onChange = [this](int id)
    {
        const int paramVal = id - 1; // id=1→0 (off), id=2→1 (16 steps), ...
        if (auto* param = proc.apvts.getParameter("mstrLoop"))
            param->setValueNotifyingHost(param->convertTo0to1((float)paramVal));
        loopStepLabel.setVisible(id != 1);
        if (onStatusUpdate) onStatusUpdate("Master Loop", loopDropdown.getText());
    };
    addAndMakeVisible(loopDropdown);

    loopStepLabel.setJustificationType(juce::Justification::centredLeft);
    loopStepLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    syncLoopDropdownFromAPVTS();
    addAndMakeVisible(loopStepLabel);

    // Catch host automation of mstrLoop so the dropdown / step label stay in
    // sync with the DAW state.
    proc.apvts.addParameterListener("mstrLoop", this);
    startTimerHz(30);
}

MasterLoopSection::~MasterLoopSection()
{
    proc.apvts.removeParameterListener("mstrLoop", this);
    stopTimer();
}

void MasterLoopSection::syncLoopDropdownFromAPVTS()
{
    const int paramVal = (int) proc.apvts.getRawParameterValue("mstrLoop")->load();
    loopStepLabel.setVisible(paramVal > 0);
    loopDropdown.setSelectedId(paramVal + 1, false);
}

void MasterLoopSection::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID != "mstrLoop") return;

    // host automation can fire on the audio thread; juce::Slider / DropdownSelect
    // state isn't safe to mutate off the message thread.
    juce::Component::SafePointer<MasterLoopSection> safe(this);
    auto refresh = [safe] { if (auto* self = safe.getComponent()) self->syncLoopDropdownFromAPVTS(); };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        refresh();
    else
        juce::MessageManager::callAsync(std::move(refresh));
}

void MasterLoopSection::timerCallback()
{
    // Reconcile the loop dropdown with the mstrLoop param every tick. A preset
    // load sets the param via the APVTS, but if that doesn't fire our listener
    // (value unchanged, or the editor opened with a preset already loaded) the
    // dropdown could otherwise show a stale value. Self-healing keeps the display honest.
    if (const auto* param = proc.apvts.getRawParameterValue("mstrLoop"))
    {
        const int paramVal = (int) param->load();
        if (loopDropdown.getSelectedId() != paramVal + 1)
            syncLoopDropdownFromAPVTS();
    }

    if (loopStepLabel.isVisible())
    {
        const int steps   = proc.getMasterLoopSteps();
        const int current = proc.getMasterLoopCurrentStep() + 1;
        // Only rebuild the "n / m" string when it actually changes — avoids a
        // per-tick heap allocation when the counter is paused or unchanged.
        if (current != lastShownStep || steps != lastShownSteps)
        {
            lastShownStep  = current;
            lastShownSteps = steps;
            loopStepLabel.setText(juce::String(current) + " / " + juce::String(steps),
                                  juce::dontSendNotification);
        }
    }
}

void MasterLoopSection::resized()
{
    using mu_ui::s;
    const int h    = getHeight();
    const int pad  = s(3);
    const int btnH = h - 2 * pad;
    const int btnY = pad;
    const int innerPad = s(6);

    int x = innerPad;
    loopLabel.setBounds(x, btnY, s(36), btnH); x += s(36) + s(2);
    loopDropdown.setBounds(x, btnY, s(100), btnH); x += s(100) + s(2);
    loopStepLabel.setBounds(x, btnY, s(56), btnH);
}
