#include "RhythmPanel.h"

RhythmPanel::RhythmPanel(PluginProcessor& p)
    : proc(p)
{
    addAndMakeVisible(circle);
    addAndMakeVisible(euclidPanel);
    addAndMakeVisible(voiceSection);
    addAndMakeVisible(modulatorPanel);

    euclidPanel.onPatternChanged = [this]
    {
        if (currentRhythmIndex >= 0)
            proc.updatePattern(currentRhythmIndex);
        refreshCircle();
    };

    euclidPanel.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        if (onStatusUpdate) onStatusUpdate(name, val, currentColour());
    };

    voiceSection.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        if (onStatusUpdate) onStatusUpdate(name, val, currentColour());
    };
}

void RhythmPanel::setRhythm(int index)
{
    currentRhythmIndex = index;
    if (index >= 0 && index < proc.getNumRhythms())
    {
        euclidPanel.setRhythm(&proc.getRhythm(index));
        euclidPanel.setRhythmColour(currentColour());
        modulatorPanel.setRhythm(&proc.getRhythm(index));
        refreshCircle();
    }
    repaint();
}

void RhythmPanel::refreshCircle()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(currentRhythmIndex);
    circle.setPatterns(r.genA.getStepTypes(), r.genB.getStepTypes(), r.genC.getStepTypes());
}

juce::Colour RhythmPanel::currentColour() const
{
    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const Rhythm& r = proc.getRhythm(currentRhythmIndex);
        return MuClidLookAndFeel::rhythmPalette[r.colourIndex % 30];
    }
    return juce::Colours::transparentBlack;
}

bool RhythmPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || ext == ".mp3" || ext == ".flac")
            return true;
    }
    return false;
}

void RhythmPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile() && currentRhythmIndex >= 0)
        {
            loadedSampleNames[currentRhythmIndex] = file.getFileNameWithoutExtension();
            proc.loadSampleForRhythm(currentRhythmIndex, file);
            repaint();
            return;
        }
    }
}

void RhythmPanel::loadSample()
{
    const juce::File startDir = lastBrowseDir.isDirectory()
                                    ? lastBrowseDir
                                    : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Sample", startDir, "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile() && currentRhythmIndex >= 0)
            {
                lastBrowseDir = result.getParentDirectory();
                loadedSampleNames[currentRhythmIndex] = result.getFileNameWithoutExtension();
                proc.loadSampleForRhythm(currentRhythmIndex, result);
                repaint();
            }
        });
}

void RhythmPanel::mouseDown(const juce::MouseEvent& e)
{
    const juce::Rectangle<int> sampleBar { 0, kHeaderH, getWidth(), kSampleBarH };
    if (sampleBar.contains(e.getPosition()))
        loadSample();
}

void RhythmPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w = getWidth();

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Header
    juce::Colour col = currentColour();
    g.setColour(col);
    g.fillRect(0, 0, 4, kHeaderH);

    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const Rhythm& r = proc.getRhythm(currentRhythmIndex);
        g.setColour(col);
        g.fillEllipse(10.0f, (kHeaderH - 10) * 0.5f, 10.0f, 10.0f);
        g.setColour(MuClidLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(13.0f));
        g.drawText(juce::String(r.name), 26, 0, w - 32, kHeaderH,
                   juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(13.0f));
        g.drawText("No Rhythm", 26, 0, w - 32, kHeaderH,
                   juce::Justification::centredLeft, true);
    }

    // Sample bar — content inset from panel outline
    {
        const auto inner = sampleRect.reduced(3);
        g.setColour(MuClidLookAndFeel::colour(Id::sampleBarBackground));
        g.fillRect(inner);

        auto it = loadedSampleNames.find(currentRhythmIndex);
        if (it != loadedSampleNames.end())
        {
            g.setColour(MuClidLookAndFeel::colour(Id::labelText));
            g.setFont(juce::Font(10.0f));
            g.drawText(it->second,
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(MuClidLookAndFeel::colour(Id::sampleBarNoSample));
            g.setFont(juce::Font(10.0f).italicised());
            g.drawText("drop sample here or click to browse",
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }

        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(11.0f));
        g.drawText("...", inner.getRight() - 24, inner.getY(), 24, inner.getHeight(),
                   juce::Justification::centred, false);
    }

    // Major panel outlines — 2px in rhythm colour, rounded corners.
    // Each rect is inset by 1px so adjacent panels have a consistent 2px gap.
    g.setColour(col);
    g.drawRoundedRectangle(sampleRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(circleRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(euclidRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(voiceRect.reduced(2).toFloat(),  6.0f, 2.0f);
    g.drawRoundedRectangle(modRect.reduced(2).toFloat(),    6.0f, 2.0f);
}

void RhythmPanel::resized()
{
    const int w        = getWidth();
    const int h        = getHeight();
    const int contentH = h - kHeaderH - kSampleBarH - kVoiceH;

    // Top section: 55% of content height; circle is square, capped at 33% of panel width.
    // This keeps sizing proportional across the full 780–2400 × 580–1600 resize range.
    topH    = juce::jmax(80,  (int)(contentH * 0.55f));
    circleW = juce::jmin(topH, (int)(w * 0.33f));

    const int topY = kHeaderH + kSampleBarH;
    const int modY = topY + topH + kVoiceH;

    sampleRect = { 0,       kHeaderH,    w,           kSampleBarH             };
    circleRect = { 0,       topY,        circleW,     topH                    };
    euclidRect = { circleW, topY,        w - circleW, topH                    };
    voiceRect  = { 0,       topY + topH, w,           kVoiceH                 };
    modRect    = { 0,       modY,        w,           juce::jmax(0, h - modY) };

    circle.setBounds        (circleRect.reduced(kPanelPad + 1));
    euclidPanel.setBounds   (euclidRect.reduced(kPanelPad + 1));
    voiceSection.setBounds  (voiceRect.reduced(kPanelPad + 1));
    modulatorPanel.setBounds(modRect.reduced(kPanelPad + 1));
}
