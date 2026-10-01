// ControlDesign — a GUI sandbox for refining the knob design in isolation, without
// rebuilding a whole product.
//
// The panel pairs every shipped knob with its proposed neumorphic replacement, drawn
// side by side from identical data: same label, same category colour, same
// MuLookAndFeel::kKnobSize*, same range and value, via the actual KnobWithLabel
// component every product instantiates. Left of each pair is the real
// MuLookAndFeel geometry; right is GlowKnobLookAndFeel (local to this file, not
// shipped). One pair per size a colour is actually used at, and per control type
// (step vs smooth) where a size uses both:
//
//   mu-clid, purple (knobEuclidean)
//     Size 1, step   — Steps     (1..64 x1)        EuclideanPanel
//     Size 1, smooth — Attack    (0..10s, skewed)  not a shipped combination
//     Size 2, step   — Octave    (-3..3 x1)        Voice/PitchSubsection
//     Size 2, smooth — Attack    (0..10s, skewed)  Voice/PitchSubsection
//   mu-tant, green (knobPostPad)
//     Size 1, smooth — Cutoff    (skewed, Hz)      not a shipped combination
//     Size 2, smooth — Cutoff    (skewed, Hz)      VoicePanel Filter 1
//
// Pairs marked "not shipped" are there to judge the style at a size/type the product
// doesn't currently use: mu-clid's purple is step-only at Size 1, and mu-tant's green
// appears at Size 2 only, where all five filter knobs are continuous floats (their
// ranges come from the APVTS parameters, not setRange), so green has no step example.
//
// Installs a real MuLookAndFeel instance via setLookAndFeel() on the panel (as
// EditorShellBase does for every product's editor) — without this, KnobWithLabel
// falls back to JUCE's default LookAndFeel and draws nothing like the shipped knob.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// juce::String reads a bare const char* in the system codepage, which mangles the
// UTF-8 bytes in literals like an em dash or a middot; wrap them explicitly.
static juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// The products' own value formatters, mirrored here so the shipped half of each pair
// shows exactly the text it shows in the app (the unit lives in the knob's label, not
// the value). Without these JUCE's raw float formatting takes over and the comparison
// is against something that never ships.
//   mu-clid Voice/PitchSubsection + AmpSubsection + FilterSubsection: adsrValueStr
static juce::String adsrValueText(double v)
{
    const double ms = std::max(1.0, v * 1000.0);
    return ms < 1000.0 ? juce::String((int) std::round(ms))
                       : juce::String(ms / 1000.0, 2);
}

//   mu-tant PluginProcessor_APVTS: the flt_cut cutoffText attribute
static juce::String cutoffValueText(double v)
{
    return v < 1000.0 ? juce::String((int) std::round(v))
                      : juce::String(v / 1000.0, 1);
}

// Alternative knob look — NOT part of the shipped design, kept local to ControlDesign
// so it can be compared against the real MuLookAndFeel geometry without touching
// mu-core or any product. Modelled on a neumorphic reference: a raised/embossed dark
// disc (soft drop shadow + subtle highlight gradient), a glowing ring with a wedge
// removed at the bottom (owner spec: 7 o'clock round to 5 o'clock, not the family's
// ~90-degree gap), and a single glowing position dot — no pointer line, no filled
// "value" arc. Ring/dot colour comes from the slider's own accent colour so it can
// still be compared across the family's category colours. All glow extents are sized
// as a fraction of the knob's own radius (never a fixed pixel count) so the dot and
// ring glow never clip against the component edge at any knob size or sweep angle —
// including right at the gap edges, where a fixed-pixel glow was clipping before.
class GlowKnobLookAndFeel : public MuLookAndFeel
{
public:
    // The annotations KnobWithLabel paints (mod ring, live mod arc, GR arc) hug this
    // ring, so report where it actually is — inset from the bounds, on this style's own
    // sweep — rather than letting them assume the family's standard placement.
    RotaryGeometry getRotaryGeometry(juce::Rectangle<int> sliderBounds) const override
    {
        const auto b = sliderBounds.toFloat();
        const float outerR = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        return { b.getCentre(), outerR * 0.82f, kStartAngle, kEndAngle };
    }

