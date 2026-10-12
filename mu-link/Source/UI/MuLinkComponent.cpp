#include "MuLinkComponent.h"
#include "Persistence/PresetFiles.h"   // safePresetFileName, writeXmlAtomically

namespace
{
const juce::String kProductName = juce::String(juce::CharPointer_UTF8("\xce\xbc-link"));
// Rows mu-link leaves out: its tempo and meter are on the bar and fixed, its clock source is on the bar,
// and its sync offset is always shown.
mu_ui::StandardSettingsOverlay::Options settingsOptions()
{
    mu_ui::StandardSettingsOverlay::Options o;
    o.showTransport = false;
    o.showMeter = false;
    o.showClockSource = false;
    o.syncOffsetAlways = true;
    return o;
}
constexpr int kClockSectionW = 150;   // the clock source switch's slot in the transport bar (unscaled)
}

//==============================================================================
MuLinkComponent::ClockSourceSection::ClockSourceSection()
{
    addAndMakeVisible(segment);
}

// The switch is as tall as the BPM field: the slot less the bar's own inset.
void MuLinkComponent::ClockSourceSection::resized()
{
    segment.setBounds(getLocalBounds().reduced(mu_ui::s(6), mu_ui::s(3)));
}

// Mixer panel on top, Scenes panel under it; together they fill the main area.
void MuLinkComponent::MainPage::resized()
{
    const int scenesH = mu_ui::s(mu_link::ScenesPanel::kScenesPanelH);
    mixer .setBounds(0, 0, getWidth(), getHeight() - scenesH);
    scenes.setBounds(0, getHeight() - scenesH, getWidth(), scenesH);
}

