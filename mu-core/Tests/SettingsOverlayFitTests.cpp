#include "TestProcessorBase.h"
#include "UI/StandardSettingsOverlay.h"

#include <memory>
#include <vector>

// Fit test for the family settings page (design-ui-family section 5: nothing scrolls, so every
// control must sit inside the overlay, clear of its neighbours and above the footer link).
namespace
{
using Overlay = mu_ui::StandardSettingsOverlay;

// Exposes the protected layout constants so the test uses the page's own numbers.
struct Probe : Overlay
{
    using Overlay::Overlay;
    static constexpr int pad = kPad, rowH = kRowH, rowGap = kRowGap, headerH = kHeaderH;
};

// Adds a product-style extra group ("Locations": two sections of two rows each) like mu-Clid.
struct Locations
{
    std::vector<std::unique_ptr<juce::Label>> labels;

    void addTo(Probe& o)
    {
        for (int sec = 0; sec < 2; ++sec)
        {
            auto a = std::make_unique<juce::Label>(juce::String(), "Path A");
            auto b = std::make_unique<juce::Label>(juce::String(), "Path B");
            o.addAndMakeVisible(*a);
            o.addAndMakeVisible(*b);
            juce::Label* pa = a.get();
            juce::Label* pb = b.get();
            labels.push_back(std::move(a));
            labels.push_back(std::move(b));
            o.addSection(Overlay::Where::Group,
                         { sec == 0 ? "Samples" : "Presets", 2 * Probe::rowH + Probe::rowGap,
                           [pa, pb](const Overlay::Rows& r)
                           {
                               pa->setBounds(r.labelX, r.area.getY(), r.ctrlW, r.rowH);
                               pb->setBounds(r.labelX, r.area.getY() + r.rowH + mu_ui::s(Probe::rowGap),
                                             r.ctrlW, r.rowH);
                           } },
                         "Locations");
        }
    }
};

// Restores the global UI scale on scope exit.
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};
} // namespace

struct SettingsOverlayFitTests : juce::UnitTest
{
    SettingsOverlayFitTests() : juce::UnitTest("StandardSettingsOverlay fit", "mu-core") {}

    // Builds one configuration, lays it out and checks the three fit rules.
    void checkFit(const juce::String& label, juce::AudioProcessor::WrapperType wrapper,
                  bool withLocations, bool midiSyncOn, float scale, int w, int h)
    {
        ScaleGuard guard;
        mu_ui::scale = scale;

        TestProcessorBase proc(wrapper);
        proc.setMidiSyncEnabled(midiSyncOn);   // shows the Messages + Sync offset rows (standalone)
        expect(proc.getMidiSyncEnabled() == midiSyncOn, label + ": MIDI sync state took effect");

        Probe overlay(proc);
        Locations locations;
        if (withLocations) locations.addTo(overlay);

        const int W = mu_ui::s(w), H = mu_ui::s(h);
        overlay.setBounds(0, 0, W, H);

        const auto bounds = overlay.getLocalBounds();
        const int footerTop = H - mu_ui::s(Probe::pad) - mu_ui::s(20);
        const int headerBottom = mu_ui::s(Probe::headerH);

        std::vector<juce::Component*> kids;
        for (auto* c : overlay.getChildren())
            if (c->isVisible() && ! c->getBounds().isEmpty())
                kids.push_back(c);

        expect(kids.size() > 3, label + ": has visible children");

        int deepest = 0;
        for (auto* c : kids)
        {
            const auto b = c->getBounds();
            expect(bounds.contains(b), label + ": inside overlay: " + b.toString());

            const bool chrome = dynamic_cast<juce::TextButton*>(c) != nullptr && b.getBottom() <= headerBottom;
            const bool footer = dynamic_cast<juce::HyperlinkButton*>(c) != nullptr;
            if (! chrome && ! footer)
            {
                deepest = juce::jmax(deepest, b.getBottom());
                expect(b.getBottom() <= footerTop,
                       label + ": above footer (bottom " + juce::String(b.getBottom())
                           + " > " + juce::String(footerTop) + "): " + b.toString());
            }
        }

        for (size_t i = 0; i < kids.size(); ++i)
            for (size_t j = i + 1; j < kids.size(); ++j)
                expect(! kids[i]->getBounds().intersects(kids[j]->getBounds()),
                       label + ": overlap " + kids[i]->getBounds().toString() + " vs "
                           + kids[j]->getBounds().toString());

        logMessage(label + ": deepest control bottom " + juce::String(deepest) + " / footer " + juce::String(footerTop));
    }

    void runTest() override
    {
        // Needs a message manager because the page constructs real components.
        juce::ScopedJuceInitialiser_GUI gui;

        beginTest("settings page fits without scrolling");

        struct Wrapper { const char* name; juce::AudioProcessor::WrapperType type; };
        const Wrapper wrappers[] = { { "standalone", juce::AudioProcessor::wrapperType_Standalone },
                                     { "plugin",     juce::AudioProcessor::wrapperType_VST3 } };
        const float scales[] = { 1.0f, 1.25f };
        const int heights[] = { 814, 774 };   // 774 = 814 less the 40 px demo-banner allowance

        for (const auto& wr : wrappers)
            for (float sc : scales)
                for (int h : heights)
                    for (bool loc : { false, true })
                        for (bool sync : { false, true })
                        {
                            if (sync && wr.type != juce::AudioProcessor::wrapperType_Standalone)
                                continue;   // MIDI Clock exists only in the standalone
                            checkFit(juce::String(wr.name) + (loc ? " +Locations" : "")
                                         + (sync ? " +midiSync" : "") + " scale " + juce::String(sc)
                                         + " h" + juce::String(h),
                                     wr.type, loc, sync, sc, 1088, h);
                        }
    }
};

static SettingsOverlayFitTests settingsOverlayFitTests;
