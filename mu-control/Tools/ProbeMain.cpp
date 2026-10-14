// mu-control-probe: drives the real Launchpad X so a person can judge it by eye. It lists the MIDI ports,
// opens the pad through LaunchpadXDriver (Programmer mode), runs the numbered steps given on the command line
// and prints every message it sends and receives. Every step ends with the pads dark and Live mode restored,
// including on Ctrl+C and when the console window is closed.
//
//   mu-control-probe                  list the MIDI ports and show the usage; opens nothing
//   mu-control-probe 1                RGB test: gradient, then the layer colours bright and dim
//   mu-control-probe 2                palette fallback: static, flashing and pulsing pads
//   mu-control-probe 3                the layer hue table, bright against dim, RGB against the palette picture
//   mu-control-probe 4                input echo for 30 s (decoded presses and the raw logo CC 99)
//   mu-control-probe 5                the full manager demo: four layers, four items, a queued pulse
//   mu-control-probe --seconds 20 4   override how long a step holds or listens
//   mu-control-probe 1 2 3 4 5        several steps in turn

#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Devices/LaunchpadX/LaunchpadXPalette.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include "Driver/JuceDevicePorts.h"
#include "Manager/ControllerManager.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
using namespace mu_control;
using Bytes = std::vector<uint8_t>;

std::atomic<bool> stopRequested { false };
std::atomic<bool> cleanupDone { false };
std::mutex        printMutex;

template <typename... Args>
void say(Args&&... args)
{
    const std::lock_guard<std::mutex> lock(printMutex);
    (std::cout << ... << args) << std::endl;
}

juce::String hex(const uint8_t* d, size_t n, size_t maxShown = 28)
{
    juce::String s;
    for (size_t i = 0; i < std::min(n, maxShown); ++i) s << juce::String::toHexString((int) d[i]).paddedLeft('0', 2).toUpperCase() << ' ';
    if (n > maxShown) s << "... (" << (int) n << " bytes)";
    return s.trim();
}

// What a message is, in words, so the output can be read without the manual beside you.
juce::String describeSend(const uint8_t* d, size_t n)
{
    if (n >= 9 && d[0] == 0xF0 && d[1] == 0x00 && d[2] == 0x20 && d[3] == 0x29 && d[4] == 0x02 && d[5] == 0x0C)
    {
        if (d[6] == 0x0E) return d[7] == 1 ? "Programmer mode ON" : "Live mode (Programmer OFF)";
        if (d[6] == 0x0B) return "aftertouch setting";
        if (d[6] == 0x03) return "lighting, " + juce::String((int) (n - 8)) + " bytes of specs";
    }
    return {};
}

juce::String describeRecv(const uint8_t* d, size_t n)
{
    if (n == 3 && d[0] == 0xB0 && d[1] == 99) return "LOGO CC 99 raw value " + juce::String((int) d[2]);
    if (n == 3 && d[0] == 0xB0) return "CC " + juce::String((int) d[1]) + " value " + juce::String((int) d[2]);
    if (n == 3 && (d[0] & 0xF0) == 0x90) return "note " + juce::String((int) d[1]) + " velocity " + juce::String((int) d[2]);
    if (n == 3 && (d[0] & 0xF0) == 0xA0) return "polyphonic aftertouch " + juce::String((int) d[1]) + " pressure " + juce::String((int) d[2]);
    return {};
}

// The real ports with a running commentary on every byte in and out. The commentary comes from the MIDI thread,
// which is fine for a probe that only prints.
class TapPorts : public DevicePorts
{
public:
    juce::StringArray outputNames() const override { return real.outputNames(); }
    juce::StringArray inputNames() const override  { return real.inputNames(); }