//==============================================================================
MuLinkComponent::MuLinkComponent(mu_link::AudioServer& serverToShow, juce::File dataRoot)
    : server(serverToShow),
      backend(serverToShow, std::move(dataRoot)),
      chrome(*this, backend),
      settings(backend, settingsOptions()),
      page(serverToShow)
{
    chrome.getTransportBar().setLogoText(kProductName);
    chrome.getAboutOverlay().setProductInfo(kProductName,
        { juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 AGPLv3")) });

    chrome.setMainArea(nullptr, &page);
    wireTransportBar();
    wirePresets();
    wireSettings();

    // Unpainted brushed steel, LCD lettering in the global accent, until an app colour is chosen.
    chrome.setMetalStyle(true);
    chrome.setScrews(true);

    // Status bar: hover text from the panels and the clock switch, and the strips' value changes.
    page.mixer.onStatus = [this](const juce::String& n, const juce::String& v, juce::Colour tag)
    { chrome.getStatusBar().showParam(n, v, tag); };
    page.scenes.onStatus = [this](const juce::String& n, const juce::String& v)
    { chrome.getStatusBar().showParam(n, v); };
    chrome.forwardKnobStatus(page.mixer);

    // The scenes table lives on disk beside the presets.
    page.scenes.onRecall = [this](int scene) { triggerScene(scene); };
    page.scenes.onEdited = [this] { saveScenes(); chrome.markDirty(); };
    page.mixer.onEdited  = [this] { chrome.markDirty(); };
    loadScenes();
    page.scenes.selectScene(0);

    // Start stopped at 120 BPM; the user presses Play to run the master transport.
    backend.setInternalBpm(120.0);
    server.setPlaying(false);

    chrome.applyWindowSize();
}

MuLinkComponent::~MuLinkComponent()
{
    // The backend outlives the window's children; no hook may call back into a destroyed window.
    backend.onLoadPresetFile = nullptr;
    backend.onSavePreset     = nullptr;
}

// The bar: no tap or nudge, no mixer button (the mixer is the main area), the clock source switch in
// the loop-section slot, and the Clock lamp that the bar shows by itself while MIDI In drives the clock.
void MuLinkComponent::wireTransportBar()
{
    auto& bar = chrome.getTransportBar();
    bar.setShowTapAndNudge(false);
    bar.setShowMixerToggle(false);
    bar.setLoopSection(&clockSource, kClockSectionW);

    auto& seg = clockSource.segment;
    seg.setFiresOnReselect(true);
    seg.setSelectedIndex(backend.getMidiSyncEnabled() ? 1 : 0, false);
    seg.onChange = [this](int index)
    {
        backend.setMidiSyncEnabled(index == 1);
        showClockSourceStatus();
    };
    hover.watch(seg, "Clock source", {});
    hover.onStatus = [this](const juce::String& n, const juce::String&)
    {
        chrome.getStatusBar().showParam(n, clockSourceText());
    };
}

juce::String MuLinkComponent::clockSourceText() const
{
    return clockSource.segment.getSelectedIndex() == 1 ? "MIDI In (slaved to external clock)"
                                                       : "Internal (mu-link is the master)";
}

void MuLinkComponent::showClockSourceStatus()
{
    chrome.getStatusBar().showParam("Clock source", clockSourceText());
}

// The gear opens the shared settings page; mu-link adds one General section: the device dialog.
void MuLinkComponent::wireSettings()
{
    chooseDevicesButton.onClick = [this]
    {
        if (onChooseDevices) onChooseDevices();
        else showDeviceDialog();
    };
    settings.addAndMakeVisible(chooseDevicesButton);

    mu_ui::StandardSettingsOverlay::Section devices;
    devices.title = "Audio / MIDI devices";
    devices.place = [this](const mu_ui::StandardSettingsOverlay::Rows& r)
    {
        chooseDevicesButton.setBounds(r.labelX, r.area.getY(), mu_ui::s(180), r.rowH);
    };
    settings.addSection(mu_ui::StandardSettingsOverlay::Where::General, std::move(devices));

    // Clients: one Latency choice for every app (how far ahead each renders), with a hint under it.
    settings.makeFieldLabel(latencyLabel, "Latency");
    settings.addAndMakeVisible(latencyLabel);
    latencyChoice.setSelectedIndex(backend.getLatencyIndex(), false);
    latencyChoice.onChange = [this](int index) { applyLatency(index, true); };
    settings.addAndMakeVisible(latencyChoice);
    hover.watch(latencyChoice, "Latency", {});
    mu_ui::StandardSettingsOverlay::Section clients;
    clients.heightPx = 2 * mu_ui::StandardSettingsOverlay::kRowH + mu_ui::StandardSettingsOverlay::kRowGap;
    clients.place = [this](const mu_ui::StandardSettingsOverlay::Rows& r)
    {
        latencyLabel .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        latencyChoice.setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH);
    };
    clients.paint = [this](juce::Graphics& g, const mu_ui::StandardSettingsOverlay::Rows& r)
    {
        settings.drawHint(g, r.area.getY() + r.rowH + mu_ui::s(mu_ui::StandardSettingsOverlay::kRowGap) / 2 + r.rowH / 2,
                          "Render-ahead per app. Low is fastest; raise it if the Dropouts lamps light.",
                          r.ctrlX, r.area.getRight() - r.ctrlX);
    };
    settings.addSection(mu_ui::StandardSettingsOverlay::Where::Group, std::move(clients), "Clients");
    applyLatency(backend.getLatencyIndex(), false);
    chrome.setSettingsOverlay(&settings);
}

// Low / Normal / High: ask every app to render 2048 / its own default (4096) / 8192 frames ahead. The panel
// sends it to each connected app, and again to any that connects later.
void MuLinkComponent::applyLatency(int index, bool announce)
{
    static constexpr int kDepthFrames[3] = { 2048, 0, 8192 };   // 0 = the app's own default
    static const char* const kNames[3] = { "Low", "Normal", "High" };
    index = juce::jlimit(0, 2, index);
    backend.setLatencyIndex(index);
    page.mixer.setLatencyFrames(kDepthFrames[index]);
    if (announce)
    {
        const int frames = kDepthFrames[index] != 0 ? kDepthFrames[index] : 4096;
        const double sr = server.sampleRate() > 0.0 ? server.sampleRate() : 48000.0;
        chrome.getStatusBar().showParam("Latency", juce::String(kNames[index]) + ", about "
                                        + juce::String(juce::roundToInt(frames * 1000.0 / sr)) + " ms");
    }
}

// The audio / MIDI device picker in a dialog; it binds to the live device manager, so changes apply at once.
void MuLinkComponent::showDeviceDialog()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        server.audioDeviceManager(),
        0, 0,        // no audio inputs
        2, 2,        // stereo output
        true,        // MIDI input (external clock source)
        true,        // MIDI output (MIDI-clock-out port)
        true,        // channels as stereo pairs
        false);      // no advanced options
    selector->setSize(380, 460);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(selector.release());
    opts.dialogTitle                  = kProductName + juce::String(juce::CharPointer_UTF8(" â Audio / MIDI Options"));
    opts.dialogBackgroundColour       = MuLookAndFeel::colour(MuLookAndFeel::windowBackground);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;
    opts.launchAsync();
}

