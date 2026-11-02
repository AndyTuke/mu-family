#include "SourceRangeBar.h"
#include "MuLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kColumns = 1024, kProbes = 32;
}

void SourceRangeBar::setAudio(std::shared_ptr<const juce::AudioBuffer<float>> audio)
{
    peaks.clear();
    length = audio != nullptr ? audio->getNumSamples() : 0;
    rangeStart = 0; rangeEnd = length;
    if (length > 0)
    {
        // Summarise the source: the loudest sample probed in each column, scaled so the loudest column fills the bar.
        peaks.assign((size_t) kColumns, 0.0f);
        float loudest = 1.0e-6f;
        const int channels = audio->getNumChannels();
        for (int col = 0; col < kColumns; ++col)
        {
            const int from = (int) ((int64_t) length * col / kColumns), to = (int) ((int64_t) length * (col + 1) / kColumns);
            const int step = std::max(1, (to - from) / kProbes);
            float peak = 0.0f;
            for (int at = from; at < to; at += step)
                for (int ch = 0; ch < channels; ++ch) peak = std::max(peak, std::abs(audio->getSample(ch, at)));
            peaks[(size_t) col] = peak;
            loudest = std::max(loudest, peak);
        }
        for (auto& p : peaks) p /= loudest;
    }
    repaint();
}

void SourceRangeBar::setRange(int start, int end)
{
    if (grab != Grab::None) return;   // a drag in progress owns the range
    start = juce::jlimit(0, juce::jmax(0, length - 1), start);
    end = juce::jlimit(start + 1, juce::jmax(start + 1, length), end <= 0 ? length : end);
    if (start == rangeStart && end == rangeEnd) return;
    rangeStart = start; rangeEnd = end;
    repaint();
}

int SourceRangeBar::xOfSample(int sample) const noexcept
{
    return length > 0 ? (int) std::lround((double) sample / (double) length * (double) getWidth()) : 0;
}

int SourceRangeBar::sampleAtX(int x) const noexcept
{
    return length > 0 ? juce::jlimit(0, length, (int) std::lround((double) x / (double) std::max(1, getWidth()) * (double) length)) : 0;
}

SourceRangeBar::Grab SourceRangeBar::grabAt(int x) const noexcept
{
    const int grabPx = juce::jmax(6, getHeight() / 4);
    const int a = xOfSample(rangeStart), b = xOfSample(rangeEnd);
    const int da = std::abs(x - a), db = std::abs(x - b);
    if (da <= grabPx && da <= db) return Grab::Start;
    if (db <= grabPx) return Grab::End;
    return x > a && x < b ? Grab::Slide : Grab::None;
}

void SourceRangeBar::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds();
    g.setColour(LF::colour(LF::lfoEditorBackground));
    g.fillRect(r);
    if (length <= 0 || peaks.empty()) return;

    const int mid = r.getCentreY(), half = r.getHeight() / 2 - 2;
    const int a = xOfSample(rangeStart), b = xOfSample(rangeEnd);
    for (int x = 0; x < r.getWidth(); ++x)
    {
        const float p = peaks[(size_t) std::min(kColumns - 1, x * kColumns / std::max(1, r.getWidth()))];
        const int h = std::max(1, (int) std::lround(p * (float) half));
        g.setColour(x >= a && x < b ? LF::colour(LF::lfoEditorCurve) : LF::colour(LF::labelText).withAlpha(0.25f));
        g.drawVerticalLine(x, (float) (mid - h), (float) (mid + h));
    }

    // The ignored parts are veiled; the range has a frame and a handle on each end.
    g.setColour(LF::colour(LF::lfoEditorBackground).withAlpha(0.55f));
    g.fillRect(0, 0, a, r.getHeight());
    g.fillRect(b, 0, r.getWidth() - b, r.getHeight());
    g.setColour(LF::appAccent(*this));
    g.drawRect(a, 0, std::max(1, b - a), r.getHeight(), 1);
    const int hw = std::max(3, r.getHeight() / 8);
    g.fillRect(a, 0, hw, r.getHeight());
    g.fillRect(b - hw, 0, hw, r.getHeight());
}

void SourceRangeBar::mouseDown(const juce::MouseEvent& e)
{
    grab = grabAt(e.x);
    dragged = false;
    const int at = sampleAtX(e.x);
    grabOffset = grab == Grab::End ? at - rangeEnd : at - rangeStart;
}

void SourceRangeBar::mouseDrag(const juce::MouseEvent& e)
{
    if (grab == Grab::None || length <= 0) return;
    const int at = sampleAtX(e.x) - grabOffset;
    const int minR = std::min(minRange, length);
    if (grab == Grab::Start)      rangeStart = juce::jlimit(0, rangeEnd - minR, at);
    else if (grab == Grab::End)   rangeEnd = juce::jlimit(rangeStart + minR, length, at);
    else
    {
        const int span = rangeEnd - rangeStart;
        rangeStart = juce::jlimit(0, length - span, at);
        rangeEnd = rangeStart + span;
    }
    dragged = true;
    repaint();
}

void SourceRangeBar::mouseUp(const juce::MouseEvent&)
{
    const bool changed = dragged && grab != Grab::None;
    grab = Grab::None;
    dragged = false;
    if (changed && onRangeChanged) onRangeChanged(rangeStart, rangeEnd);
}

void SourceRangeBar::mouseMove(const juce::MouseEvent& e)
{
    const auto g = grabAt(e.x);
    setMouseCursor(g == Grab::Start || g == Grab::End ? juce::MouseCursor::LeftRightResizeCursor
                 : g == Grab::Slide ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}