    bool open(const PortSpec& spec) override
    {
        say("  OPEN  output \"", spec.outputPattern, "\"  input \"", spec.inputPattern, "\"");
        real.onBytes = [this](const uint8_t* d, size_t n)
        {
            say("  RECV  ", hex(d, n), "   ", describeRecv(d, n));
            if (onBytes) onBytes(d, n);
        };
        real.onConnectionChanged = [this](bool c)
        {
            say("  PORT  device ", c ? "reconnected" : "unplugged");
            if (onConnectionChanged) onConnectionChanged(c);
        };
        const bool ok = real.open(spec);
        say(ok ? "  OPEN  ok" : "  OPEN  FAILED (device missing or the ports are in use elsewhere)");
        return ok;
    }

    void close() override
    {
        say("  CLOSE ports");
        real.close();
        real.onBytes = nullptr;
        real.onConnectionChanged = nullptr;
    }

    bool isOpen() const override      { return real.isOpen(); }
    bool isConnected() const override { return real.isConnected(); }

    bool send(const uint8_t* d, size_t n) override
    {
        const bool ok = real.send(d, n);
        say("  SEND  ", hex(d, n), "   ", describeSend(d, n), ok ? "" : "   (FAILED)");
        return ok;
    }

private:
    JuceDevicePorts real;
};

// Colours ------------------------------------------------------------------------------------------------

// The family layer palette (MuLookAndFeel::layerPalette), copied so the probe needs no UI header.
constexpr uint32_t kLayerRgb[8] = { 0xE5484D, 0x2EC4C9, 0xF28C28, 0xD04FC8, 0xA6D63A, 0xF07CA6, 0xC9CCD1, 0x2B3FD6 };
constexpr const char* kLayerName[8] = { "red", "cyan", "orange", "magenta", "lime", "rose", "silver", "navy" };

uint32_t scale(uint32_t rgb, float s)
{
    const auto ch = [&](int shift) { return (uint32_t) std::min(255.0f, (float) ((rgb >> shift) & 0xFFu) * s + 0.5f); };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

juce::String rgbText(uint32_t rgb) { return "#" + juce::String::toHexString((int) rgb).paddedLeft('0', 6).toUpperCase(); }

// A full frame (64 pads, 8 top buttons, 8 side buttons) that starts dark; set what the step wants lit.
struct Frame
{
    std::vector<LedState> leds;

    Frame()
    {
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) leds.push_back({ { ControlKind::Pad, x, y }, 0, LedMode::Static });
        for (int i = 0; i < 8; ++i) leds.push_back({ { ControlKind::Top, i, 0 }, 0, LedMode::Static });
        for (int i = 0; i < 8; ++i) leds.push_back({ { ControlKind::Side, 0, i }, 0, LedMode::Static });
    }

    void set(ControlKind kind, int x, int y, uint32_t rgb, LedMode mode = LedMode::Static)
    {
        for (auto& l : leds)
            if (l.id.kind == kind && l.id.x == x && l.id.y == y) { l.rgb = rgb; l.mode = mode; return; }
    }
    void pad(int x, int y, uint32_t rgb, LedMode mode = LedMode::Static) { set(ControlKind::Pad, x, y, rgb, mode); }
    void top(int i, uint32_t rgb, LedMode mode = LedMode::Static)        { set(ControlKind::Top, i, 0, rgb, mode); }
    void side(int i, uint32_t rgb, LedMode mode = LedMode::Static)       { set(ControlKind::Side, 0, i, rgb, mode); }
};

// Timing and cleanup ---------------------------------------------------------------------------------------

// Waits while the message loop runs; false when Ctrl+C or a closing console asked to stop.
bool waitFor(double seconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int) (seconds * 1000.0));
    while (std::chrono::steady_clock::now() < end)
    {
        if (stopRequested) return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return ! stopRequested;
}