// Presets: the backend asks this window to apply or capture its state.
void MuLinkComponent::wirePresets()
{
    backend.onLoadPresetFile = [this](const juce::File& f) { loadPresetFile(f); };
    backend.onSavePreset = [this](const juce::String& name, const juce::String& desc, const juce::String& category)
    { doSavePreset(name, desc, category); };

    // New without a saved default restores the factory state; with one, the shared flow has loaded it.
    chrome.onPresetNew = [this]
    {
        if (backend.getDefaultPresetFile().existsAsFile())
            return;
        resetToDefaults();
        chrome.getStatusBar().showParam("New", "Defaults restored");
    };
}

//==============================================================================
void MuLinkComponent::paint(juce::Graphics& g)
{
    chrome.paintBackground(g);
}

void MuLinkComponent::resized()
{
    chrome.resized();
}

//==============================================================================
// Scenes: a scene is a set of per-client program changes, each on its own MIDI channel, so one
// scene can mix full-preset recalls (channel 9) with single-layer swaps (channels 1 to 8).

void MuLinkComponent::triggerScene(int scene)
{
    // Send each enabled and connected client its program change; the client's standalone bridge
    // injects it and the product's program-change hot-swap fires.
    if (! server.hasBus())
        return;

    auto& reg = server.registry();
    const auto& table = page.scenes.getScenes();
    for (int i = 0; i < mu_link::ScenesPanel::kNumClients; ++i)
    {
        const auto& cell = table[(size_t) juce::jlimit(0, mu_link::ScenesPanel::kNumScenes - 1, scene)][(size_t) i];
        if (! cell.enabled)
            continue;
        if (reg.slots[i].active.load(std::memory_order_acquire) == 0)
            continue;   // slot not connected
        mu_link::sendProgramChange(reg.slots[i], cell.program, cell.channel);
    }
}

juce::File MuLinkComponent::scenesFile() const
{
    return backend.getContentDir().getChildFile("scenes.json");
}

void MuLinkComponent::saveScenes() const
{
    juce::Array<juce::var> sceneArr;
    const auto& table = page.scenes.getScenes();
    for (int s = 0; s < mu_link::ScenesPanel::kNumScenes; ++s)
    {
        juce::Array<juce::var> cells;
        for (int i = 0; i < mu_link::ScenesPanel::kNumClients; ++i)
        {
            const auto& c = table[(size_t) s][(size_t) i];
            auto* o = new juce::DynamicObject();
            o->setProperty("on", c.enabled);
            o->setProperty("pc", c.program);
            o->setProperty("ch", c.channel);
            cells.add(juce::var(o));
        }
        sceneArr.add(juce::var(cells));
    }
    const auto f = scenesFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(sceneArr)));
}

