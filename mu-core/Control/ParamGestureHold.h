#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <vector>

// Writes a parameter from a controller as ONE host gesture: the gesture opens on the first write
// and closes after 300 ms of quiet, so a sweep records as a single automation stroke instead of
// a gesture per message. Message thread only.
namespace mu_core
{

class ParamGestureHold : private juce::Timer
{
public:
    ~ParamGestureHold() override { stopTimer(); endAll(); }

    void set(juce::RangedAudioParameter& p, float normalised)
    {
        auto it = std::find_if(open.begin(), open.end(), [&p](const Entry& e) { return e.param == &p; });
        if (it == open.end())
        {
            p.beginChangeGesture();
            open.push_back({ &p, 0.0 });
            it = open.end() - 1;
        }
        it->expiryMs = juce::Time::getMillisecondCounterHiRes() + kHoldMs;
        p.setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
        if (! isTimerRunning()) startTimer(kPollMs);
    }

private:
    static constexpr double kHoldMs = 300.0;
    static constexpr int    kPollMs = 100;
    struct Entry { juce::RangedAudioParameter* param; double expiryMs; };

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto it = open.begin(); it != open.end();)
            if (it->expiryMs <= now) { it->param->endChangeGesture(); it = open.erase(it); }
            else ++it;
        if (open.empty()) stopTimer();
    }

    void endAll() { for (auto& e : open) e.param->endChangeGesture(); open.clear(); }

    std::vector<Entry> open;
};

} // namespace mu_core