#if JUCE_WINDOWS
BOOL WINAPI onConsoleEvent(DWORD)
{
    stopRequested = true;
    // The process dies when this returns (for a close event, a few seconds later): wait for the pads to be put back.
    for (int i = 0; i < 400 && ! cleanupDone; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return TRUE;
}
#endif

void onSignal(int) { stopRequested = true; }

// A pad opened through the driver. Leaving scope (normally, early or on Ctrl+C) lights nothing and restores Live mode.
struct Session
{
    explicit Session(bool rgb = true) : driver(rgb) {}
    ~Session() { say("  (closing: pads dark, then Live mode)"); driver.close(); }

    bool open()
    {
        say("Opening the Launchpad X in Programmer mode ...");
        if (! driver.open(ports)) { say("Could not open the pad. Is it connected, and not held by another program?"); return false; }
        return true;
    }

    void show(const Frame& f) { driver.setLeds(f.leds); }

    TapPorts        ports;
    LaunchpadXDriver driver;
};

// Steps -----------------------------------------------------------------------------------------------------

// 1: RGB lighting through the batched SysEx.
void stepRgb(double holdSeconds)
{
    say("\n== Step 1: RGB test ==");
    Session s;
    if (! s.open()) return;

    // Part A: a gradient. Red rises left to right, green falls top to bottom, blue is fixed at 25 %.
    say("Part A (3 s): gradient on all 64 pads. Red rises to the RIGHT, green rises to the TOP, blue fixed; top buttons white ramp, side buttons grey ramp.");
    Frame a;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) a.pad(x, y, ((uint32_t) (x * 255 / 7) << 16) | ((uint32_t) ((7 - y) * 255 / 7) << 8) | 0x40u);
    for (int i = 0; i < 8; ++i) { const uint32_t g = (uint32_t) (32 + i * 32); a.top(i, (g << 16) | (g << 8) | g); a.side(i, (g << 16) | (g << 8) | g); }
    s.show(a);
    if (! waitFor(3.0)) return;

    // Part B: the eight layer colours, one per row (the top row is layer 1); left half bright, right half dim.
    say("Part B (", holdSeconds, " s): one row per layer colour, top row = layer 1. Columns 1-4 BRIGHT, columns 5-8 DIM (x0.45). Side buttons = the layer colour bright.");
    Frame b;
    for (int layer = 0; layer < 8; ++layer)
    {
        const int y = layer;   // the same convention as the surface mapping: layer 1 on the top row
        for (int x = 0; x < 8; ++x) b.pad(x, y, x < 4 ? kLayerRgb[layer] : scale(kLayerRgb[layer], 0.45f));
        b.side(y, kLayerRgb[layer]);
        say("   layer ", layer + 1, " ", kLayerName[layer], ": bright ", rgbText(kLayerRgb[layer]), "  dim ", rgbText(scale(kLayerRgb[layer], 0.45f)));
    }
    s.show(b);
    waitFor(holdSeconds);
}

// 2: the palette forms with the device's own flash and pulse.
void stepPalette(double holdSeconds)
{
    say("\n== Step 2: palette fallback (static, flashing, pulsing) ==");
    Session s(false);
    if (! s.open()) return;

    // The hues of the palette picture, picked by nearest entry in the driver.
    const uint32_t hues[8] = { 0xFF6259, 0xFFB064, 0xFFF14C, 0x4CFF4C, 0x65EBFF, 0x5B5DFD, 0xFF62FF, 0xFFFFFF };
    const char* names[8] = { "red", "orange", "yellow", "green", "cyan", "blue", "magenta", "white" };
    Frame f;
    for (int x = 0; x < 8; ++x)
    {
        f.pad(x, 0, hues[x], LedMode::Static);
        f.pad(x, 1, hues[x], LedMode::Flash);
        f.pad(x, 2, hues[x], LedMode::Pulse);
        f.pad(x, 4, scale(hues[x], 0.30f), LedMode::Static);
        say("   column ", x + 1, " ", names[x], " -> palette index ", (int) launchpadx::nearestPaletteIndex(hues[x]),
            ", dim -> ", (int) launchpadx::nearestPaletteIndex(scale(hues[x], 0.30f)));
    }
    say("Top row of pads = STATIC, second row = FLASHING (colour / off), third row = PULSING, fifth row = static DIM versions. Holding ", holdSeconds, " s.");
    s.show(f);
    waitFor(holdSeconds);
}

