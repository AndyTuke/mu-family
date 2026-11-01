#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>
#include <vector>

// A zoomable, scrollable waveform of a whole loop with editable markers (docs/mu-tilate/design-ui.md, "Source"): the audio
// is summarised once into min/max blocks, so a repaint costs about one column of work per pixel whatever the loop's length.
// The mouse wheel zooms around the pointer; Shift+wheel (or a horizontal wheel) scrolls; a middle-button or Ctrl drag pans. A marker can be dragged, a double-click on empty waveform asks for a
// new one, Alt-click (or right-click) on a marker asks to delete it, and a click between markers selects that region. The
// component edits nothing itself: it reports what the user asked for through the callbacks and shows what it is given.
class LoopWaveform : public juce::Component
{
public:
    struct Marker
    {
        int  position = 0;      // samples
        bool pinned   = false;  // a hand-placed marker: drawn with a pin
    };

    LoopWaveform() = default;

    // The audio to show (mono mix of every channel for display); nullptr clears it.
    void setAudio(std::shared_ptr<const juce::AudioBuffer<float>> audio);
    // The markers, ascending. Marker 0 is the loop's start and cannot be moved or deleted.
    void setMarkers(std::vector<Marker> newMarkers);
    // The region (between marker i and i + 1, the last to the end of the loop) shown tinted; -1 for none.
    void setSelectedRegion(int index);
    // The playhead in samples, or a negative value for none.
    void setPlayhead(double samples);

    int getSelectedRegion() const noexcept { return selected; }

    // Zoom to show the whole loop again.
    void showAll();

    // A marker was dragged: its index and new position (called while dragging and on release).
    std::function<void(int index, int newPosition)> onMarkerMoved;
    // A double-click on empty waveform at this position asked for a new marker.
    std::function<void(int position)> onMarkerAddRequested;
    // Alt-click or right-click on this marker asked to delete it.
    std::function<void(int index)> onMarkerDeleteRequested;
    // A click between markers: the region's index.
    std::function<void(int region)> onRegionClicked;
    // A region was dragged away from where it was pressed: its index and the pointer in screen coordinates (while dragging),
    // then the same once on release. The host decides what is under the pointer and whether a drop is accepted.
    std::function<void(int region, juce::Point<int> screenPos)> onRegionDragged;
    std::function<void(int region, juce::Point<int> screenPos)> onRegionDropped;

    // Zooms by `factor` (above 1 zooms in) keeping the sample at x (pixels) where it is; for buttons and the wheel.
    void zoomBy(double factor, int anchorX);

    void paint(juce::Graphics&) override;
    void resized() override { clampView(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    static constexpr int kBlock = 64;       // samples per summary block

    int  length() const noexcept { return audio != nullptr ? audio->getNumSamples() : 0; }
    double samplesPerPixel() const noexcept { return getWidth() > 0 ? viewLength / (double) getWidth() : 1.0; }
    int  xOf(double sample) const noexcept { return (int) std::lround((sample - viewStart) / samplesPerPixel()); }
    int  sampleAt(int x) const noexcept { return (int) std::lround(viewStart + (double) x * samplesPerPixel()); }
    int  markerNear(int x) const noexcept;   // index of the marker within the grab distance of x, or -1
    int  regionAt(int sample) const noexcept;
    void clampView() noexcept;

    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    std::vector<float> summaryMin, summaryMax;      // per kBlock samples, mono mix
    std::vector<Marker> markers;
    int    selected = -1;
    double playhead = -1.0;

    double viewStart = 0.0;     // first visible sample
    double viewLength = 1.0;    // visible samples
    int    dragging = -1;       // marker being dragged
    int    pressedRegion = -1;  // the region a press landed in, until the drag gesture is decided
    bool   regionDragging = false;
    bool   panning = false;
    double panStartView = 0.0;
    int    hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopWaveform)
};
