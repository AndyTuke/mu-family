#include "MasterLoopSection.h"
#include "UI/Components/MuLookAndFeel.h"
#include <cmath>

MasterLoopSection::MasterLoopSection(ProcessorBase& p)
    : proc(p)
{
    loopLabel.setText("Loop:", juce::dontSendNotification);
    loopLabel.setJustificationType(juce::Justification::centredRight);
    loopLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(loopLabel);

    loopDropdown.addItem(juce::String::charToString(0x221E), 1); // ∞ = pattern reset length disabled
    // Loop lengths in 16-step units, up to the step cap (16 in demo, 256 otherwise).
    for (int i = 1; i <= 16 && i * 16 <= 256; ++i)
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
    syncLoopDropdownFromApvts();
    addAndMakeVisible(loopStepLabel);

    // The Swap group stays hidden until the product lists the timings it offers.
    swapLabel.setText("Swap:", juce::dontSendNotification);
    swapLabel.setJustificationType(juce::Justification::centredRight);
    swapLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    swapLabel.setTooltip("When a queued preset change takes effect");
    swapDropdown.getComboBox().setTooltip("When a queued preset change takes effect");
    addChildComponent(swapLabel);
    addChildComponent(swapDropdown);

    // Catch host automation of mstrLoop so the dropdown / step label stay in
    // sync with the DAW state.
    proc.apvts.addParameterListener("mstrLoop", this);
    startTimerHz(mu_ui::kUiRefreshHz);
}

MasterLoopSection::~MasterLoopSection()
{
    proc.apvts.removeParameterListener("mstrLoop", this);
    if (! swapChoices.empty())
        proc.apvts.removeParameterListener(mu_hotswap::kSwapTimingParamId, this);
    stopTimer();
}

void MasterLoopSection::setSwapTimingChoices(std::vector<SwapChoice> choices)
{
    const bool hadChoices = ! swapChoices.empty();
    swapChoices = std::move(choices);

    swapDropdown.clear();
    for (const auto& c : swapChoices)
        swapDropdown.addItem(c.text, (int) c.timing + 1);   // the item id is the SwapTiming integer + 1
    swapDropdown.onChange = [this](int id)
    {
        if (auto* param = proc.apvts.getParameter(mu_hotswap::kSwapTimingParamId))
            param->setValueNotifyingHost(param->convertTo0to1((float) (id - 1)));
        if (onStatusUpdate) onStatusUpdate("Swap Timing", swapDropdown.getText());
    };

    const bool show = ! swapChoices.empty();
    swapLabel.setVisible(show);
    swapDropdown.setVisible(show);
    if (show && ! hadChoices)
        proc.apvts.addParameterListener(mu_hotswap::kSwapTimingParamId, this);
    if (! show && hadChoices)
        proc.apvts.removeParameterListener(mu_hotswap::kSwapTimingParamId, this);
    syncSwapDropdownFromApvts();
    resized();
}

int MasterLoopSection::getPreferredWidth() const noexcept
{
    int w = kPad + kLoopLabelW + kLabelGap + kLoopDropW + kLabelGap + kLoopCountW + kPad;
    if (! swapChoices.empty())
        w += kGroupGap + kSwapLabelW + kLabelGap + kSwapDropW;
    return w;
}

int MasterLoopSection::swapIdFor(mu_hotswap::SwapTiming stored) const
{
    using T = mu_hotswap::SwapTiming;
    auto offered = [this](T t) { for (const auto& c : swapChoices) if (c.timing == t) return true; return false; };
    if (offered(stored)) return (int) stored + 1;
    // The nearest timing the product offers: a bar is nearest a layer loop, a loop nearest the other loop.
    const T order[3][2] = { { T::OnLayerLoop, T::OnBar }, { T::OnBar, T::OnMasterLoop }, { T::OnLayerLoop, T::OnMasterLoop } };
    const int row = stored == T::OnMasterLoop ? 0 : (stored == T::OnLayerLoop ? 1 : 2);
    for (const T t : order[row])
        if (offered(t)) return (int) t + 1;
    return swapChoices.empty() ? 0 : (int) swapChoices.front().timing + 1;
}

void MasterLoopSection::syncSwapDropdownFromApvts()
{
    if (swapChoices.empty()) return;
    const auto* raw = proc.apvts.getRawParameterValue(mu_hotswap::kSwapTimingParamId);
    const float v   = raw != nullptr ? raw->load() : 0.0f;
    const int   id  = swapIdFor(mu_hotswap::swapTimingFromIndex(std::isfinite(v) ? (int) std::lround(v) : 0));
    if (swapDropdown.getSelectedId() != id)
        swapDropdown.setSelectedId(id, false);
}

void MasterLoopSection::syncLoopDropdownFromApvts()
{
    const int paramVal = (int) proc.apvts.getRawParameterValue("mstrLoop")->load();
    loopStepLabel.setVisible(paramVal > 0);
    loopDropdown.setSelectedId(paramVal + 1, false);
}

void MasterLoopSection::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    const bool loop = parameterID == "mstrLoop";
    const bool swap = parameterID == mu_hotswap::kSwapTimingParamId;
    if (! loop && ! swap) return;

    // Host automation can fire on the audio thread, where juce::Slider / DropdownSelect state must not be
    // touched and a message-queue post would allocate. Off the message thread do nothing: timerCallback
    // reconciles both dropdowns with their parameters on every tick.
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return;

    if (loop) syncLoopDropdownFromApvts();
    else      syncSwapDropdownFromApvts();
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
            syncLoopDropdownFromApvts();
    }
    syncSwapDropdownFromApvts();   // the same self-healing for the swap timing

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

    int x = s(kPad);
    loopLabel.setBounds(x, btnY, s(kLoopLabelW), btnH);    x += s(kLoopLabelW) + s(kLabelGap);
    loopDropdown.setBounds(x, btnY, s(kLoopDropW), btnH);  x += s(kLoopDropW) + s(kLabelGap);
    loopStepLabel.setBounds(x, btnY, s(kLoopCountW), btnH); x += s(kLoopCountW) + s(kGroupGap);

    if (! swapChoices.empty())
    {
        swapLabel.setBounds(x, btnY, s(kSwapLabelW), btnH);    x += s(kSwapLabelW) + s(kLabelGap);
        swapDropdown.setBounds(x, btnY, s(kSwapDropW), btnH);
    }
}
