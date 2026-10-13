// Tests for the mu-link window (MuLinkComponent): the layout on the shared chrome at every display
// size, and the behaviour that the design asks for - scene buttons that recall again when pressed
// twice, the Clock lamp that follows the clock source, and the preset hooks that capture and apply
// the mixer and scenes. It runs over a headless AudioServer (no audio device), like the backend tests.

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <vector>
#include "../UI/MuLinkComponent.h"

#ifdef _WIN32

using namespace mu_link;

namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

juce::File freshRoot()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("mu-link-window-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
}

// Lets the message loop run, so the bar's refresh timer ticks.
void pumpMessages(int ms = 150)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

StatusLamp* findLamp(juce::Component& bar)
{
    for (auto* c : bar.getChildren())
        if (auto* lamp = dynamic_cast<StatusLamp*>(c)) return lamp;
    return nullptr;
}

NudgeInput* findNudge(juce::Component& bar)
{
    for (auto* c : bar.getChildren())
        if (auto* n = dynamic_cast<NudgeInput*>(c)) return n;
    return nullptr;
}

void clickSegment(SegmentControl& sc, int index, int count)
{
    const float x = ((float) index + 0.5f) * (float) sc.getWidth() / (float) count;
    const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                             juce::Point<float>(x, 5.0f), juce::ModifierKeys(), 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, &sc, &sc, juce::Time::getCurrentTime(),
                             juce::Point<float>(x, 5.0f), juce::Time::getCurrentTime(), 1, false);
    sc.mouseDown(e);
}
} // namespace

class MuLinkWindowTest : public juce::UnitTest
{
public:
    MuLinkWindowTest() : juce::UnitTest("mu-link UI / window") {}

    // The window over a headless server with its own data folder.
    struct Rig
    {
        Rig()
        {
            root = freshRoot();
            ok = server.startHeadless(48000.0, 256, 120.0);
            window = std::make_unique<MuLinkComponent>(server, root);
        }
        ~Rig()
        {
            window.reset();
            root.deleteRecursively();
        }
        juce::File root;
        bool ok = false;
        AudioServer server;
        std::unique_ptr<MuLinkComponent> window;
    };

    // Every visible child sits inside its parent; unless `labelsMayTouch`, the non-label ones don't overlap.
    void checkChildren(juce::Component& parent, const juce::String& tag)
    {
        std::vector<juce::Component*> items;
        for (auto* c : parent.getChildren())
            if (c->isVisible())
            {
                expect(parent.getLocalBounds().contains(c->getBounds()), tag + ": a child leaves its parent");
                if (dynamic_cast<juce::Label*>(c) == nullptr) items.push_back(c);
            }
        for (size_t i = 0; i < items.size(); ++i)
            for (size_t j = i + 1; j < items.size(); ++j)
                expect(! items[i]->getBounds().intersects(items[j]->getBounds()),
                       tag + ": two children overlap (" + juce::String((int) i) + ", " + juce::String((int) j) + ")");
    }