void MuLinkComponent::loadScenes()
{
    const auto f = scenesFile();
    if (! f.existsAsFile())
        return;
    const auto parsed = juce::JSON::parse(f.loadFileAsString());
    auto* sceneArr = parsed.getArray();
    if (sceneArr == nullptr)
        return;

    auto table = page.scenes.getScenes();
    for (int s = 0; s < juce::jmin(mu_link::ScenesPanel::kNumScenes, sceneArr->size()); ++s)
    {
        auto* cells = (*sceneArr)[s].getArray();
        if (cells == nullptr)
            continue;
        for (int i = 0; i < juce::jmin(mu_link::ScenesPanel::kNumClients, cells->size()); ++i)
        {
            const auto& cv = (*cells)[i];
            auto& c = table[(size_t) s][(size_t) i];
            c.enabled = (bool) cv.getProperty("on", false);
            c.program = juce::jlimit(0, 127, (int) cv.getProperty("pc", 0));
            c.channel = juce::jlimit(1, 16,  (int) cv.getProperty("ch", 9));
        }
    }
    page.scenes.setScenes(table);
}

//==============================================================================
// Presets: the full app state (tempo, mixer, master inserts, scenes) as a .muLink XML file.

juce::ValueTree MuLinkComponent::captureState() const
{
    juce::ValueTree state("MuLinkPreset");
    state.setProperty("tempo", backend.getInternalBpm(), nullptr);

    juce::ValueTree master("Master");
    master.setProperty("gain", (double) server.masterGainValue(), nullptr);
    for (int w = 0; w < mu_ui::MasterInsertPanel::kSlots; ++w)
    {
        juce::ValueTree ins("Insert");
        ins.setProperty("idx",  w, nullptr);
        ins.setProperty("algo", server.masterInsertAlgo(w), nullptr);
        for (int s = 0; s < mu_ui::MasterInsertPanel::kParams; ++s)
            ins.setProperty("p" + juce::String(s), (double) server.masterInsertParam(w, s), nullptr);
        master.appendChild(ins, nullptr);
    }
    state.appendChild(master, nullptr);

    for (int i = 0; i < mu_link::MixerPanel::kClients; ++i)
    {
        juce::ValueTree c("Client");
        c.setProperty("idx",  i, nullptr);
        c.setProperty("gain", (double) server.clientGainValue(i), nullptr);
        c.setProperty("mute", server.clientMuted(i), nullptr);
        c.setProperty("solo", server.clientSoloed(i), nullptr);
        for (int b = 0; b < 4; ++b)
            c.setProperty("eq" + juce::String(b), (double) server.clientEqValue(i, b), nullptr);
        state.appendChild(c, nullptr);
    }

    juce::ValueTree scenesTree("Scenes");
    const auto& table = page.scenes.getScenes();
    for (int s = 0; s < mu_link::ScenesPanel::kNumScenes; ++s)
    {
        juce::ValueTree sc("Scene");
        sc.setProperty("idx", s, nullptr);
        for (int i = 0; i < mu_link::ScenesPanel::kNumClients; ++i)
        {
            const auto& cell = table[(size_t) s][(size_t) i];
            juce::ValueTree ce("Cell");
            ce.setProperty("idx", i, nullptr);
            ce.setProperty("on", cell.enabled, nullptr);
            ce.setProperty("pc", cell.program, nullptr);
            ce.setProperty("ch", cell.channel, nullptr);
            sc.appendChild(ce, nullptr);
        }
        scenesTree.appendChild(sc, nullptr);
    }
    state.appendChild(scenesTree, nullptr);
    return state;
}

