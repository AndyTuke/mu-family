#include "VoicePanel.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tant
{

namespace
{
    juce::StringArray rootNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    void populateRoots(DropdownSelect& d)
    {
        const auto names = rootNames();
        for (int i = 0; i < names.size(); ++i)
            d.addItem(names[i], i + 1);
    }

    void populateScales(DropdownSelect& d)
    {
        for (int i = 0; i < (int) kScales.size(); ++i)
            d.addItem(kScales[(size_t) i].name, i + 1);
    }

    void populateXmodModes(DropdownSelect& d)
    {
        d.addItem("Off",  1);
        d.addItem("FM",   2);
        d.addItem("Sync", 3);
    }

    void populateFilterTypes(DropdownSelect& d)
    {
        // First-stab labels — the real engine will narrow this. Same id span as
        // the AudioParameterInt 0..15 range so the attachment maps cleanly.
        const char* names[] = { "LP 12", "LP 24", "HP 12", "HP 24",
                                "BP 12", "BP 24", "Notch", "AP",
                                "Comb",  "1P LP", "1P HP", "Peak",
                                "Lo Shelf", "Hi Shelf", "—", "—" };
        for (int i = 0; i < 16; ++i)
            d.addItem(names[i], i + 1);
    }
}

VoicePanel::VoicePanel(PluginProcessor& p)
    : proc(p)
{
    auto& apvts = proc.apvts;

    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredRight);
        l.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        addAndMakeVisible(l);
    };

    // ── Tonal centre ────────────────────────────────────────────────────────
    setupLabel(rootLabel,  "Root");
    setupLabel(scaleLabel, "Scale");
    populateRoots(rootDropdown);
    populateScales(scaleDropdown);
    addAndMakeVisible(rootDropdown);
    addAndMakeVisible(scaleDropdown);
    rootAttachment  = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "root",  rootDropdown.getComboBox());
    scaleAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "scale", scaleDropdown.getComboBox());

    // ── Oscillator 1 ────────────────────────────────────────────────────────
    for (auto* k : { &o1OctKnob, &o1ToneKnob, &o1FineKnob, &o1PosKnob })
        addAndMakeVisible(k);
    o1OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, "o1_oct",  o1OctKnob.getSlider());
    o1ToneAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, "o1_tone", o1ToneKnob.getSlider());
    o1FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, "o1_fine", o1FineKnob.getSlider());
    o1PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, "o1_pos",  o1PosKnob.getSlider());

    // ── Oscillator 2 ────────────────────────────────────────────────────────
    for (auto* k : { &o2OctKnob, &o2ToneKnob, &o2FineKnob, &o2PosKnob })
        addAndMakeVisible(k);
    o2OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, "o2_oct",  o2OctKnob.getSlider());
    o2ToneAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, "o2_tone", o2ToneKnob.getSlider());
    o2FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, "o2_fine", o2FineKnob.getSlider());
    o2PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, "o2_pos",  o2PosKnob.getSlider());

    // ── Cross-mod + balance ─────────────────────────────────────────────────
    addAndMakeVisible(xmodKnob);
    setupLabel(xmodLabel, "Mode");
    populateXmodModes(xmodModeDropdown);
    addAndMakeVisible(xmodModeDropdown);
    addAndMakeVisible(mixKnob);
    xmodAttachment     = std::make_unique<APVTS::SliderAttachment>  (apvts, "xmod",  xmodKnob.getSlider());
    xmodModeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "xmode", xmodModeDropdown.getComboBox());
    mixAttachment      = std::make_unique<APVTS::SliderAttachment>  (apvts, "mix",   mixKnob.getSlider());

    // ── Filter ──────────────────────────────────────────────────────────────
    setupLabel(fltTypeLabel, "Type");
    populateFilterTypes(fltTypeDropdown);
    addAndMakeVisible(fltTypeDropdown);
    addAndMakeVisible(fltCutKnob);
    addAndMakeVisible(fltResKnob);
    fltTypeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "flt_type", fltTypeDropdown.getComboBox());
    fltCutAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, "flt_cut",  fltCutKnob.getSlider());
    fltResAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, "flt_res",  fltResKnob.getSlider());

    // ── Level ───────────────────────────────────────────────────────────────
    addAndMakeVisible(levelKnob);
    levelAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, "level", levelKnob.getSlider());
}

VoicePanel::~VoicePanel() = default;

void VoicePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.fillAll(MuLookAndFeel::colour(Id::windowBackground));
}

void VoicePanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int pad      = s(16);
    const int rowGap   = s(12);
    const int knobW    = s(80);
    const int knobH    = s(80);
    const int labelW   = s(48);
    const int ddH      = s(24);
    const int hgap     = s(8);

    // ── Row 1: tonal centre + filter ────────────────────────────────────────
    int y = pad;
    int x = pad;

    rootLabel.setBounds(x, y, labelW, ddH);            x += labelW + hgap;
    rootDropdown.setBounds(x, y, s(64), ddH);          x += s(64) + s(16);
    scaleLabel.setBounds(x, y, labelW, ddH);           x += labelW + hgap;
    scaleDropdown.setBounds(x, y, s(120), ddH);

    // Right cluster: filter type + cut + res
    int rx = w - pad;
    fltResKnob.setBounds(rx - knobW, y - s(2), knobW, knobH);  rx -= knobW + hgap;
    fltCutKnob.setBounds(rx - knobW, y - s(2), knobW, knobH);  rx -= knobW + hgap;
    fltTypeDropdown.setBounds(rx - s(100), y, s(100), ddH);    rx -= s(100) + hgap;
    fltTypeLabel.setBounds(rx - labelW, y, labelW, ddH);

    y += knobH + rowGap;

    // ── Row 2: Osc1 four knobs ──────────────────────────────────────────────
    x = pad;
    for (auto* k : { &o1OctKnob, &o1ToneKnob, &o1FineKnob, &o1PosKnob })
    {
        k->setBounds(x, y, knobW, knobH);
        x += knobW + hgap;
    }

    y += knobH + rowGap;

    // ── Row 3: Osc2 four knobs + xmod + level ───────────────────────────────
    x = pad;
    for (auto* k : { &o2OctKnob, &o2ToneKnob, &o2FineKnob, &o2PosKnob })
    {
        k->setBounds(x, y, knobW, knobH);
        x += knobW + hgap;
    }
    x += s(16);
    xmodKnob.setBounds(x, y, knobW, knobH);            x += knobW + hgap;
    xmodLabel.setBounds(x, y, labelW, ddH);            x += labelW + hgap;
    xmodModeDropdown.setBounds(x, y, s(80), ddH);      x += s(80) + s(16);
    mixKnob.setBounds(x, y, knobW, knobH);             x += knobW + s(16);
    levelKnob.setBounds(w - pad - knobW, y, knobW, knobH);
}

} // namespace mu_tant
