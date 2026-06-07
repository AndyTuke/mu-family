#include "EnginePanel.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

namespace
{
    struct Spec { const char* id; const char* label; };

    // The engine controls for each lane (in display order).
    std::vector<Spec> specsFor(int channel)
    {
        switch (channel)
        {
            case Kick:  return { {"k_tune","Tune"}, {"k_ptch","Pitch"}, {"k_pdec","P.Dec"}, {"k_adec","Decay"}, {"k_drive","Drive"} };
            // Bass also exposes its sidechain-to-Kick controls (the key interaction):
            // Duck depth + envelope — small Duck = smooth, large = pumping.
            case Bass:  return { {"b_wave","Wave"}, {"b_tune","Tune"}, {"b_sub","Sub"}, {"b_cut","Cutoff"}, {"b_res","Reso"},
                                 {"b_env","F.Env"}, {"b_edec","E.Dec"}, {"b_atk","Atk"}, {"b_dec","Dec"}, {"b_sus","Sus"}, {"b_drive","Drive"},
                                 {"ch1_scAmt","Duck"}, {"ch1_scAtk","D.Atk"}, {"ch1_scRel","D.Rel"} };
            case Hat:   return { {"h_tune","Tune"}, {"h_dec","Decay"} };
            case Snare: return { {"s_tune","Tune"}, {"s_dec","Decay"} };
            default:    return {};
        }
    }
}

EnginePanel::EnginePanel(ProcessorBase& processor) : proc(processor)
{
    rebuild();
}

void EnginePanel::setChannel(int idx)
{
    if (idx == currentChannel && ! controls.empty()) return;
    currentChannel = idx;
    rebuild();
    repaint();
}

void EnginePanel::rebuild()
{
    controls.clear();

    for (const auto& spec : specsFor(currentChannel))
    {
        auto ctl = std::make_unique<Control>();
        ctl->label = spec.label;

        ctl->name.setText(spec.label, juce::dontSendNotification);
        ctl->name.setJustificationType(juce::Justification::centred);
        ctl->name.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(ctl->name);

        // Choice params get a combo box; everything else a rotary knob.
        if (dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(spec.id)) != nullptr)
        {
            ctl->combo = std::make_unique<juce::ComboBox>();
            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(spec.id)))
                ctl->combo->addItemList(cp->choices, 1);
            addAndMakeVisible(*ctl->combo);
            ctl->comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, spec.id, *ctl->combo);
        }
        else
        {
            ctl->knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            addAndMakeVisible(*ctl->knob);
            ctl->knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, spec.id, *ctl->knob);
        }
        controls.push_back(std::move(ctl));
    }
    resized();
}

void EnginePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const auto accent = MuLookAndFeel::channelPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1, proc.getChannelColourIndex(currentChannel))];

    auto header = getLocalBounds().removeFromTop(26).reduced(8, 2);
    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText(proc.getChannelName(currentChannel) + "  engine", header, juce::Justification::centredLeft, false);
}

void EnginePanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(28).reduced(8, 4);
    if (controls.empty()) return;

    // Wrapping grid: fit as many ~64 px columns per row as the width allows, then wrap.
    const int n      = (int) controls.size();
    const int colW   = 64;
    const int perRow = juce::jlimit(1, n, juce::jmax(1, area.getWidth() / colW));
    const int rows   = (n + perRow - 1) / perRow;
    const int cellW  = area.getWidth() / perRow;
    const int cellH  = juce::jmax(40, area.getHeight() / juce::jmax(1, rows));
    const int knobH  = juce::jmin(cellH - 16, cellW - 8);

    for (int i = 0; i < n; ++i)
    {
        const int r = i / perRow, c = i % perRow;
        juce::Rectangle<int> cell(area.getX() + c * cellW, area.getY() + r * cellH, cellW, cellH);
        controls[(size_t) i]->name.setBounds(cell.removeFromTop(13));
        auto ctlR = cell.removeFromTop(knobH).reduced(4, 0);
        if (controls[(size_t) i]->combo) controls[(size_t) i]->combo->setBounds(ctlR.removeFromTop(24));
        else if (controls[(size_t) i]->knob) controls[(size_t) i]->knob->setBounds(ctlR);
    }
}

} // namespace mu_on