// 3: the layer hues, bright against dim, and the palette picture beside them.
void stepHues(double holdSeconds)
{
    say("\n== Step 3: layer hue table ==");
    Session s;
    if (! s.open()) return;

    Frame f;
    say("Columns = the 8 layer colours (red cyan orange magenta lime rose silver navy). Rows from the bottom:");
    say("   1 bright RGB   2 dim x0.45   3 dim x0.30 (the planned level)   4 dim x0.20   5 palette-picture bright   6 palette-picture dim");
    for (int layer = 0; layer < 8; ++layer)
    {
        const auto& hue = launchpadx::kLayerHues[(size_t) layer];
        uint32_t paletteBright = 0, paletteDim = 0;
        for (const auto& e : launchpadx::kPalette)
        {
            if (e.index == hue.bright) paletteBright = e.rgb;
            if (e.index == hue.dim) paletteDim = e.rgb;
        }
        f.pad(layer, 7, kLayerRgb[layer]);
        f.pad(layer, 6, scale(kLayerRgb[layer], 0.45f));
        f.pad(layer, 5, scale(kLayerRgb[layer], 0.45f));
        f.pad(layer, 4, scale(kLayerRgb[layer], 0.20f));
        f.pad(layer, 3, paletteBright);
        f.pad(layer, 2, paletteDim);
        say("   ", kLayerName[layer], ": ", rgbText(kLayerRgb[layer]), "   palette picture ", rgbText(paletteBright), " / ", rgbText(paletteDim));
    }
    s.show(f);
    say("Holding ", holdSeconds, " s.");
    waitFor(holdSeconds);
}

// 4: every press and release, decoded, plus the raw logo CC 99.
void stepInput(double listenSeconds)
{
    say("\n== Step 4: input echo (", listenSeconds, " s) ==");
    // The MIDI thread queues the decoded events; this thread prints them and lights the pressed pad.
    // Declared before the session so they outlive its callbacks.
    std::mutex queueLock;
    std::deque<SurfaceEvent> queue;
    Session s;
    if (! s.open()) return;

    s.driver.onEvent = [&](const SurfaceEvent& e) { const std::lock_guard<std::mutex> lock(queueLock); queue.push_back(e); };

    say("Press pads, the round buttons on top, the right-hand column and the LOGO. A pressed control lights white while held.");
    Frame held;
    s.show(held);

    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int) (listenSeconds * 1000.0));
    while (std::chrono::steady_clock::now() < end && ! stopRequested)
    {
        std::deque<SurfaceEvent> events;
        { const std::lock_guard<std::mutex> lock(queueLock); events.swap(queue); }

        for (const auto& e : events)
        {
            const auto idx = launchpadx::indexOf(e.id);
            const char* kind = e.id.kind == ControlKind::Pad ? "pad" : e.id.kind == ControlKind::Top ? "TOP button" : e.id.kind == ControlKind::Side ? "SIDE button" : "special";
            const char* what = e.type == SurfaceEventType::Press ? "PRESS  " : e.type == SurfaceEventType::Release ? "release" : "value  ";
            say("  EVENT ", what, " ", kind, " x=", e.id.x, " y(from top)=", e.id.y, " number=", idx ? juce::String(*idx) : juce::String("-"));
            if (e.type != SurfaceEventType::Value) held.set(e.id.kind, e.id.x, e.id.y, e.type == SurfaceEventType::Press ? 0xFFFFFF : 0);
        }
        if (! events.empty()) s.show(held);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    s.driver.onEvent = nullptr;
}