    void checkLayout(float scale)
    {
        using LF = MuLookAndFeel;
        using mu_ui::s;
        ScaleGuard guard;
        Rig rig;
        expect(rig.ok, "bus failed");
        auto& w = *rig.window;
        mu_ui::scale = scale;   // the chrome applies the stored scale at construction; test the others by hand
        w.setSize(s(LF::kWindowWidth), s(LF::kWindowHeight));
        const juce::String tag = "scale " + juce::String(scale, 2);

        auto& mixer = w.getMixer();
        auto& scenes = w.getScenes();
        auto* page = mixer.getParentComponent();
        expect(page != nullptr && scenes.getParentComponent() == page, tag + ": both panels in the main page");
        if (page == nullptr) return;

        // The main area is the window less the bar and the status bar; the two panels fill it.
        const int mainH = s(LF::kWindowHeight) - s(LF::kTransportBarH) - s(LF::kStatusBarH);
        expectEquals(page->getHeight(), mainH);
        expectEquals(page->getWidth(), s(LF::kWindowWidth));
        expectEquals(mixer.getHeight() + scenes.getHeight(), mainH);
        expectEquals(scenes.getHeight(), s(ScenesPanel::kScenesPanelH));
        expectEquals(mixer.getBottom(), scenes.getY());

        // The master column is the 80 px strip plus the 136 px insert panel, side by side.
        auto& master = mixer.getMaster();
        auto& inserts = mixer.getInserts();
        expectEquals(master.getWidth() + inserts.getWidth(), s(LF::kMixerMasterTotalW));
        expectEquals(master.getRight(), inserts.getX());
        expectEquals(master.getY(), inserts.getY());

        // Each scene cell lines up under its strip.
        for (int i = 0; i < MixerPanel::kClients; ++i)
        {
            auto& strip = mixer.getStrip(i);
            expectEquals(scenes.getProgramField(i).getRight(), strip.getRight());
            expectEquals(scenes.getChannelField(i).getRight(), strip.getRight());
            expect(scenes.getProgramField(i).getX() >= strip.getX(), tag + ": the field stays in its column");
            expect(scenes.getOnSwitch(i).getX() == strip.getX(), tag + ": the On switch starts at the column");
            expect(strip.getRight() <= master.getX(), tag + ": strips end before the master column");
        }

        checkChildren(w, tag + " window");
        checkChildren(*page, tag + " page");
        checkChildren(mixer, tag + " mixer");
        checkChildren(scenes, tag + " scenes");
        checkChildren(master, tag + " master");
        checkChildren(inserts, tag + " inserts");
        for (int i = 0; i < MixerPanel::kClients; ++i)
            checkChildren(mixer.getStrip(i), tag + " strip " + juce::String(i + 1));
        checkChildren(w.getChrome().getTransportBar(), tag + " bar");

        if (scale == 1.0f)
        {
            expectEquals(mixer.getHeight(), 662);
            expectEquals(scenes.getHeight(), 152);
            expectEquals(mixer.getStrip(0).getWidth(), 112);
            expectEquals(mixer.getStrip(0).getHeight(), 622);
        }
    }

    void runTest() override
    {
        beginTest("the window follows the family size and has no sidebar");
        {
            ScaleGuard guard;
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            expectEquals(w.getWidth(), mu_ui::s(MuLookAndFeel::kWindowWidth));
            expectEquals(w.getHeight(), mu_ui::s(MuLookAndFeel::kWindowHeight));
            expect(w.getChrome().getSidebar() == nullptr, "no sidebar");
            expect(MuLookAndFeel::isMetal(w), "metal style");
            expect(MuLookAndFeel::hasScrews(w), "screws");
        }

        beginTest("layout fits at Small, Medium and Large");
        checkLayout(0.85f);
        checkLayout(1.0f);
        checkLayout(1.15f);

        beginTest("scene buttons recall on every press, including the selected one");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& reg = rig.server.registry();
            expect(w.getScenes().getSceneButtons().getFiresOnReselect(), "pills fire on re-select");

            // Scene 3: client 1 gets program 12 on channel 3; client 2 is ticked but not connected.
            auto table = w.getScenes().getScenes();
            table[2][0] = { true, 12, 3 };
            table[2][1] = { true, 5, 9 };
            w.getScenes().setScenes(table);
            reg.slots[0].active.store(1);

            const auto epoch = reg.slots[0].pcEpoch.load();
            clickSegment(w.getScenes().getSceneButtons(), 2, 8);
            expectEquals((int) reg.slots[0].pcEpoch.load(), (int) epoch + 1);
            expectEquals((int) reg.slots[0].pcProgram.load(), 12);
            expectEquals((int) reg.slots[0].pcChannel.load(), 3);
            expectEquals(w.getScenes().getSelectedScene(), 2);

            clickSegment(w.getScenes().getSceneButtons(), 2, 8);   // the same scene again
            expectEquals((int) reg.slots[0].pcEpoch.load(), (int) epoch + 2);
            expectEquals((int) reg.slots[1].pcEpoch.load(), 0);    // not connected: nothing sent

            // An unticked client is left alone.
            table[2][0].enabled = false;
            w.getScenes().setScenes(table);
            clickSegment(w.getScenes().getSceneButtons(), 2, 8);
            expectEquals((int) reg.slots[0].pcEpoch.load(), (int) epoch + 2);
            reg.slots[0].active.store(0);
        }

