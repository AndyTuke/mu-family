#pragma once

#include "FXSlotBase.h"
#include "FXAlgorithmDef.h"
#include <atomic>
#include <vector>

// Send-style reverb slot using Signalsmith FDN reverb (MIT, header-only).
// Dry signal is unaffected — wet reverb is added on top.
class ReverbSlot : public FXSlotBase
{
public:
    enum class Algorithm { Room = 0, Hall, Plate, Spring };

    ReverbSlot();
    ~ReverbSlot();

    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>&) override;

    juce::String getName()     override { return "Reverb"; }
    juce::String getCategory() override { return "Send"; }
    juce::Component* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool isEnabled() const     { return enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool e)    { enabled.store(e, std::memory_order_relaxed); }
    void setLevel(float v)     { level.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed); }

    // Send-bus processing: overwrites buffer with wet-only reverb output.
    void processReturn(juce::AudioBuffer<float>&);

    void setAlgorithm(int index);
    int  getAlgorithmIndex() const { return algorithmIndex; }

    void setParam(const juce::String& id, float value);

    // Per-param getters — used by the UI / APVTS-push path right after
    // setAlgorithm to read the algorithm's new defaults out of the slot and
    // push them into APVTS so the visible knobs match the audible state.
    float getSize()      const noexcept { return size; }
    float getPreDelay()  const noexcept { return preDelay.load(std::memory_order_relaxed); }
    float getDiffusion() const noexcept { return diffusion; }
    float getDamp()      const noexcept { return damp; }
    float getMod()       const noexcept { return mod; }
    float getDirt()      const noexcept { return dirt.load(std::memory_order_relaxed); }

    static std::vector<FXAlgorithmDef> allDefs() { return FXAlgorithmRegistry::reverbAlgorithms(); }

private:
    void applyAlgorithmPreset();
    void updateReverb();
    // copy any pending param snapshot into the Signalsmith reverb's public
    // fields. Audio thread only — call at the top of processReturn before reverb.process.
    void applyPendingReverbParams();
    void runPreDelay(const juce::AudioBuffer<float>& src, int numSamples);

    // fields read on the audio thread are atomic to avoid torn reads when
    // setEnabled / setLevel / setParam writes from the message thread.
    std::atomic<bool>  enabled  { true };
    std::atomic<float> level    { 1.0f };
    int                algorithmIndex = 0;

    float              size      = 0.5f;
    std::atomic<float> preDelay  { 10.0f };   // read by runPreDelay (audio thread)
    float              diffusion = 0.7f;
    float              damp      = 0.4f;
    float              mod       = 0.2f;
    std::atomic<float> dirt      { 0.0f };    // read by processReturn (audio thread)
    float              rt20      = 1.0f;      // decay time to -20dB (set per algorithm)

    double sr = 44100.0;

    // Pre-delay buffer
    static constexpr int MaxPreDelaySamples = 192000 / 10;  // ~100ms at 192kHz
    std::vector<float> preDelayBufL, preDelayBufR;
    int preDelayWrite = 0;

    // Wet work buffers (allocated in prepare, never in process)
    std::vector<float> wetL, wetR;

    // Signalsmith FDN reverb — included in the .cpp to keep windows.h out of this header
    struct ReverbImpl;
    std::unique_ptr<ReverbImpl> impl;
};
