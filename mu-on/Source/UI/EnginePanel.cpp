#include "EnginePanel.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

namespace
{
    using Spec = mu_ui::ParamKnobGrid::Spec;

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
            // Rumble processes the Kick feed: input drive → 3 delay taps → reverb → filter.
            case Rumble: return { {"r_drive","Drive"}, {"r_d1","1/16"}, {"r_d2","2/16"}, {"r_d3","3/16"},
                                  {"r_size","Rv Size"}, {"r_revmix","Rv Mix"}, {"r_revlp","Rv LP"}, {"r_cut","Cutoff"}, {"r_res","Reso"} };
            default:    return {};
        }
    }
}

EnginePanel::EnginePanel(ProcessorBase& processor, int lane)
    : proc(processor), currentChannel(lane), grid(processor.apvts)
{
    addAndMakeVisible(grid);
    grid.setSpecs(specsFor(currentChannel));
}

void EnginePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    // Metal style: the host's raised box shows through, named on its plate by the host.
    if (MuLookAndFeel::isMetal(*this))
        return;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const auto accent = MuLookAndFeel::channelPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1, proc.getChannelColourIndex(currentChannel))];

    auto header = getLocalBounds().removeFromTop(mu_ui::s(26)).reduced(mu_ui::s(8), mu_ui::s(2));
    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(16.0f), juce::Font::bold)));
    g.drawText(proc.getChannelName(currentChannel) + "  engine", header, juce::Justification::centredLeft, false);
}

void EnginePanel::resized()
{
    // Metal: the grid fills the host's box. Flat: the header takes the top 28 px (drawn in paint).
    if (MuLookAndFeel::isMetal(*this))
        grid.setBounds(getLocalBounds());
    else
        grid.setBounds(getLocalBounds().withTrimmedTop(mu_ui::s(28)).reduced(mu_ui::s(8), mu_ui::s(4)));
}

} // namespace mu_on