    // 7 o'clock round through 12 back to 5 o'clock — a 300-degree sweep, 60-degree
    // gap at the bottom. Independent of the Slider's own (family-standard) rotary
    // parameters, which drawRotarySlider also receives but this style doesn't use.
    static constexpr float kStartAngle = (7.0f / 12.0f) * juce::MathConstants<float>::twoPi;
    static constexpr float kEndAngle   = (5.0f / 12.0f) * juce::MathConstants<float>::twoPi
                                        + juce::MathConstants<float>::twoPi;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float /*startAngle*/, float /*endAngle*/,
                          juce::Slider& slider) override
    {
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        const float outerR = juce::jmin(w, h) * 0.5f - 2.0f;
        const float ringR  = outerR * 0.82f;
        const float faceR  = outerR * 0.74f;
        const float angle  = kStartAngle + sliderPos * (kEndAngle - kStartAngle);

        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

        // Tick marks across the same sweep — only where there's room. The threshold is
        // on the rotary's own radius, and a Size 2 knob only reaches ~17 (its slider
        // rect is the cell minus the label band), so this sits below that to include
        // Size 2 while still skipping the two smallest sizes, where 16 ticks would mush
        // together.
        if (outerR >= 15.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            constexpr int kTicks = 16;
            for (int i = 0; i <= kTicks; ++i)
            {
                const float a = kStartAngle + (float) i / (float) kTicks * (kEndAngle - kStartAngle);
                const juce::Point<float> p1(cx + outerR * 0.90f * std::sin(a), cy - outerR * 0.90f * std::cos(a));
                const juce::Point<float> p2(cx + outerR * std::sin(a),         cy - outerR * std::cos(a));
                g.drawLine({ p1, p2 }, 1.0f);
            }
        }

        // Raised disc face, lit from the top right. Four cues sell the convex read, all
        // consistent with that one light source: the cast shadow falls away from it
        // (down-left), a radial highlight sits where the light strikes, the lit rim
        // catches a thin bright edge, and the far rim takes a soft inner shade.
        juce::Path discPath;
        discPath.addEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.65f),
                         (int) juce::jmax(2.0f, outerR * 0.18f),
                         { -(int) (outerR * 0.06f), (int) (outerR * 0.09f) }).drawForPath(g, discPath);

        juce::ColourGradient faceGrad(juce::Colour(0xff3b2d47),                       // lit crown
                                      cx + faceR * 0.42f, cy - faceR * 0.42f,
                                      juce::Colour(0xff100c16),                       // shadowed rim
                                      cx - faceR * 0.75f, cy + faceR * 0.85f, true);
        faceGrad.addColour(0.35, juce::Colour(0xff2d2238));
        faceGrad.addColour(0.70, juce::Colour(0xff1b1423));
        g.setGradientFill(faceGrad);
        g.fillPath(discPath);

        // Specular and occlusion, both as radial gradients clipped to the disc rather
        // than stroked arcs: a stroke has crisp sides and abruptly-ending caps, which
        // reads as a drawn line instead of light falling across a curved surface.
        {
            const juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(discPath);

            const float litX = cx + faceR * 0.50f;
            const float litY = cy - faceR * 0.50f;
            const float litR = faceR * 1.05f;
            juce::ColourGradient spec(juce::Colours::white.withAlpha(0.20f), litX, litY,
                                      juce::Colours::white.withAlpha(0.0f),  litX + litR, litY, true);
            spec.addColour(0.45, juce::Colours::white.withAlpha(0.06f));
            g.setGradientFill(spec);
            g.fillEllipse(litX - litR, litY - litR, litR * 2.0f, litR * 2.0f);

            const float shX = cx - faceR * 0.45f;
            const float shY = cy + faceR * 0.52f;
            const float shR = faceR * 0.95f;
            juce::ColourGradient occl(juce::Colours::black.withAlpha(0.40f), shX, shY,
                                      juce::Colours::black.withAlpha(0.0f),  shX + shR, shY, true);
            g.setGradientFill(occl);
            g.fillEllipse(shX - shR, shY - shR, shR * 2.0f, shR * 2.0f);
        }

        // Glowing ring ARC (the wedge between kEndAngle and kStartAngle, under the
        // knob, is left undrawn) — soft wide passes building up to a crisp core line.
        // Stroke widths scale with outerR so the glow fits inside ringR's headroom to
        // the component edge at every knob size, not just the larger ones.
        juce::Path ringPath;
        ringPath.addCentredArc(cx, cy, ringR, ringR, 0.0f, kStartAngle, kEndAngle, true);
        for (float i = 4.0f; i >= 1.0f; i -= 1.0f)
        {
            g.setColour(accent.withAlpha(0.10f));
            g.strokePath(ringPath, juce::PathStrokeType(i * outerR * 0.09f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }
        g.setColour(accent);
        g.strokePath(ringPath, juce::PathStrokeType(juce::jmax(1.2f, outerR * 0.05f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Position dot on the ring, with its own small glow — the only value
        // indicator, and always fully visible (including right at the gap edges,
        // where the value naturally sits near the sweep's minimum or maximum).
        const float dotX = cx + ringR * std::sin(angle);
        const float dotY = cy - ringR * std::cos(angle);
        const float dotR = juce::jmax(2.0f, outerR * 0.065f);

        // Halo: one radial gradient fading the accent colour out to fully transparent,
        // so the light falls off smoothly rather than banding across stacked rings.
        // Its reach stays inside ringR's headroom to the component edge, so the glow is
        // never clipped at any angle.
        const float haloR = dotR * 2.6f;
        juce::ColourGradient halo(accent.withAlpha(0.55f), dotX, dotY,
                                  accent.withAlpha(0.0f),  dotX + haloR, dotY, true);
        halo.addColour(0.35, accent.withAlpha(0.26f));
        g.setGradientFill(halo);
        g.fillEllipse(dotX - haloR, dotY - haloR, haloR * 2.0f, haloR * 2.0f);

        // A tighter white bloom hugging the core, so it reads as light spilling onto
        // the surface rather than a wash of the accent colour.
        const float bloomR = dotR * 1.8f;
        juce::ColourGradient bloom(juce::Colours::white.withAlpha(0.40f), dotX, dotY,
                                   juce::Colours::white.withAlpha(0.0f),  dotX + bloomR, dotY, true);
        g.setGradientFill(bloom);
        g.fillEllipse(dotX - bloomR, dotY - bloomR, bloomR * 2.0f, bloomR * 2.0f);

        g.setColour(juce::Colours::white);
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

        // Optional value readout centred inside the disc — step-type controls only
        // (flagged via a component property on the Slider), where the exact number is
        // worth reading; sweeping controls stay dot-only. Formatted from the raw value
        // rather than getTextFromValue, which the caller blanks to suppress
        // KnobWithLabel's own dead-zone value text.
        if ((bool) slider.getProperties().getWithDefault("muShowCenterValue", false))
        {
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::valueText));
            g.setFont(juce::Font(juce::FontOptions(juce::jmax(9.0f, outerR * 0.34f), juce::Font::bold)));
            g.drawText(juce::String((int) std::lround(slider.getValue())),
                      juce::Rectangle<float>(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f).toNearestInt(),
                      juce::Justification::centred, false);
        }
    }
};

// One row: a title + description in the left label column, and a strip of
// KnobWithLabel instances (each already sized by the caller) laid out with even
// gaps, vertically centred on the row.
class KnobRow : public juce::Component
{
public:
    KnobRow(juce::String rowName, juce::String rowDesc)
        : name(std::move(rowName)), desc(std::move(rowDesc)) {}

    ~KnobRow() override
    {
        // Clear the per-knob LookAndFeel before the knobs (and the LookAndFeel they
        // point at) go away.
        for (auto& e : entries)
            e.knob->setLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    // Adds a comparison pair: the shipped knob, and immediately to its right the same
    // control drawn by altLnf (the proposed replacement). Both get identical label,
    // colour, size, range and value, so the only difference on screen is the drawing.
    //
    // isStep marks a discrete control, where the exact number is worth reading, from a
    // smooth/continuous one, where it isn't. The replacement shows the value centred on
    // its disc for step controls and stays dot-only for smooth ones, so on the
    // replacement KnobWithLabel's own dead-zone value text is always suppressed: it has
    // no flag for that, so the slider's textFromValueFunction is blanked and the
    // component draws an empty string. (It must NOT be done by painting over that
    // region — the glow disc fills the dead zone, so an opaque patch slices the disc
    // and erases the position dot whenever the value sits low in the sweep.)
    struct Pair { KnobWithLabel& shipped; KnobWithLabel& replacement; };

    // textFn, when given, is the product's own value formatter (the shipped knobs show
    // formatted text like "8.0" or "240", never JUCE's raw float), applied to the
    // shipped half only — the replacement's text is blanked either way.
    Pair addPair(const juce::String& label, MuLookAndFeel::ColourIds colour,
                 int w, int h, double lo, double hi, double step, double value,
                 bool isStep, const juce::String& caption, juce::LookAndFeel& altLnf,
                 std::function<juce::String(double)> textFn = nullptr)
    {
        auto make = [&](bool replacement) -> KnobWithLabel&
        {
            auto k = std::make_unique<KnobWithLabel>(label, colour);
            k->setSize(w, h);
            k->setRange(lo, hi, step);
            k->setValue(value, juce::dontSendNotification);
            if (replacement)
            {
                k->setLookAndFeel(&altLnf);
                k->getSlider().getProperties().set("muShowCenterValue", isStep);
                k->getSlider().textFromValueFunction = [](double) { return juce::String(); };
            }
            else if (textFn)
            {
                k->getSlider().textFromValueFunction = textFn;
                k->getSlider().setValue(value, juce::dontSendNotification);   // re-render with it
            }
            addAndMakeVisible(*k);
            auto& ref = *k;
            entries.push_back({ std::move(k), label, ! replacement,
                                replacement ? juce::String() : caption, 2 });
            return ref;
        };

        auto& shipped     = make(false);
        auto& replacement = make(true);
        return { shipped, replacement };
    }

    // A single knob in the replacement style, with no shipped counterpart — for sweeps
    // where the comparison is already established and only the new look is in question.
    KnobWithLabel& addReplacementOnly(const juce::String& label, MuLookAndFeel::ColourIds colour,
                                      int w, int h, double lo, double hi, double step,
                                      double value, bool isStep, const juce::String& caption,
                                      juce::LookAndFeel& altLnf)
    {
        auto k = std::make_unique<KnobWithLabel>(label, colour);
        k->setSize(w, h);
        k->setRange(lo, hi, step);
        k->setValue(value, juce::dontSendNotification);
        k->setLookAndFeel(&altLnf);
        k->getSlider().getProperties().set("muShowCenterValue", isStep);
        k->getSlider().textFromValueFunction = [](double) { return juce::String(); };
        addAndMakeVisible(*k);
        auto& ref = *k;
        entries.push_back({ std::move(k), label, true, caption, 1 });
        return ref;
    }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        auto label = getLocalBounds().removeFromLeft(kLabelW).reduced(14, 0);
        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(name, label.removeFromTop(label.getHeight() / 2).withTrimmedTop(14),
                   juce::Justification::bottomLeft, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(desc, label.withTrimmedBottom(14), juce::Justification::topLeft, false);
    }

    // Outlines each knob's real bounds (rotary + label band) so anything drawn outside
    // that border reads at a glance, captions each pair with its size and control type,
    // and flags labels that don't fit their knob's width — KnobWithLabel silently
    // ellipsises those in production, hiding exactly the problem worth catching, so the
    // full label is redrawn unclipped in red over a dark backing rect sized to its true
    // width. Labels that already fit are left as the product draws them.
    void paintOverChildren(juce::Graphics& g) override
    {
        using mu_ui::sf;

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        for (auto& e : entries)
            g.drawRect(e.knob->getBounds(), 1);

        // Captions, centred under the knob(s) they describe.
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].caption.isEmpty()) continue;
            auto span = entries[i].knob->getBounds();
            for (int j = 1; j < entries[i].captionSpan && i + (size_t) j < entries.size(); ++j)
                span = span.getUnion(entries[i + (size_t) j].knob->getBounds());
            g.drawText(entries[i].caption,
                       span.withY(span.getBottom() + 4).withHeight(kCaptionH).expanded(20, 0),
                       juce::Justification::centred, false);
        }

        const juce::Font labelFont(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobLabelFont)));
        g.setFont(labelFont);

        for (auto& e : entries)
        {
            const int textW = (int) juce::GlyphArrangement::getStringWidth(labelFont, e.label);
            if (textW <= e.knob->getWidth())
                continue;   // fits — production rendering already shows it correctly

            const int labelH = (int) sf((float) MuLookAndFeel::kKnobLabelH);
            auto r = e.knob->getBounds();
            auto textArea = juce::Rectangle<int>(r.getCentreX() - textW / 2 - 4,
                                                 r.getBottom() - labelH, textW + 8, labelH);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRect(textArea);
            g.setColour(juce::Colour(0xffff5a4d));   // overflow warning — not a shipped colour token
            g.drawText(e.label, textArea, juce::Justification::centred, false);
        }
    }

    // Knobs sit in a centred strip, tight within each shipped/replacement pair and
    // widely spaced between pairs, so the comparison groups read as pairs.
    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(kLabelW);
        area.removeFromBottom(kCaptionH + 6);   // room for the pair captions

        int totalW = 0;
        for (size_t i = 0; i < entries.size(); ++i)
            totalW += entries[i].knob->getWidth() + (i == 0 ? 0 : gapBefore(i));

        int x = area.getX() + (area.getWidth() - totalW) / 2;
        const int cy = area.getCentreY();

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0) x += gapBefore(i);
            entries[i].knob->setTopLeftPosition(x, cy - entries[i].knob->getHeight() / 2);
            x += entries[i].knob->getWidth();
        }
    }

    static constexpr int kLabelW   = 170;
    static constexpr int kCaptionH = 13;