        beginTest("editing a scene cell updates the table, marks the preset edited and is saved to disk");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            w.getChrome().clearDirty();
            w.getScenes().selectScene(4);
            w.getScenes().getProgramField(3).setValue(33, true);
            w.getScenes().getChannelField(3).setValue(2, true);
            w.getScenes().getOnSwitch(3).setSelectedIndex(0, true);
            const auto& cell = w.getScenes().getScenes()[4][3];
            expectEquals(cell.program, 33);
            expectEquals(cell.channel, 2);
            expect(cell.enabled, "ticked on");
            expect(w.getChrome().isDirty(), "edit marks the preset");
            expect(w.getBackend().getContentDir().getChildFile("scenes.json").existsAsFile(), "scenes.json written");

            // A second window over the same server and folder reads the table back.
            MuLinkComponent again(rig.server, rig.root);
            const auto& back = again.getScenes().getScenes()[4][3];
            expect(back.enabled && back.program == 33 && back.channel == 2, "table restored from disk");
        }

        beginTest("the clock source switch drives the backend and the Clock lamp shows only under MIDI In");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& bar = w.getChrome().getTransportBar();
            auto* lamp = findLamp(bar);
            auto* bpm = findNudge(bar);
            expect(lamp != nullptr && bpm != nullptr, "bar has the lamp and the BPM field");
            if (lamp == nullptr || bpm == nullptr) return;

            auto& seg = w.getClockSourceControl();
            expect(seg.getFiresOnReselect(), "re-selecting fires");
            pumpMessages();
            expect(! lamp->isVisible(), "no lamp under Internal");
            expect(bpm->isEnabled(), "BPM field live under Internal");

            clickSegment(seg, 1, 2);
            expect(w.getBackend().getMidiSyncEnabled(), "MIDI In chosen");
            expect(rig.server.clockSourceMode() == ClockSource::ExternalMidi, "server follows the clock");
            pumpMessages();
            expect(lamp->isVisible(), "lamp shown under MIDI In");
            expect(! bpm->isEnabled(), "BPM field shows the incoming clock, not editable");

            clickSegment(seg, 0, 2);
            expect(! w.getBackend().getMidiSyncEnabled(), "Internal chosen");
            pumpMessages();
            expect(! lamp->isVisible(), "lamp hidden again");
            expect(bpm->isEnabled(), "BPM field live again");
        }

        beginTest("the gear opens the shared settings page with only mu-link's rows");
        {
            ScaleGuard guard;
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& chrome = w.getChrome();
            auto& settings = w.getSettings();
            chrome.getTransportBar().onSettingsToggle();
            expect(chrome.isSettingsVisible() && settings.isVisible(), "gear opens the page");
            expect(! w.getMixer().getParentComponent()->isVisible(), "the page replaces the main area");

            // Visible controls: Display size, Latency, the sync offset, the devices button; no tempo, meter or source rows.
            int nudges = 0, dropdowns = 0, segments = 0;
            NudgeInput* offset = nullptr;
            SegmentControl* size = nullptr;
            for (auto* c : settings.getChildren())
            {
                if (! c->isVisible()) continue;
                if (auto* n = dynamic_cast<NudgeInput*>(c)) { ++nudges; offset = n; }
                if (dynamic_cast<DropdownSelect*>(c) != nullptr) ++dropdowns;
                if (auto* sc = dynamic_cast<SegmentControl*>(c)) { ++segments; if (sc != &w.getLatencyControl()) size = sc; }
            }
            expectEquals(nudges, 1);
            expectEquals(dropdowns, 0);
            expectEquals(segments, 2);   // Display size and Latency
            expect(w.getChooseDevicesButton().isVisible(), "Choose devices button shown");
            expect(w.getChooseDevicesButton().getButtonText() == "Choose devices...", "its wording");

            int opened = 0;
            w.onChooseDevices = [&opened] { ++opened; };
            w.getChooseDevicesButton().onClick();
            expectEquals(opened, 1);

            // Sync offset: -50 to +250 ms, reaches the server.
            if (offset != nullptr)
            {
                offset->setValue(25, true);
                expectEquals(rig.server.syncOffsetMs(), 25);
                offset->setValue(1000, true);
                expectEquals(rig.server.syncOffsetMs(), 250);
                offset->setValue(-1000, true);
                expectEquals(rig.server.syncOffsetMs(), -50);
            }

            // Every child of the page sits inside it, at Medium and at Large (Display size).
            auto checkInside = [&](const juce::String& tag)
            {
                for (auto* c : settings.getChildren())
                    if (c->isVisible())
                        expect(settings.getLocalBounds().contains(c->getBounds()), tag + ": a settings child leaves the page");
            };
            checkInside("medium");
            if (size != nullptr)
            {
                size->setSelectedIndex(1, true);   // Large
                expectEquals(w.getBackend().getUiScale(), mu_core::ShellBackend::kUiScaleLarge);
                expectEquals(w.getWidth(), mu_ui::s(MuLookAndFeel::kWindowWidth));
                expect(w.getWidth() > 1170, "the window grew with the display size");
                checkInside("large");
            }

            chrome.getTransportBar().onSettingsToggle();
            expect(! chrome.isSettingsVisible(), "gear closes it");
            mu_ui::scale = 1.0f;
        }

        beginTest("Latency asks every connected app for its render-ahead depth and is kept with the settings");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            MuLinkClientMemory app;
            expect(app.open(), "client failed to attach");
            expectEquals(app.claimSlot("app", 2), 0);

            expectEquals(w.getBackend().getLatencyIndex(), 1);                 // Normal by default
            expectEquals(w.getMixer().getLatencyFrames(), 0);                   // 0 = the app's own default
            w.getLatencyControl().setSelectedIndex(0, true);                    // Low
            expectEquals(w.getMixer().getLatencyFrames(), 2048);
            expectEquals((int) app.registry().slots[0].requestedDepthFrames.load(), 2048);
            w.getLatencyControl().setSelectedIndex(2, true);                    // High
            expectEquals((int) app.registry().slots[0].requestedDepthFrames.load(), 8192);
            expectEquals(w.getBackend().getLatencyIndex(), 2);                  // remembered
            w.getLatencyControl().setSelectedIndex(1, true);                    // Normal withdraws the request
            expectEquals((int) app.registry().slots[0].requestedDepthFrames.load(), 0);
        }

        beginTest("a dropout lights the strip's lamp red, latches it amber, and a click or a Latency change clears it");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& mixer = w.getMixer();
            auto& lamp = mixer.getStrip(0).getDropoutLamp();
            MuLinkClientMemory app;
            expect(app.open(), "client failed to attach");
            expectEquals(app.claimSlot("app", 2), 0);

            // Past the warm-up, a rise in the slot's underrun count turns the lamp active, then latched.
            mixer.setLatencyFrames(0);
            rig.server.requestClientDepth(0, 0);
            juce::Thread::sleep(MixerPanel::kDropoutWarmupMs + 200);
            mixer.updateLatencyAndDropouts();
            app.registry().slots[0].underrunFrames.fetch_add(64, std::memory_order_relaxed);
            mixer.updateLatencyAndDropouts();
            expectEquals((int) mixer.getDropoutState(0), 1);                    // active
            juce::Thread::sleep(MixerPanel::kDropoutHoldMs + 200);
            mixer.updateLatencyAndDropouts();
            expectEquals((int) mixer.getDropoutState(0), 2);                    // latched
            lamp.onClick();
            expectEquals((int) mixer.getDropoutState(0), 0);                    // a click clears it

            app.registry().slots[0].underrunFrames.fetch_add(64, std::memory_order_relaxed);
            mixer.updateLatencyAndDropouts();
            expectEquals((int) mixer.getDropoutState(0), 1);
            mixer.setLatencyFrames(2048);                                       // a new depth: the old count no longer applies
            expectEquals((int) mixer.getDropoutState(0), 0);
        }

        beginTest("the bar keeps Tap, Nudge and the Mixer button out");
        {
            Rig rig;
            auto& bar = rig.window->getChrome().getTransportBar();
            for (auto* c : bar.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*>(c))
                {
                    const auto t = b->getButtonText();
                    if (t == "Tap" || t == "-" || t == "+" || t == "Mixer")
                        expect(! b->isVisible(), "button '" + t + "' is hidden");
                }
        }

        beginTest("Names | Presets switches what the strips show");
        {
            Rig rig;
            auto& mixer = rig.window->getMixer();
            expect(! mixer.showsPresetNames(), "names by default");
            clickSegment(mixer.getLabelModeControl(), 1, 2);
            expect(mixer.showsPresetNames(), "presets after the click");
            clickSegment(mixer.getLabelModeControl(), 0, 2);
            expect(! mixer.showsPresetNames(), "names again");
        }

        beginTest("the strips and the master write to the server, and a user edit marks the preset");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& s = rig.server;
            w.getChrome().clearDirty();

            w.getMixer().getStrip(2).onClientLevel(0.5f);
            expectEquals(s.clientGainValue(2), 0.5f);
            expect(w.getChrome().isDirty(), "level marks the preset");

            w.getMixer().getStrip(3).onClientMute(true);
            w.getMixer().getStrip(3).onClientSolo(true);
            expect(s.clientMuted(3) && s.clientSoloed(3), "mute and solo");

            // EQ: the High band at +6 dB is stored normalised in the High slot; flat is 0.5.
            using Eq = MixerChannel::ClientEq;
            w.getMixer().getStrip(1).onClientEq(Eq::High, 6.0f);
            expect(s.clientEqValue(1, MixerPanel::eqSlotOf(Eq::High)) > 0.5f, "High boosted");
            w.getMixer().getStrip(1).onClientEq(Eq::High, 0.0f);
            expectWithinAbsoluteError(s.clientEqValue(1, MixerPanel::eqSlotOf(Eq::High)), 0.5f, 0.0001f);

            w.getMixer().getMaster().onMasterLevel(0.7f);
            expectEquals(s.masterGainValue(), 0.7f);
            w.getMixer().getInserts().onAlgorithm(0, 6);
            expectEquals(s.masterInsertAlgo(0), 6);
        }

        beginTest("Save writes the whole state to a .muLink file and loading it puts everything back");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& s = rig.server;

            // Set up a state.
            s.setClientGain(2, 0.5f);
            s.setClientMute(3, true);
            s.setClientEqParam(1, 3, 0.8f);
            s.setMasterGain(0.7f);
            s.setMasterInsertAlgo(1, 6);
            s.setMasterInsertParam(1, 0, 0.25f);
            w.getBackend().setInternalBpm(98.5);
            auto table = w.getScenes().getScenes();
            table[1][5] = { true, 40, 4 };
            w.getScenes().setScenes(table);

            w.getChrome().getSaveDialog().onSave("Round Trip", "a description", "Live", false);
            const auto file = w.getBackend().getPresetsDir().getChildFile("Round Trip.muLink");
            expect(file.existsAsFile(), "preset file written");
            expect(! w.getChrome().isDirty(), "saving clears the flag");
            if (auto xml = juce::parseXML(file))
            {
                expectEquals(xml->getTagName(), juce::String("MuLinkPreset"));
                expectEquals(xml->getStringAttribute("presetCategory"), juce::String("Live"));
                expectEquals(xml->getStringAttribute("presetDescription"), juce::String("a description"));
            }
            else
                expect(false, "preset is not XML");
            expect(w.getBackend().loadCategoryList().contains("Live"), "the category is listed");
            expect(w.getChrome().getTransportBar().getLoadedPresetFile() == file, "the bar shows the saved preset");

            // Change everything, then load it back through the browser path.
            s.setClientGain(2, 1.0f);
            s.setClientMute(3, false);
            s.setClientEqParam(1, 3, 0.5f);
            s.setMasterGain(1.0f);
            s.setMasterInsertAlgo(1, 0);
            s.setMasterInsertParam(1, 0, 0.0f);
            w.getBackend().setInternalBpm(140.0);
            w.getScenes().setScenes({});

            w.getChrome().markDirty();
            w.getChrome().getPresetBrowser().onLoadPreset(file);
            expectEquals(s.clientGainValue(2), 0.5f);
            expect(s.clientMuted(3), "mute back");
            expectWithinAbsoluteError(s.clientEqValue(1, 3), 0.8f, 0.0001f);
            expectEquals(s.masterGainValue(), 0.7f);
            expectEquals(s.masterInsertAlgo(1), 6);
            expectWithinAbsoluteError(s.masterInsertParam(1, 0), 0.25f, 0.0001f);
            expectEquals(w.getBackend().getInternalBpm(), 98.5);
            const auto& cell = w.getScenes().getScenes()[1][5];
            expect(cell.enabled && cell.program == 40 && cell.channel == 4, "scene cell back");
            expect(! w.getChrome().isDirty(), "loading clears the flag");

            // The controls follow the server.
            expectEquals(w.getMixer().getInserts().getAlgorithm(1), 6);
            expectWithinAbsoluteError(w.getMixer().getInserts().getParam(1, 0), 0.25f, 0.0001f);
        }

        beginTest("loading something that is not a mu-link preset reports it and changes nothing");
        {
            Rig rig;
            auto& w = *rig.window;
            juce::String message;
            w.getBackend().onLoadError = [&message](const juce::String& m) { message = m; };
            const auto bad = rig.root.getChildFile("Bad.muLink");
            bad.getParentDirectory().createDirectory();
            bad.replaceWithText("<Something/>");
            rig.server.setMasterGain(0.3f);
            w.getBackend().loadPreset(bad);
            expect(message.isNotEmpty(), "the error is reported");
            expectEquals(rig.server.masterGainValue(), 0.3f);
        }

        beginTest("New restores the factory state when there is no saved default");
        {
            Rig rig;
            expect(rig.ok, "bus failed");
            auto& w = *rig.window;
            auto& s = rig.server;
            s.setClientGain(0, 0.2f);
            s.setClientMute(1, true);
            s.setClientSolo(2, true);
            s.setClientEqParam(4, 0, 0.9f);
            s.setMasterGain(0.4f);
            s.setMasterInsertAlgo(0, 6);
            w.getBackend().setInternalBpm(180.0);
            auto table = w.getScenes().getScenes();
            table[0][0] = { true, 9, 1 };
            w.getScenes().setScenes(table);

            w.getChrome().clearDirty();
            w.getChrome().getTransportBar().onNewPreset();
            for (int i = 0; i < MixerPanel::kClients; ++i)
            {
                expectEquals(s.clientGainValue(i), 1.0f);
                expect(! s.clientMuted(i) && ! s.clientSoloed(i), "mute and solo off");
                for (int b = 0; b < 4; ++b)
                    expectEquals(s.clientEqValue(i, b), 0.5f);
            }
            expectEquals(s.masterGainValue(), 1.0f);
            expectEquals(s.masterInsertAlgo(0), 0);
            expectEquals(s.masterInsertAlgo(1), 0);
            expectEquals(w.getBackend().getInternalBpm(), 120.0);
            expect(w.getScenes().getScenes()[0][0] == ScenesPanel::Cell{}, "scenes cleared");
            expectEquals(w.getMixer().getInserts().getAlgorithm(0), 0);
        }
    }
};

static MuLinkWindowTest muLinkWindowTest;

#endif // _WIN32