void MuLinkComponent::applyState(const juce::ValueTree& state)
{
    // Write each value to the server, then let the mixer read them back into its controls.
    if (state.hasProperty("tempo"))
    {
        backend.setInternalBpm((double) state.getProperty("tempo"));
        chrome.getTransportBar().refreshTempo();
    }

    auto master = state.getChildWithName("Master");
    if (master.isValid())
    {
        server.setMasterGain((float) (double) master.getProperty("gain", 1.0));
        for (int w = 0; w < mu_ui::MasterInsertPanel::kSlots; ++w)
        {
            auto ins = master.getChildWithProperty("idx", w);
            if (! ins.isValid()) continue;
            server.setMasterInsertAlgo(w, (int) ins.getProperty("algo", 0));
            for (int s = 0; s < mu_ui::MasterInsertPanel::kParams; ++s)
                server.setMasterInsertParam(w, s, (float) (double) ins.getProperty("p" + juce::String(s), 0.0));
        }
    }

    for (int i = 0; i < mu_link::MixerPanel::kClients; ++i)
    {
        auto c = state.getChildWithProperty("idx", i);   // direct Client children carry idx
        if (! c.isValid() || ! c.hasType("Client")) continue;
        server.setClientGain(i, (float) (double) c.getProperty("gain", 1.0));
        server.setClientMute(i, (bool) c.getProperty("mute", false));
        server.setClientSolo(i, (bool) c.getProperty("solo", false));
        for (int b = 0; b < 4; ++b)
            server.setClientEqParam(i, b, (float) (double) c.getProperty("eq" + juce::String(b), 0.5));
    }
    page.mixer.refreshFromServer();

    auto scenesTree = state.getChildWithName("Scenes");
    if (scenesTree.isValid())
    {
        auto table = page.scenes.getScenes();
        for (int s = 0; s < mu_link::ScenesPanel::kNumScenes; ++s)
        {
            auto sc = scenesTree.getChildWithProperty("idx", s);
            if (! sc.isValid()) continue;
            for (int i = 0; i < mu_link::ScenesPanel::kNumClients; ++i)
            {
                auto ce = sc.getChildWithProperty("idx", i);
                if (! ce.isValid()) continue;
                auto& cell = table[(size_t) s][(size_t) i];
                cell.enabled = (bool) ce.getProperty("on", false);
                cell.program = juce::jlimit(0, 127, (int) ce.getProperty("pc", 0));
                cell.channel = juce::jlimit(1, 16,  (int) ce.getProperty("ch", 9));
            }
        }
        page.scenes.setScenes(table);
        saveScenes();
    }
}

void MuLinkComponent::resetToDefaults()
{
    juce::ValueTree state("MuLinkPreset");
    state.setProperty("tempo", 120.0, nullptr);

    juce::ValueTree master("Master");
    master.setProperty("gain", 1.0, nullptr);
    for (int w = 0; w < mu_ui::MasterInsertPanel::kSlots; ++w)
    {
        juce::ValueTree ins("Insert");
        ins.setProperty("idx", w, nullptr);
        ins.setProperty("algo", 0, nullptr);   // None
        master.appendChild(ins, nullptr);
    }
    state.appendChild(master, nullptr);

    for (int i = 0; i < mu_link::MixerPanel::kClients; ++i)
    {
        juce::ValueTree c("Client");
        c.setProperty("idx", i, nullptr);   // gain, mute, solo and EQ take their defaults: unity, off, flat
        state.appendChild(c, nullptr);
    }

    applyState(state);

    page.scenes.setScenes({});   // every cell back to off, program 0, channel 9
    saveScenes();
}

void MuLinkComponent::doSavePreset(const juce::String& name, const juce::String& desc, const juce::String& category)
{
    auto state = captureState();
    state.setProperty("presetCategory",    category, nullptr);   // read back by the browser
    state.setProperty("presetDescription", desc,     nullptr);

    const auto file = backend.getPresetsDir().getChildFile(
        mu_pp::safePresetFileName(name, "Preset") + "." + backend.getFullPresetExtension());
    if (auto xml = state.createXml())
        mu_pp::writeXmlAtomically(*xml, file, [this](const juce::String& msg)
        { if (backend.onLoadError) backend.onLoadError(msg); });
}

void MuLinkComponent::loadPresetFile(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        if (backend.onLoadError) backend.onLoadError("Could not read " + file.getFileName());
        return;
    }
    const auto state = juce::ValueTree::fromXml(*xml);
    if (! state.hasType("MuLinkPreset"))
    {
        if (backend.onLoadError) backend.onLoadError(file.getFileName() + " is not a mu-link preset");
        return;
    }
    applyState(state);
}
