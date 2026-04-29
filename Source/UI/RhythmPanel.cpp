#include "RhythmPanel.h"

RhythmPanel::RhythmPanel(PluginProcessor& p)
    : proc(p)
{
    addAndMakeVisible(circle);
    addAndMakeVisible(euclidPanel);
    addAndMakeVisible(voiceSection);

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
        refreshCircle();
    }
    repaint();
}

void RhythmPanel::refreshCircle()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(currentRhythmIndex);
    circle.setPatterns(r.genA.getPattern(), r.genB.getPattern());
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
            proc.loadSampleForRhythm(currentRhythmIndex, file);
            repaint();
            return;
        }
    }
}

void RhythmPanel::loadSample()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Sample",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile() && currentRhythmIndex >= 0)
                proc.loadSampleForRhythm(currentRhythmIndex, result);
        });
}

void RhythmPanel::mouseDown(const juce::MouseEvent& e)
{
    // Click inside the sample bar area opens file chooser
    const juce::Rectangle<int> sampleBar { 0, kHeaderH, getWidth(), kSampleBarH };
    if (sampleBar.contains(e.getPosition()))
        loadSample();
}

void RhythmPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w = getWidth();
    const int h = getHeight();

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // ── Header bar ────────────────────────────────────────────────────────────
    juce::Colour col = currentColour();

    // Left accent strip
    g.setColour(col);
    g.fillRect(0, 0, 4, kHeaderH);

    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const Rhythm& r = proc.getRhythm(currentRhythmIndex);

        // Colour dot
        g.setColour(col);
        g.fillEllipse(10.0f, (kHeaderH - 10) * 0.5f, 10.0f, 10.0f);

        // Name
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

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)kHeaderH, (float)w, (float)kHeaderH, 0.5f);

    // ── Sample bar ────────────────────────────────────────────────────────────
    const int sampleY = kHeaderH;
    g.setColour(MuClidLookAndFeel::colour(Id::sampleBarBackground));
    g.fillRect(0, sampleY, w, kSampleBarH);

    g.setColour(MuClidLookAndFeel::colour(Id::sampleBarNoSample));
    g.setFont(juce::Font(10.0f).italicised());
    g.drawText("drop sample here or click to browse",
               8, sampleY, w - 32, kSampleBarH,
               juce::Justification::centredLeft, true);

    // Browse icon (folder arrow)
    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(11.0f));
    g.drawText("...", w - 28, sampleY, 24, kSampleBarH,
               juce::Justification::centred, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)(sampleY + kSampleBarH),
               (float)w, (float)(sampleY + kSampleBarH), 0.5f);

    // ── Voice section separator ────────────────────────────────────────────────
    const int voiceY = kHeaderH + kSampleBarH + kTopH;
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)voiceY, (float)w, (float)voiceY, 0.5f);

    // ── Modulator placeholder ─────────────────────────────────────────────────
    const int modY = voiceY + kVoiceH;
    const int modH = h - modY;
    if (modH > 0)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawLine(0.0f, (float)modY, (float)w, (float)modY, 0.5f);
        g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(11.0f));
        g.drawText("Modulator Panel  ·  Stage 7", 0, modY, w, modH,
                   juce::Justification::centred, false);
    }

    // ── Circle / EuclideanPanel divider ───────────────────────────────────────
    const int topY = kHeaderH + kSampleBarH;
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float)kCircleW, (float)topY,
               (float)kCircleW, (float)(topY + kTopH), 0.5f);
}

void RhythmPanel::resized()
{
    const int topY = kHeaderH + kSampleBarH;

    circle.setBounds(0, topY, kCircleW, kTopH);
    euclidPanel.setBounds(kCircleW, topY, getWidth() - kCircleW, kTopH);
    voiceSection.setBounds(0, topY + kTopH, getWidth(), kVoiceH);
}