// 6: the colour lab. The right-hand buttons pick a layer colour; the whole pad shows it (top half bright, bottom half dim);
// the top buttons nudge its red, green and blue and the dim scale. Every change is printed; the final table is printed at the end.
void stepColourLab(double seconds)
{
    say("\n== Step 6: colour lab (", seconds, " s) ==");
    std::mutex queueLock;
    std::deque<SurfaceEvent> queue;
    Session s;
    if (! s.open()) return;
    s.driver.onEvent = [&](const SurfaceEvent& e) { const std::lock_guard<std::mutex> lock(queueLock); queue.push_back(e); };

    uint32_t colours[8];
    for (int i = 0; i < 8; ++i) colours[i] = kLayerRgb[i];
    float dimScale = 0.45f;
    int layer = 0;

    say("Right-hand buttons (top = layer 1 ... bottom = layer 8) pick the colour. The pad shows it: top four rows bright, bottom four rows dim.");
    say("Top buttons, left to right: red -, red +, green -, green +, blue -, blue +, dim -, dim +.");

    auto channel = [&](int c) -> int { return (int) ((colours[layer] >> (16 - 8 * c)) & 0xFFu); };
    auto setChannel = [&](int c, int v)
    {
        v = juce::jlimit(0, 255, v);
        const int shift = 16 - 8 * c;
        colours[layer] = (colours[layer] & ~(0xFFu << shift)) | ((uint32_t) v << shift);
    };
    auto render = [&]
    {
        Frame f;
        const uint32_t bright = colours[layer], dim = scale(colours[layer], dimScale);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) f.pad(x, y, y < 4 ? bright : dim);
        for (int i = 0; i < 8; ++i) f.side(i, i == layer ? 0xFFFFFF : colours[i]);
        for (int i = 0; i < 6; ++i) { const uint32_t ch = i % 2 == 0 ? 0x400000u >> (i / 2 * 8) : 0xFF0000u >> (i / 2 * 8); f.top(i, ch); }
        f.top(6, 0x303030); f.top(7, 0xA0A0A0);
        s.show(f);
    };
    auto report = [&]
    {
        say("  layer ", layer + 1, " ", kLayerName[layer], ": bright ", rgbText(colours[layer]), "  dim (x", dimScale, ") ", rgbText(scale(colours[layer], dimScale)));
    };

    render();
    report();

    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int) (seconds * 1000.0));
    while (std::chrono::steady_clock::now() < end && ! stopRequested)
    {
        std::deque<SurfaceEvent> events;
        { const std::lock_guard<std::mutex> lock(queueLock); events.swap(queue); }

        bool changed = false;
        for (const auto& e : events)
        {
            if (e.type != SurfaceEventType::Press) continue;
            if (e.id.kind == ControlKind::Side) { layer = e.id.y; changed = true; }
            else if (e.id.kind == ControlKind::Top)
            {
                const int i = e.id.x;
                if (i < 6) setChannel(i / 2, channel(i / 2) + (i % 2 == 0 ? -8 : 8));
                else dimScale = juce::jlimit(0.05f, 1.0f, dimScale + (i == 6 ? -0.05f : 0.05f));
                changed = true;
            }
        }
        if (changed) { render(); report(); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    s.driver.onEvent = nullptr;

    say("\nFinal layer colours (bright) and dim scale ", dimScale, ":");
    for (int i = 0; i < 8; ++i) say("   layer ", i + 1, " ", kLayerName[i], ": ", rgbText(colours[i]), "   dim ", rgbText(scale(colours[i], dimScale)));
}

// 7: the "Surface colours" window on the pad (colour-corrected by the driver), row for row: layer colours bright, layer dim, app accents bright, app dim
// (each accent two pads wide). Compare the pad with that window.
void stepSwatch(double seconds)
{
    say("\n== Step 7: surface colour swatch (", seconds, " s) ==");
    Session s;
    if (! s.open()) return;

    constexpr uint32_t kAppRgb[4] = { 0x7F77DD, 0x4ADC8E, 0x3D8BEB, 0xF2C53D };
    // The screen colours go to the driver as the app sends them; the driver applies the pad correction
    // (LaunchpadXColourMap.h), and dim is the screen colour scaled, which it corrects in pad space.
    constexpr float dimScale = 0.20f;
    Frame f;
    for (int x = 0; x < 8; ++x)
    {
        f.pad(x, 0, kLayerRgb[x]);
        f.pad(x, 1, scale(kLayerRgb[x], dimScale));
        f.pad(x, 2, kAppRgb[x / 2]);
        f.pad(x, 3, scale(kAppRgb[x / 2], dimScale));
    }
    s.show(f);
    say("Rows, top to bottom: layer colours 1-8 bright, layer colours dim, app accents (purple, green, blue, yellow; two pads each) bright, app accents dim.");
    waitFor(seconds);
}

// A LiveView with settable fields for the demo.
struct DemoView : mu_core::LiveView
{
    DemoView()
    {
        const juce::File folder("C:/presets");
        for (int i = 0; i < 4; ++i)
            performance.setItem(i, "Demo", folder.getChildFile("p" + juce::String(i) + ".preset"), folder, "Preset " + juce::String(i + 1));
    }

    void notify() { ++revision; listeners.call([](Listener& l) { l.liveStateChanged(); }); }

    bool isPerformanceMode() const noexcept override { return mode; }
    const mu_pp::PerformanceFile& getPerformance() const noexcept override { return performance; }
    juce::File getPerformanceFile() const override { return {}; }
    int  getCurrentItem() const noexcept override { return current; }
    int  getQueuedItem() const noexcept override { return queued; }
    int  getSelectedLayer() const noexcept override { return selected; }
    int  getLayerCount() const override { return 4; }
    int  getLayerColourIndex(int layer) const override { return layer; }
    bool isItemPlayable(int item) const override { return item >= 0 && item < 4; }
    uint32_t getRevision() const noexcept override { return revision; }
    int  getPlayingClip(int) const override { return -1; }
    int  getQueuedClip(int) const override { return -1; }
    uint8_t getClipMask(int) const override { return 0; }
    bool isLayerMuted(int) const override { return false; }
    void addListener(Listener* l) const override    { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }

    bool mode = false;
    mu_pp::PerformanceFile performance;
    int current = 0, queued = -1, selected = 0;
    uint32_t revision = 0;
    mutable juce::ListenerList<Listener> listeners;
};

// Performs the actions the way Performance mode would: a pressed item queues, then commits after two seconds.
struct DemoSink : mu_core::ControlSink, private juce::Timer
{
    explicit DemoSink(DemoView& v) : view(v) {}
    ~DemoSink() override { stopTimer(); }

    bool perform(const mu_core::ControlAction& a) override
    {
        say("  ACTION ", mu_core::actionName(a.type), " layer=", a.layer, " index=", a.index);
        if (a.type == mu_core::ControlActionType::SelectLayer)
        {
            view.selected = a.layer;
            view.notify();
            return true;
        }
        if (a.type == mu_core::ControlActionType::PerformanceItem)
        {
            if (a.index == view.current) view.queued = -1;   // the current item cancels a queue
            else { view.queued = a.index; startTimer(2000); }
            view.notify();
            return true;
        }
        return false;
    }

    void timerCallback() override
    {
        stopTimer();
        if (view.queued >= 0) { view.current = view.queued; view.queued = -1; view.notify(); }
    }

    DemoView& view;
};

struct StatusPrinter : mu_core::ControlAttachment::Listener
{
    explicit StatusPrinter(const mu_core::ControlAttachment& a) : attachment(a) { attachment.addListener(this); }
    ~StatusPrinter() override { attachment.removeListener(this); }
    void controlStatusChanged() override
    {
        const char* names[] = { "Not found", "Found", "In use", "Released", "Lost" };
        say("  STATUS ", names[(int) attachment.getStatus()], "   device \"", attachment.getDeviceLabel(), "\"");
    }
    const mu_core::ControlAttachment& attachment;
};

// 5: the real manager over the real pad with a fake performance, so the top row and right column light as in Performance mode.
void stepManager(double seconds)
{
    say("\n== Step 5: full manager demo (", seconds, " s) ==");
    say("A pretend performance: 4 layers, 4 items, item 1 playing. Top row: item 1 bright, items 2-4 dim. Right column: layer 1 bright, layers 2-4 dim (rows 5-8 dark), layer 1 at the top.");
    say("Press a top button: that item PULSES (queued), and becomes the bright one after 2 s. Press a right-hand button to select a layer.");

    DemoView view;
    DemoSink sink(view);
    mu_core::SurfaceLook look;
    for (size_t i = 0; i < 8; ++i) look.layerRgb[i] = kLayerRgb[i];
    look.accentRgb = 0x40A0FF;
    look.faultRgb = 0xFF3030;

    {
        ControllerManager manager(sink, view, look, juce::File(), [] { return ControllerBackend { std::make_unique<TapPorts>(), std::make_unique<LaunchpadXDriver>(), LaunchpadXDriver::defaultPorts() }; });
        StatusPrinter printer(manager);
        say("  STATUS at start: ", manager.getStatus() == mu_core::ControlStatus::Found ? "Found (pad detected, nothing opened)" : "NOT FOUND", "   device \"", manager.getDeviceLabel(), "\"");

        if (manager.getStatus() != mu_core::ControlStatus::NotFound)
        {
            say("Turning Performance mode ON (the manager opens the pad now).");
            view.mode = true;
            view.notify();
            waitFor(seconds);
            say("Turning Performance mode OFF (the manager puts the pad back in Live mode).");
            view.mode = false;
            view.notify();
            waitFor(0.5);
        }
    }   // the manager's destructor also restores Live mode
}

void listPorts()
{
    JuceDevicePorts ports;
    say("MIDI outputs:");
    for (const auto& n : ports.outputNames()) say("   ", n);
    say("MIDI inputs:");
    for (const auto& n : ports.inputNames()) say("   ", n);
    const auto spec = LaunchpadXDriver::defaultPorts();
    const auto has = [](const juce::StringArray& names, const juce::String& p) { for (const auto& n : names) if (n.matchesWildcard(p, true)) return true; return false; };
    say("Launchpad X expected at output \"", spec.outputPattern, "\" and input \"", spec.inputPattern, "\": ",
        has(ports.outputNames(), spec.outputPattern) && has(ports.inputNames(), spec.inputPattern) ? "both present" : "NOT both present");
}

void usage()
{
    say("\nUsage: mu-control-probe [--seconds N] <step>...");
    say("  1  RGB test (gradient, then layer colours bright and dim; holds 8 s)");
    say("  2  palette fallback: static, flashing, pulsing (holds 8 s)");
    say("  3  layer hue table: bright against dim, RGB against palette picture (holds 8 s)");
    say("  4  input echo (30 s): decoded presses and raw logo CC 99");
    say("  5  manager demo with 4 layers and 4 items (30 s)");
    say("  6  colour lab: pick a layer colour, nudge RGB and the dim level by eye (300 s)");
    say("  7  surface colour swatch: the Surface colours window on the pad, row for row (300 s)");
    say("Every step ends with the pads dark and Live mode restored; Ctrl+C does the same.");
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::signal(SIGINT, onSignal);
#if JUCE_WINDOWS
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#endif

    double seconds = -1.0;
    std::vector<int> steps;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if (arg == "--seconds" && i + 1 < argc) seconds = juce::String(argv[++i]).getDoubleValue();
        else if (arg.containsOnly("1234567") && arg.length() == 1) steps.push_back(arg.getIntValue());
        else if (arg != "--list") { say("Unknown argument: ", arg); usage(); return 2; }
    }

    listPorts();
    if (steps.empty()) { usage(); cleanupDone = true; return 0; }

    for (const int step : steps)
    {
        if (stopRequested) break;
        switch (step)
        {
            case 1: stepRgb(seconds > 0 ? seconds : 8.0); break;
            case 2: stepPalette(seconds > 0 ? seconds : 8.0); break;
            case 3: stepHues(seconds > 0 ? seconds : 8.0); break;
            case 4: stepInput(seconds > 0 ? seconds : 30.0); break;
            case 5: stepManager(seconds > 0 ? seconds : 30.0); break;
            case 6: stepColourLab(seconds > 0 ? seconds : 300.0); break;
            case 7: stepSwatch(seconds > 0 ? seconds : 300.0); break;
            default: break;
        }
    }

    say(stopRequested ? "\nStopped: pads dark and Live mode restored." : "\nDone: pads dark and Live mode restored.");
    cleanupDone = true;
    return stopRequested ? 130 : 0;
}
