#include "LayerPanel.h"
#include "UI/LayerPresetHeader.h"

LayerPanel::LayerPanel(ProcessorBase& processor, const juce::String& noun)
    : proc(processor)
{
    beatSource = [this] { return proc.getInternalBeatPos(); };

    // Reset / preset load / save: the shared per-slot wiring.
    mu_ui::wireSlotPresetHeader(header, *this, proc,
        { noun,
          [this] { return currentLayer; },
          [this] { setLayer(currentLayer); },   // rebind so the content and modulators show the new state
          {} });
    addAndMakeVisible(header);
    addAndMakeVisible(modulatorPanel);

    refreshPresetList();
    startTimerHz(mu_ui::kUiRefreshHz);
}

LayerPanel::~LayerPanel()
{
    stopTimer();
    modulatorPanel.setVoiceSlot(nullptr);   // the layer can outlive the panel
}

void LayerPanel::setContent(juce::Component* c)
{
    if (content == c) return;
    if (content != nullptr) removeChildComponent(content);
    content = c;
    if (content != nullptr) addAndMakeVisible(*content);
    resized();
}

void LayerPanel::setFixedLayers(bool fixed)
{
    header.setShowDelete(! fixed);
    header.setNameEditable(! fixed);
}

void LayerPanel::setLayer(int idx)
{
    header.commitNameEdit();   // a rename in progress belongs to the layer being left
    currentLayer = juce::jmax(0, idx);

    modulatorPanel.setVoiceSlot(proc.getLayer(currentLayer));
    refreshHeader();
    refreshPresetList();
    if (onLayerChanged) onLayerChanged(currentLayer);
    repaint();
}

void LayerPanel::refreshHeader()
{
    header.setLayerName(proc.getChannelName(currentLayer));
    header.setColour(layerColour());
    header.setSelectedPresetId(0);
}

void LayerPanel::refreshPresetList()
{
    mu_ui::refreshSlotPresetList(header, proc, currentLayer);
}

juce::Colour LayerPanel::layerColour() const
{
    return MuLookAndFeel::channelPalette[(size_t) (proc.getChannelColourIndex(currentLayer)
                                                   % MuLookAndFeel::kChannelPaletteSize)];
}

void LayerPanel::timerCallback()
{
    modulatorPanel.setPlayheadBeat(beatSource());
    header.setStagingBadge(proc.hasPendingSwap(currentLayer));   // "SWP" while a layer preset waits for its boundary
}

void LayerPanel::paint(juce::Graphics& g)
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::ColourIds::panelBackground));
    const auto accent = MuLookAndFeel::appAccent(*this);
    for (auto r : { headerR, modR })
        MuLookAndFeel::drawAccentPanel(g, r.reduced(2).toFloat(), accent);
}

void LayerPanel::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    MuLookAndFeel::drawStripScrews(g, headerR.reduced(2).toFloat());   // the thin preset strip
    MuLookAndFeel::drawPanelScrews(g, modR.reduced(2).toFloat());
}

void LayerPanel::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int padX = s(LF::kScrewedPanelInset);   // content in from a panel's sides
    const int padY = s(LF::kChannelInset);        //   … and from its top / bottom

    // Band 1: the preset strip. With screws the header is narrower, leaving a screw at each end.
    headerR = { 0, 0, w, s(LF::kLayerHeaderStripH) };
    header.setBounds(headerR.reduced(LF::hasScrews(*this) ? padX : s(4), s(2)));

    // Band 2: the product's content, a fixed height.
    const juce::Rectangle<int> contentR { 0, headerR.getBottom(), w, s(LF::kLayerContentH) };
    if (content != nullptr) content->setBounds(contentR);

    // Band 3: the modulators take whatever is left, so lost height (banners) comes off this band only.
    modR = { 0, contentR.getBottom(), w, juce::jmax(0, h - contentR.getBottom()) };
    modulatorPanel.setBounds(modR.reduced(padX, padY));
}
