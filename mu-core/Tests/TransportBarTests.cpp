#include "TestProcessorBase.h"
#include "TestShellBackend.h"
#include "UI/TransportBar.h"

// Layout checks for the shared transport bar's Tap / Nudge option (design-ui mu-link section 2).
namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// The button whose text is `text`, or nullptr.
juce::TextButton* findButton(juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            if (b->getButtonText() == text) return b;
    return nullptr;
}
} // namespace

struct TransportBarTests : juce::UnitTest
{
    TransportBarTests() : juce::UnitTest("TransportBar tap and nudge option", "mu-core") {}

    void runTest() override
    {
        ScaleGuard guard;
        mu_ui::scale = 1.0f;
        TestProcessorBase proc(juce::AudioProcessor::wrapperType_Standalone);

        beginTest("default: Tap and both Nudge buttons are laid out inside the bar");
        TransportBar shown(proc);
        shown.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
        auto* tap = findButton(shown, "Tap");
        auto* slow = findButton(shown, "-");
        auto* fast = findButton(shown, "+");
        expect(tap != nullptr && slow != nullptr && fast != nullptr, "buttons exist");
        if (tap == nullptr || slow == nullptr || fast == nullptr) return;
        expect(tap->isVisible() && slow->isVisible() && fast->isVisible(), "all visible by default");
        expect(! slow->getBounds().isEmpty() && fast->getBounds().getRight() <= shown.getWidth(), "nudge inside the bar");
        const auto shownRight = fast->getBounds().getRight();

        beginTest("hidden: Tap and Nudge are not visible, the lamp slot stays");
        TransportBar hidden(proc);
        hidden.setShowTapAndNudge(false);
        hidden.setBounds(0, 0, shown.getWidth(), shown.getHeight());
        auto* tap2 = findButton(hidden, "Tap");
        auto* slow2 = findButton(hidden, "-");
        auto* fast2 = findButton(hidden, "+");
        expect(! tap2->isVisible() && ! slow2->isVisible() && ! fast2->isVisible(), "all hidden");
        expect(tap2->getBounds() == tap->getBounds(), "the Tap / lamp slot keeps its place for the clock lamp");
        beginTest("hidden then shown again restores the original layout");
        hidden.setShowTapAndNudge(true);
        expect(findButton(hidden, "+")->getBounds().getRight() == shownRight, "nudge back where it was");
        expect(findButton(hidden, "Tap")->isVisible(), "Tap visible again");
    }
};

static TransportBarTests transportBarTests;

// The bar over a ShellBackend that is not a processor: the seam an app without an AudioProcessor uses.
struct TransportBarBackendTests : juce::UnitTest
{
    TransportBarBackendTests() : juce::UnitTest("TransportBar over a ShellBackend", "mu-core") {}

    void runTest() override
    {
        ScaleGuard guard;
        mu_ui::scale = 1.0f;
        const juce::String playText = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
        const juce::String stopText = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));

        beginTest("standalone backend: Play toggles the internal transport and the BPM field writes the tempo");
        {
            TestShellBackend fake(true);
            fake.bpm = 97.0;
            TransportBar bar(fake);
            bar.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            auto* play = findButton(bar, playText);
            expect(play != nullptr, "play button exists");
            if (play == nullptr) return;
            play->onClick();
            expect(fake.playing, "click started the internal transport");
            play->onClick();
            expect(! fake.playing, "second click stopped it");
            expect(findButton(bar, "Tap") != nullptr && findButton(bar, "+") != nullptr, "Tap and Nudge are built");
        }

        beginTest("plugin backend: Play is display-only and mirrors the host");
        {
            TestShellBackend fake(false);
            fake.hostPlay = true;
            TransportBar bar(fake);
            bar.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            expect(findButton(bar, "Tap") == nullptr && findButton(bar, "+") == nullptr, "no Tap or Nudge in a plugin");
            auto* stop = findButton(bar, stopText);
            expect(stop != nullptr, "shows Stop while the host plays");
            if (stop != nullptr) stop->onClick();
            expect(! fake.playing, "the click does not touch the internal transport");
        }

        beginTest("a bar clears a held nudge when destroyed");
        {
            TestShellBackend fake(true);
            fake.nudge = 1;
            { TransportBar bar(fake); }
            expect(fake.nudge == 0, "nudge released");
        }

        beginTest("host reads default to no host");
        {
            MinimalShellBackend none(false);
            double ppq = 5.0;
            int n = 0, d = 0;
            expect(! none.isHostPlaying(), "not playing");
            expect(! none.getHostPpqPosition(ppq), "no position");
            expect(! none.getHostBarStartPpq(ppq), "no bar start");
            none.getHostTimeSignature(n, d);
            expect(n == 4 && d == 4, "4/4");
            expect(! none.hasPendingFullPreset(), "no pending preset");
        }
    }
};

static TransportBarBackendTests transportBarBackendTests;
