#include "StepIndicator.h"

StepIndicator::StepIndicator()
{
    setInterceptsMouseClicks(true, false);   // hover reaches it for the status text; it has no click handling
}

StepIndicator::~StepIndicator() { stopTimer(); }

void StepIndicator::setText(const juce::String& lit, const juce::String& ghost, bool pulses)
{
    if (lit == litText && ghost == ghostText && pulses == ghostPulses) return;
    litText = lit;  ghostText = ghost;  ghostPulses = pulses;
    updateTimer();
    repaint();
}

void StepIndicator::setLamps(const std::vector<Lamp>& states)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto flashMs = (double) MuLookAndFeel::lighting().stepFlashMs;

    // A lamp that has just become current flashes brighter for a moment.
    std::vector<double> flashes(states.size(), 0.0);
    for (size_t i = 0; i < states.size(); ++i)
    {
        const bool wasCurrent = i < lamps.size() && lamps[i] == Lamp::Current;
        if (states[i] == Lamp::Current && ! wasCurrent && ! lamps.empty()) flashes[i] = now + flashMs;
        else if (states[i] == Lamp::Current && i < flashUntil.size())      flashes[i] = flashUntil[i];
    }
    if (states == lamps && flashes == flashUntil) return;

    lamps      = states;
    flashUntil = std::move(flashes);
    lampHints.resize(lamps.size());
    updateTimer();
    repaint();
}

void StepIndicator::setLampHint(int i, const juce::String& name, const juce::String& value)
{
    if (i >= 0 && i < (int) lampHints.size()) lampHints[(size_t) i] = { name, value };
}

void StepIndicator::setHint(const juce::String& name, const juce::String& value) { hint = { name, value }; }

// The animation timer runs only while something pulses or flashes.
void StepIndicator::updateTimer()
{
    bool animated = ghostPulses;
    for (auto l : lamps) animated = animated || l == Lamp::Queued;
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto t : flashUntil) animated = animated || t > now;
    if (animated && ! isTimerRunning()) startTimerHz(mu_ui::kUiRefreshHz);
    else if (! animated && isTimerRunning()) stopTimer();
}

StepIndicator::Layout StepIndicator::computeLayout(juce::Rectangle<int> bounds, int numLamps)
{
    Layout out;
    const int pitch = mu_ui::s(MuLookAndFeel::kStepLampPitch);
    const int gap   = numLamps > 0 ? mu_ui::s(MuLookAndFeel::kStepGap) : 0;
    const int lampsW = numLamps * pitch;
    out.lcd = bounds.withWidth(juce::jmax(0, bounds.getWidth() - lampsW - gap));
    for (int i = 0; i < numLamps; ++i)
        out.lamps.push_back({ out.lcd.getRight() + gap + i * pitch, bounds.getY(), pitch, bounds.getHeight() });
    return out;
}

void StepIndicator::splitText(float avail, float litMeasured, float ghostMeasured, float gap, float& litW, float& ghostW)
{
    if (ghostMeasured <= 0.0f)                       { litW = juce::jmin(litMeasured, avail); ghostW = 0.0f; return; }
    if (litMeasured + gap + ghostMeasured <= avail)  { litW = litMeasured; ghostW = ghostMeasured; return; }
    litW   = juce::jmin(litMeasured, juce::jmax(0.6f * avail, avail - ghostMeasured - gap));
    ghostW = juce::jmax(0.0f, avail - litW - gap);
}

float StepIndicator::pulse(double timeMs, float hz) noexcept
{
    return 0.5f - 0.5f * (float) std::cos(juce::MathConstants<double>::twoPi * (double) hz * timeMs / 1000.0);
}

float StepIndicator::litFor(Lamp state, double timeMs, bool flash) noexcept
{
    const auto& L = MuLookAndFeel::lighting();
    const float bright = juce::jmin(1.0f, L.lampOn + L.lampPlayhead);
    switch (state)
    {
        case Lamp::Empty:     return L.lampOff;
        case Lamp::Available: return L.lampDim;
        case Lamp::Current:   return flash ? bright : L.lampOn;
        case Lamp::Queued:    return L.lampDim + (bright - L.lampDim) * pulse(timeMs, L.queuedPulseHz);
        case Lamp::Fault:     return L.lampDim;
    }
    return L.lampOff;
}

int StepIndicator::lampAt(juce::Point<int> p) const
{
    const auto layout = computeLayout(getLocalBounds(), (int) lamps.size());
    for (int i = 0; i < (int) layout.lamps.size(); ++i)
        if (layout.lamps[(size_t) i].contains(p)) return i;
    return -1;
}

void StepIndicator::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate) return;
    const int i = lampAt(e.getPosition());
    const auto& h = (i >= 0 && i < (int) lampHints.size()) ? lampHints[(size_t) i] : hint;
    if (h.first.isNotEmpty() || h.second.isNotEmpty()) onStatusUpdate(h.first, h.second);
}

void StepIndicator::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    const auto& L = MuLookAndFeel::lighting();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto layout = computeLayout(getLocalBounds(), (int) lamps.size());
    const auto accent = MuLookAndFeel::appAccent(*this);

    // The LCD: glass, the lit lettering and its ghost continuation, then the glare and bezel.
    const auto lcd = layout.lcd.toFloat();
    MuLookAndFeel::drawLcdGlass(g, lcd, accent, false);
    {
        const auto font = MuLookAndFeel::lcdFont(sf((float) MuLookAndFeel::kLcdTextH));
        const auto area = layout.lcd.reduced(s(MuLookAndFeel::kLcdTextPadX), 0);
        const float gap = (float) s(MuLookAndFeel::kLcdTextPadX);
        float litW = 0.0f, ghostW = 0.0f;
        splitText((float) area.getWidth(), juce::GlyphArrangement::getStringWidth(font, litText),
                  juce::GlyphArrangement::getStringWidth(font, ghostText), gap, litW, ghostW);

        const auto lit = MuLookAndFeel::lcdLitColour(*this);
        g.setFont(font);
        g.setColour(lit);
        g.drawText(litText, area.withWidth((int) std::ceil(litW)), juce::Justification::centredLeft, true);
        if (ghostW > 0.0f)
        {
            const float a = ghostPulses ? L.lcdGhost + (1.0f - L.lcdGhost) * pulse(now, L.queuedPulseHz) : L.lcdGhost;
            g.setColour(lit.withAlpha(a));
            g.drawText(ghostText, area.withX(area.getX() + (int) std::ceil(litW + gap)).withWidth((int) std::ceil(ghostW)),
                       juce::Justification::centredLeft, true);
        }
    }
    MuLookAndFeel::drawLcdFront(g, lcd);

    // The lamps: a dark disc with the lit lens a pixel inside it, lit by the item's state.
    using Id = MuLookAndFeel::ColourIds;
    const float d = sf((float) MuLookAndFeel::kStatusLampD);
    for (size_t i = 0; i < layout.lamps.size(); ++i)
    {
        const auto cell = layout.lamps[i].toFloat();
        const juce::Rectangle<float> disc(cell.getCentreX() - d * 0.5f, cell.getCentreY() - d * 0.5f, d, d);
        const bool flash = flashUntil[i] > now;
        const auto clr = lamps[i] == Lamp::Fault ? MuLookAndFeel::colour(Id::indicatorFault) : accent;

        g.setColour(MuLookAndFeel::lampBase());
        g.fillEllipse(disc);
        juce::Path lens;
        lens.addEllipse(disc.reduced(1.0f));
        MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(clr, litFor(lamps[i], now, flash)),
                                disc.getCentre(), d * L.lampHotSpotReach);
    }
}