private:
    struct Entry
    {
        std::unique_ptr<KnobWithLabel> knob;
        juce::String label;
        bool startsGroup = false;  // first of a pair (or a lone knob): wide gap before it
        juce::String caption;      // size + control type, drawn under the group
        int captionSpan = 1;       // knobs the caption is centred across
    };

    int gapBefore(size_t i) const { return entries[i].startsGroup ? 48 : 10; }

    juce::String name, desc;
    std::vector<Entry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobRow)
};

// The panel: a title strip plus the three KnobRows described above.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        using Id = MuLookAndFeel::ColourIds;

        // Install the real MuLookAndFeel, exactly as EditorShellBase does for every
        // product's editor — KnobWithLabel has no look of its own, so without this
        // the rotary falls back to JUCE's stock default.
        setLookAndFeel(&lookAndFeel);

        // mu-clid's purple — Size 1 (EuclideanPanel, step only) and Size 2
        // (Voice/PitchSubsection, which uses both step and smooth controls).
        auto clid = std::make_unique<KnobRow>(utf8("mu-clid \xe2\x80\x94 purple"),
                                              "knobEuclidean");
        clid->addPair("Steps",  Id::knobEuclidean, MuLookAndFeel::kKnobSize1W, MuLookAndFeel::kKnobSize1H,
                      1, 64, 1, 5,      true,  utf8("Size 1 \xc2\xb7 step"),   altLookAndFeel);
        clid->addPair("Attack (ms)", Id::knobEuclidean, MuLookAndFeel::kKnobSize1W, MuLookAndFeel::kKnobSize1H,
                      0, 10, 0.001, 0.24, false, utf8("Size 1 \xc2\xb7 smooth (not shipped)"), altLookAndFeel,
                      adsrValueText);
        clid->addPair("Octave", Id::knobEuclidean, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                      -3, 3, 1, 2,      true,  utf8("Size 2 \xc2\xb7 step"),   altLookAndFeel);
        clid->addPair("Attack (ms)", Id::knobEuclidean, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                      0, 10, 0.001, 0.24, false, utf8("Size 2 \xc2\xb7 smooth"), altLookAndFeel,
                      adsrValueText);
        addAndMakeVisible(*clid);
        rows.push_back(std::move(clid));

        // mu-tant's green — Size 2 only, and every filter knob is a continuous float,
        // so there's no step example to pair.
        auto tant = std::make_unique<KnobRow>(utf8("mu-tant \xe2\x80\x94 green"),
                                              "knobPostPad");
        tant->addPair("Cutoff (kHz)", Id::knobPostPad, MuLookAndFeel::kKnobSize1W, MuLookAndFeel::kKnobSize1H,
                      20, 20000, 0.0, 8000, false, utf8("Size 1 \xc2\xb7 smooth (not shipped)"), altLookAndFeel,
                      cutoffValueText);
        tant->addPair("Cutoff (kHz)", Id::knobPostPad, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                      20, 20000, 0.0, 8000, false, utf8("Size 2 \xc2\xb7 smooth"), altLookAndFeel,
                      cutoffValueText);
        addAndMakeVisible(*tant);
        rows.push_back(std::move(tant));

        // The two small sizes, where the style is under the most pressure: Size 3 is
        // mu-clid's pad knobs (step) and the mixer strip (smooth), Size 4 is the mixer's
        // sidechain envelope. Size 4 has no shipped step control — included anyway,
        // because fitting a centred number into that disc is the hardest case there is.
        auto small = std::make_unique<KnobRow>(utf8("Small sizes"), "Size 3 + Size 4");
        small->addPair("Pre Pad", Id::knobPrePad, MuLookAndFeel::kKnobSize3W, MuLookAndFeel::kKnobSize3H,
                       0, 16, 1, 3, true, utf8("Size 3 \xc2\xb7 step"), altLookAndFeel);
        small->addPair("SC Amount", Id::knobPan, MuLookAndFeel::kKnobSize3W, MuLookAndFeel::kKnobSize3H,
                       0, 100, 0.1, 62, false, utf8("Size 3 \xc2\xb7 smooth"), altLookAndFeel);
        small->addPair("Steps", Id::knobEuclidean, MuLookAndFeel::kKnobSize4W, MuLookAndFeel::kKnobSize4H,
                       1, 64, 1, 5, true, utf8("Size 4 \xc2\xb7 step (not shipped)"), altLookAndFeel);
        small->addPair("Attack", Id::knobLevel, MuLookAndFeel::kKnobSize4W, MuLookAndFeel::kKnobSize4H,
                       0, 10, 0.001, 0.24, false, utf8("Size 4 \xc2\xb7 smooth"), altLookAndFeel,
                       adsrValueText);
        addAndMakeVisible(*small);
        rows.push_back(std::move(small));

        // The overlays KnobWithLabel paints over the rotary. These are drawn by the
        // component, NOT the LookAndFeel, against hard-coded sweep angles — so on the
        // replacement they should sit off the new ring. That mismatch is the point of
        // showing them here.
        auto over = std::make_unique<KnobRow>(utf8("Overlays"), "drawn by KnobWithLabel");
        auto modRing = over->addPair("Cutoff", Id::knobPostPad, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                                     20, 20000, 0.0, 8000, false, utf8("mod ring"), altLookAndFeel, cutoffValueText);
        modRing.shipped.setIsModulated(true);
        modRing.replacement.setIsModulated(true);

        auto modArc = over->addPair("Cutoff", Id::knobPostPad, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                                    20, 20000, 0.0, 8000, false, utf8("mod ring + live arc"), altLookAndFeel, cutoffValueText);
        modArc.shipped.setIsModulated(true);
        modArc.replacement.setIsModulated(true);
        modArc.shipped.setModulatedNorm(0.78f);
        modArc.replacement.setModulatedNorm(0.78f);

        auto gr = over->addPair("Level", Id::knobLevel, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                                -60, 6, 0.1, -6, false, utf8("GR arc"), altLookAndFeel);
        gr.shipped.setGRSource(&grLevel[0]);
        gr.replacement.setGRSource(&grLevel[1]);
        addAndMakeVisible(*over);
        rows.push_back(std::move(over));

        // Every category colour in the replacement style — the comparison is settled by
        // now, so this asks only whether the glow holds up across the whole palette.
        auto palette = std::make_unique<KnobRow>(utf8("Palette"), "replacement only, Size 2");
        struct Swatch { const char* label; MuLookAndFeel::ColourIds colour; };
        static constexpr Swatch swatches[] = {
            { "Euclid",   Id::knobEuclidean }, { "Insert",  Id::knobInsertPad },
            { "Level",    Id::knobLevel     }, { "FX Send", Id::knobFxSend    },
            { "Reverb",   Id::knobReverb    }, { "Pan",     Id::knobPan       },
            { "Pre Pad",  Id::knobPrePad    }, { "Post Pad",Id::knobPostPad   },
        };
        for (auto& sw : swatches)
            palette->addReplacementOnly(sw.label, sw.colour,
                                        MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H,
                                        0, 100, 1, 62, false, {}, altLookAndFeel);
        addAndMakeVisible(*palette);
        rows.push_back(std::move(palette));

        setSize(980, 680);
    }

    ~ControlDesignPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

        auto header = getLocalBounds().removeFromTop(kHeader).reduced(20, 0);
        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(utf8("Control Design \xe2\x80\x94 shipped vs replacement"),
                   header, juce::Justification::centredLeft, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(utf8("each pair: left = shipped \xc2\xb7 right = neumorphic replacement"),
                   header, juce::Justification::centredRight, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
        const int rowH = (getHeight() - kHeader) / (int) rows.size();
        for (size_t i = 1; i < rows.size(); ++i)
            g.drawHorizontalLine(kHeader + (int) i * rowH, 20.0f, (float) getWidth() - 20.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(kHeader);
        const int rowH = area.getHeight() / (int) rows.size();
        for (auto& r : rows)
            r->setBounds(area.removeFromTop(rowH));
    }

private:
    static constexpr int kHeader = 44;

    MuLookAndFeel lookAndFeel;
    GlowKnobLookAndFeel altLookAndFeel;

    // Fixed gain-reduction readings for the GR-arc demo; KnobWithLabel polls these at
    // 30 Hz, so they have to outlive the knobs pointed at them.
    std::array<std::atomic<float>, 2> grLevel { { { 0.45f }, { 0.45f } } };

    std::vector<std::unique_ptr<KnobRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlDesignPanel)
};

class ControlDesignApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "ControlDesign"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff1c1c1b), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new ControlDesignPanel(), true);
            setResizable(true, true);
            setResizeLimits(560, 380, 1400, 900);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ControlDesignApplication)
