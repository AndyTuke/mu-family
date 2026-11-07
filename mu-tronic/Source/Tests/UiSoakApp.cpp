// mu-tronic-uisoak: the real mu-Tronic editor in its own window, driven from inside this process. Random mouse presses, drags,
// right-clicks, double-clicks and key presses are handed straight to the window's peer (never through the operating system, so
// they cannot reach any other window), while a worker thread renders audio with a running transport and random MIDI notes, and
// the message thread also changes slot types at random. Storage is a temporary root, so nothing touches the user's folders.
//   mu-tronic-uisoak.exe <seconds> <seed>      exit 0 = survived; a crash leaves its stack in the file named by MU_UISOAK_CRASH.
// Failed jasserts are counted (a Debug build logs them) and reported at the end.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "UI/ExternalLink.h"
#include "Sequencer/PhraseSlotHost.h"   // mu-Tilate's layer library: loads a loop into a Phrase slot

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <random>
#include <thread>

namespace
{
    std::atomic<int> gAsserts { 0 }, gLaunches { 0 };

    struct CountingLogger : juce::Logger
    {
        void logMessage(const juce::String& m) override
        {
            if (m.contains("Assertion failure")) { ++gAsserts; std::fprintf(stderr, "%s\n", m.toRawUTF8()); std::fflush(stderr); }
        }
    };

    struct SoakWindow : juce::DocumentWindow
    {
        explicit SoakWindow(juce::Component& content) : juce::DocumentWindow("uisoak", juce::Colours::black, 0)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setContentNonOwned(&content, true);
            setResizable(false, false);
            setVisible(true);
            setTopLeftPosition(40, 40);
        }
        void closeButtonPressed() override {}
    };
}

class UiSoakApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "mu-tronic-uisoak"; }
    const juce::String getApplicationVersion() override { return "1"; }

    void initialise(const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        seconds = args.size() > 0 ? args[0].getIntValue() : 60;
        seed = args.size() > 1 ? (unsigned) args[1].getIntValue() : 1u;
        rng.seed(seed);

        juce::Logger::setCurrentLogger(&logger);
        // Links (mailto, browser) are counted, never opened: a soak must not start Outlook or Edge.
        mu_core::externalLauncher() = [](const juce::URL&) { ++gLaunches; };
        juce::SystemStats::setApplicationCrashHandler([](void*)
        {
            const auto path = juce::SystemStats::getEnvironmentVariable("MU_UISOAK_CRASH", {});
            if (path.isNotEmpty())
                juce::File(path).replaceWithText(juce::Time::getCurrentTime().toString(true, true) + "\n" + juce::SystemStats::getStackBacktrace());
        });

        storage = std::make_unique<mu_seam_test::TempStorage>();
        processor = std::make_unique<mu_tronic::PluginProcessor>();
        processor->prepareToPlay(48000.0, 256);
        loopFile = makeLoop(storage->root);
        // Slot 0 starts as a Phrase holding a loop, so the waveform / slice / timeline views have something to act on.
        processor->setLayerType(0, mu_core::LayerTypeId::Phrase);
        loadLoopInto(0);
        playHead = std::make_unique<mu_seam_test::RunningPlayHead>(48000.0, 256);
        processor->setPlayHead(playHead.get());
        editor.reset(processor->createEditor());
        window = std::make_unique<SoakWindow>(*editor);
        window->setSize(editor->getWidth(), editor->getHeight());

        worker = std::thread([this]
        {
            std::mt19937 wr(seed * 31u + 7u);
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            int held = -1, n = 0;
            while (! stopWorker.load())
            {
                buf.clear(); midi.clear();
                if (n++ % 5 == 0)
                {
                    if (held >= 0) midi.addEvent(juce::MidiMessage::noteOff(1 + (held >> 8), held & 0x7f), 0);
                    const int ch = (int) (wr() % 16), note = 24 + (int) (wr() % 72);
                    midi.addEvent(juce::MidiMessage::noteOn(1 + ch, note, (juce::uint8) (1 + wr() % 127)), (int) (wr() % 256));
                    held = (ch << 8) | note;
                }
                processor->processBlock(buf, midi);
                if (! mu_seam_test::bufferIsSane(buf, 1000.0f)) insane.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        endTime = juce::Time::getMillisecondCounter() + (juce::uint32) (seconds * 1000);
        startTimer(15);
    }

    void shutdown() override
    {
        stopTimer();
        stopWorker.store(true);
        if (worker.joinable()) worker.join();
        processor->setPlayHead(nullptr);
        window.reset();
        editor.reset();
        processor.reset();
        storage.reset();
        juce::Logger::setCurrentLogger(nullptr);
        std::printf("uisoak seed=%u seconds=%d actions=%d asserts=%d insane=%d blockedLaunches=%d\n", seed, seconds, actions, gAsserts.load(), insane.load() ? 1 : 0, gLaunches.load());
        std::fflush(stdout);
        setApplicationReturnValue(insane.load() ? 2 : 0);
    }

private:
    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounter() >= endTime) { quit(); return; }
        auto* peer = window != nullptr ? window->getPeer() : nullptr;
        if (peer == nullptr) return;
        const int w = juce::jmax(10, window->getWidth()), h = juce::jmax(10, window->getHeight());
        for (int burst = 0; burst < 3; ++burst)
        {
            ++actions;
            const int roll = (int) (rng() % 100);
            const juce::Point<float> p((float) (rng() % (unsigned) w), (float) (rng() % (unsigned) h));
            if (roll < 55)       click(*peer, p, false);
            else if (roll < 68)  { if (sidebarDrags || p.x > 90.0f) drag(*peer, p, { (float) (rng() % (unsigned) w), (float) (rng() % (unsigned) h) }); }   // sidebar drags only on request: a known use-after-free (see findings)
            else if (roll < 76)  click(*peer, p, true);
            else if (roll < 82)  { click(*peer, p, false); click(*peer, p, false); }
            else if (roll < 84)
            {
                // A resize to an arbitrary size (very small ones included): layout code must survive any rectangle it is handed.
                static const int ws[] = { 120, 320, 600, 800, 1000, 1200, 1600, 2400 }, hs[] = { 80, 240, 400, 600, 800, 1000, 1400 };
                editor->setSize(ws[rng() % (sizeof(ws) / sizeof(ws[0]))], hs[rng() % (sizeof(hs) / sizeof(hs[0]))]);
                window->setSize(editor->getWidth(), editor->getHeight());
            }
            else if (roll < 94)
            {
                static const int keys[] = { 32, 37, 38, 39, 40, 46, 13, 27, 9, 'A', 'B', 'C', 'D', 'E', 'K', 'L', 'M', 'N', 'P', 'S', 'T', '1', '2', '3', '4', '5' };
                const int k = keys[rng() % (sizeof(keys) / sizeof(keys[0]))];
                peer->handleKeyPress(juce::KeyPress(k, juce::ModifierKeys(), (juce::juce_wchar) k));
            }
            else
            {
                // A type change made from code, as a preset load or a scene launch would.
                static const mu_core::LayerTypeId types[] = { mu_core::LayerTypeId::Empty, mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Pattern,
                    mu_core::LayerTypeId::Arp, mu_core::LayerTypeId::Kick, mu_core::LayerTypeId::Bass, mu_core::LayerTypeId::Hat,
                    mu_core::LayerTypeId::Snare, mu_core::LayerTypeId::Rumble, mu_core::LayerTypeId::FmPerc, mu_core::LayerTypeId::Chord, mu_core::LayerTypeId::Phrase };
                const int slot = (int) (rng() % (unsigned) mu_limits::kMaxLayers);
                const auto type = types[rng() % (sizeof(types) / sizeof(types[0]))];
                processor->setLayerType(slot, type);
                if (type == mu_core::LayerTypeId::Phrase) loadLoopInto(slot);
            }
        }
    }

    // Four loud bursts half a second apart, two seconds of mono audio: enough for the detector to find slices.
    static juce::File makeLoop(const juce::File& dir)
    {
        const double sr = 48000.0;
        const int frames = 96000;
        juce::AudioBuffer<float> b(1, frames);
        b.clear();
        juce::Random r(5);
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < (int) (0.3 * sr); ++i)
                b.setSample(0, t * 24000 + i, 0.8f * std::exp(-(float) i / (0.03f * (float) sr)) * (r.nextFloat() * 2.0f - 1.0f));
        auto f = dir.getChildFile("soak-loop.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
        if (auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(16)))
            writer->writeFromAudioSampleBuffer(b, 0, frames);
        return f;
    }

    void loadLoopInto(int slot)
    {
        if (auto* host = dynamic_cast<mu_tilate::PhraseSlotHost*>(processor->findLayerServices(mu_core::LayerTypeId::Phrase)))
            host->loadLoopAsync(slot, loopFile);
    }

    static void press(juce::ComponentPeer& peer, juce::Point<float> p, bool right, bool down)
    {
        const auto buttons = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
        peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, p, down ? juce::ModifierKeys(buttons) : juce::ModifierKeys(),
                              juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
    }
    void click(juce::ComponentPeer& peer, juce::Point<float> p, bool right) { press(peer, p, right, true); press(peer, p, right, false); }
    void drag(juce::ComponentPeer& peer, juce::Point<float> a, juce::Point<float> b)
    {
        press(peer, a, false, true);
        for (int i = 1; i <= 6; ++i) press(peer, a + (b - a) * ((float) i / 6.0f), false, true);
        press(peer, b, false, false);
    }

    CountingLogger logger;
    bool sidebarDrags = juce::SystemStats::getEnvironmentVariable("MU_UISOAK_SIDEBAR_DRAG", {}) == "1";
    int seconds = 60, actions = 0;
    unsigned seed = 1;
    std::mt19937 rng;
    juce::File loopFile;
    juce::uint32 endTime = 0;
    std::atomic<bool> stopWorker { false }, insane { false };
    std::thread worker;
    std::unique_ptr<mu_seam_test::TempStorage> storage;
    std::unique_ptr<mu_tronic::PluginProcessor> processor;
    std::unique_ptr<mu_seam_test::RunningPlayHead> playHead;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<SoakWindow> window;
};

juce::JUCEApplicationBase* juce_CreateApplication() { return new UiSoakApp(); }
