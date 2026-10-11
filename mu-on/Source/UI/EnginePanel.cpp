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
            case Kick:  return { {layerParamId(Kick,"tune"),"Tune"}, {layerParamId(Kick,"ptch"),"Pitch"}, {layerParamId(Kick,"pdec"),"P.Dec"}, {layerParamId(Kick,"adec"),"Decay"}, {layerParamId(Kick,"drive"),"Drive"} };
            // Bass also exposes its sidechain-to-Kick controls (the key interaction):
            // Duck depth + envelope — small Duck = smooth, large = pumping.
            case Bass:  return { {layerParamId(Bass,"wave"),"Wave"}, {layerParamId(Bass,"tune"),"Tune"}, {layerParamId(Bass,"sub"),"Sub"}, {layerParamId(Bass,"cut"),"Cutoff"}, {layerParamId(Bass,"res"),"Reso"},
                                 {layerParamId(Bass,"env"),"F.Env"}, {layerParamId(Bass,"edec"),"E.Dec"}, {layerParamId(Bass,"atk"),"Atk"}, {layerParamId(Bass,"dec"),"Dec"}, {layerParamId(Bass,"sus"),"Sus"}, {layerParamId(Bass,"drive"),"Drive"},
                                 {"ch1_scAmt","Duck"}, {"ch1_scAtk","D.Atk"}, {"ch1_scRel","D.Rel"} };
            case Hat:   return { {layerParamId(Hat,"tune"),"Tune"}, {layerParamId(Hat,"dec"),"Decay"} };
            case Snare: return { {layerParamId(Snare,"tune"),"Tune"}, {layerParamId(Snare,"dec"),"Decay"} };
            // Rumble processes the Kick feed: input drive → 3 delay taps → reverb → filter.
            case Rumble: return { {layerParamId(Rumble,"drive"),"Drive"}, {layerParamId(Rumble,"d1"),"1/16"}, {layerParamId(Rumble,"d2"),"2/16"}, {layerParamId(Rumble,"d3"),"3/16"},
                                  {layerParamId(Rumble,"size"),"Rv Size"}, {layerParamId(Rumble,"revmix"),"Rv Mix"}, {layerParamId(Rumble,"revlp"),"Rv LP"}, {layerParamId(Rumble,"cut"),"Cutoff"}, {layerParamId(Rumble,"res"),"Reso"} };
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
