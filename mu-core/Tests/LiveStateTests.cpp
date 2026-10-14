// mu_core::LiveState: the live state behind Performance mode, driven with fake callbacks (no processor,
// no hot-swap machinery). Guards the action rules in docs/design-plugin-family.md "Performance mode and live
// state" section 5: the mode gate, item switching and cancelling, wrapping Prev / Next, SelectLayer bounds,
// the commit hook, queue bookkeeping when another preset is loaded meanwhile, change notification and the
// host-session persistence.

#include "Control/LiveState.h"
#include <array>

namespace
{
using namespace mu_core;
using Stage = LiveState::Stage;

// A fake owner: stages items against a pretend transport, with the serial and pending flag a real hot-swap
// stager would show.
struct Fake
{
    bool supported = true;
    bool stopped   = false;               // a stopped transport applies a staged item at once
    bool failNext  = false;               // the next stage reports a load failure
    bool loadable[8] = { true, true, true, true, false, false, true, false };
    int  layers = 4;
    std::vector<int> staged;
    int  cancels = 0;
    bool pending = false;
    uint32_t serial = 0;

    // The clip bank's owner side: a launch stages on the layer (pending unless the transport is stopped) under
    // a new per-layer serial, as the hot-swap stager does; a commit consumes without touching the serial.
    bool refuseLaunch = false;
    std::vector<std::pair<int, int>> launched;
    std::array<bool, 8>     slotPend {};
    std::array<uint32_t, 8> slotSer {};
    std::array<bool, 8>     muted {};
    std::array<int, 8>      recorded { -1, -1, -1, -1, -1, -1, -1, -1 };
    int mutes = 0;
    int slotCancels = 0;

    LiveState::Callbacks callbacks()
    {
        LiveState::Callbacks cb;
        cb.supportsPerformanceMode = [this] { return supported; };
        cb.canLoad    = [this](int i) { return i >= 0 && i < 8 && loadable[i]; };
        cb.stageItem  = [this](int i)
        {
            if (failNext) { failNext = false; return Stage::Failed; }
            staged.push_back(i);
            ++serial;
            if (stopped) return Stage::Applied;
            pending = true;
            return Stage::Queued;
        };
        cb.cancelItem  = [this] { ++cancels; pending = false; };
        cb.layerCount  = [this] { return layers; };
        cb.fullPending = [this] { return pending; };
        cb.stageSerial = [this] { return serial; };
        cb.launchClip  = [this](int l, int c)
        {
            if (refuseLaunch) return false;
            launched.push_back({ l, c });
            ++slotSer[(size_t) l];
            slotPend[(size_t) l] = ! stopped;
            return true;
        };
        cb.slotPending = [this](int l) { return slotPend[(size_t) l]; };
        cb.slotSerial  = [this](int l) { return slotSer[(size_t) l]; };
        cb.cancelSlot  = [this](int l) { ++slotCancels; slotPend[(size_t) l] = false; ++slotSer[(size_t) l]; };
        cb.muteLayer   = [this](int l) { ++mutes; muted[(size_t) l] = ! muted[(size_t) l]; return true; };
        cb.layerMuted  = [this](int l) { return muted[(size_t) l]; };
        cb.clipPlaying = [this](int l, int c) { recorded[(size_t) l] = c; };
        return cb;
    }

    // Another full preset (browser, MIDI program change) staged over whatever was waiting.
    void foreignStage() { ++serial; pending = true; }
    // The staged preset reaches its boundary and lands.
    void commit(LiveState& s) { pending = false; s.fullPresetCommitted(); }
    // The staged layer preset reaches its boundary and lands (the owner then runs its deferred pass).
    void slotCommit(LiveState& s, int layer) { slotPend[(size_t) layer] = false; s.reconcile(); }
    // Another layer-preset load replaced the staged payload; or an edit cancelled it.
    void foreignSlotStage(int layer) { ++slotSer[(size_t) layer]; slotPend[(size_t) layer] = true; }
    void cancelFromElsewhere(int layer) { ++slotSer[(size_t) layer]; slotPend[(size_t) layer] = false; }
};

struct CountingListener : LiveView::Listener
{
    int calls = 0;
    void liveStateChanged() override { ++calls; }
};

mu_pp::PerformanceFile makeList(int items)
{
    mu_pp::PerformanceFile p;
    const juce::File folder("C:/presets");
    for (int i = 0; i < items; ++i)
        p.setItem(i, "MuTantState", folder.getChildFile("p" + juce::String(i) + ".muTant"), folder, "P" + juce::String(i));
    return p;
}

ControlAction act(ControlActionType t, int layer = 0, int index = -1)
{
    ControlAction a;
    a.type = t; a.layer = layer; a.index = index;
    return a;
}
ControlAction mode(int onOffToggle, int layer = 0) { return act(ControlActionType::PerformanceMode, layer, onOffToggle); }
}

class LiveStateTests : public juce::UnitTest
{
public:
    LiveStateTests() : juce::UnitTest("Live state (Performance mode)", "LiveState") {}

    void runTest() override
    {
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("mu_live_" + juce::String(juce::Time::currentTimeMillis()) + ".muPerformance");

        beginTest("the mode needs a performance and a product that supports it; every other action needs the mode");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            expect(! s.perform(mode(-1)), "no performance loaded: refused");
            expect(! s.isPerformanceMode());
            expect(s.loadPerformance(makeList(4), file));
            f.supported = false;
            expect(! s.perform(mode(-1)), "unsupported product: refused");
            f.supported = true;
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 1)), "mode off: item refused");
            expect(! s.perform(act(ControlActionType::SelectLayer, 1)), "mode off: select refused");
            expect(! s.perform(act(ControlActionType::PerformanceNext)), "mode off: next refused");
            expect(s.perform(mode(1, 2)), "on");
            expect(s.isPerformanceMode());
            expectEquals(s.getSelectedLayer(), 2, "entering seeds the selected layer");
            expect(s.perform(mode(0)), "off");
            expect(! s.isPerformanceMode());
            expect(s.perform(mode(-1)), "a bare action (index unset) toggles on, as a mapped button's press would");
            expect(s.isPerformanceMode());
            expect(s.perform(mode(-1)), "and off");
            expect(! s.isPerformanceMode());
            expect(s.perform(act(ControlActionType::PerformanceMode)), "the default-constructed action toggles");
            expect(s.isPerformanceMode());
        }

        beginTest("an item stages and queues; the commit makes it current; a loaded item's own press is a no-op");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)));
            expectEquals(s.getQueuedItem(), 2);
            expectEquals(s.getCurrentItem(), -1, "current moves only at the commit");
            expectEquals((int) f.staged.size(), 1);
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)), "the queued item again: already on its way");
            expectEquals((int) f.staged.size(), 1, "and it is not staged twice");
            f.commit(s);
            expectEquals(s.getCurrentItem(), 2);
            expectEquals(s.getQueuedItem(), -1);
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)), "the playing item with nothing queued");
            expectEquals((int) f.staged.size(), 1, "nothing staged");
        }

        beginTest("pressing the playing item while another is queued cancels the switch");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1)); f.commit(s);
            s.perform(act(ControlActionType::PerformanceItem, 0, 3));
            expectEquals(s.getQueuedItem(), 3);
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 1)));
            expectEquals(f.cancels, 1, "the staged preset was cancelled");
            expectEquals(s.getQueuedItem(), -1);
            expectEquals(s.getCurrentItem(), 1);
        }

        beginTest("a stopped transport applies at once: the item becomes current without a queue");
        {
            Fake f; f.stopped = true; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 3)));
            expectEquals(s.getCurrentItem(), 3);
            expectEquals(s.getQueuedItem(), -1);
        }

        beginTest("a load that fails is refused: nothing queued, nothing current, nothing staged");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            f.failNext = true;
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 1)));
            expectEquals(s.getQueuedItem(), -1);
            expectEquals(s.getCurrentItem(), -1);
        }

        beginTest("an item that cannot load, or an index out of range, is refused and stages nothing");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(8), file);
            s.perform(mode(1));
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 4)), "item 4 is missing");
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 8)), "past the end");
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, -1)), "no index");
            expectEquals((int) f.staged.size(), 0);
        }

        beginTest("another preset staged over the queued item: the queue clears, and its commit leaves no item current");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1)); f.commit(s);
            s.perform(act(ControlActionType::PerformanceItem, 0, 2));
            expectEquals(s.getQueuedItem(), 2);
            f.foreignStage();                       // the browser (or a program change) loads something else meanwhile
            s.reconcile();
            expectEquals(s.getQueuedItem(), -1, "item 2 is no longer on its way");
            expectEquals(s.getCurrentItem(), 1, "and the old item still plays until the new preset lands");
            f.commit(s);
            expectEquals(s.getCurrentItem(), -1, "a preset that is not an item took over: no item is current");
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)), "item 2 can be queued again");
            expectEquals(s.getQueuedItem(), 2);
        }

        beginTest("a queued item applied early (without a commit hook) settles at the next reconcile");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 3));
            f.pending = false;                      // a layer-preset load applied the staged full preset at once
            s.reconcile();
            expectEquals(s.getCurrentItem(), 3);
            expectEquals(s.getQueuedItem(), -1, "the queued lamp does not stick");
        }

        beginTest("Prev and Next wrap, skip unusable items and step from the queued item");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());   // loadable: 0 1 2 3 and 6
            s.loadPerformance(makeList(8), file);
            s.perform(mode(1));
            expect(s.perform(act(ControlActionType::PerformanceNext)));
            expectEquals(s.getQueuedItem(), 0, "from nothing, Next lands on the first loadable item");
            f.commit(s);
            s.perform(act(ControlActionType::PerformanceNext)); s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 2, "two presses before the commit land two items on");
            f.commit(s);
            s.perform(act(ControlActionType::PerformanceNext)); f.commit(s);   // 3
            s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 6, "4 and 5 are missing: Next skips to 6");
            f.commit(s);
            s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 0, "and wraps past 7 (also missing) to 0");
            f.commit(s);
            s.perform(act(ControlActionType::PerformancePrev));
            expectEquals(s.getQueuedItem(), 6, "Prev wraps back");
        }

        beginTest("Prev and Next with no other loadable item refuse");
        {
            Fake f; for (bool& b : f.loadable) b = false; f.loadable[2] = true;
            LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(8), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 2)); f.commit(s);
            expect(! s.perform(act(ControlActionType::PerformanceNext)));
            expect(! s.perform(act(ControlActionType::PerformancePrev)));
        }

        beginTest("SelectLayer is absolute, bounded by the live layer count, and bumps the revision only on a change");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(2), file);
            s.perform(mode(1));
            const auto rev = s.getRevision();
            expect(s.perform(act(ControlActionType::SelectLayer, 3)));
            expectEquals(s.getSelectedLayer(), 3);
            expect(s.getRevision() > rev);
            const auto rev2 = s.getRevision();
            expect(s.perform(act(ControlActionType::SelectLayer, 3)), "the same layer again is fine");
            expectEquals((int) s.getRevision(), (int) rev2, "and changes nothing");
            expect(! s.perform(act(ControlActionType::SelectLayer, 4)), "past the live layers");
            expect(! s.perform(act(ControlActionType::SelectLayer, -1)));
            expectEquals(s.getSelectedLayer(), 3);
        }

        beginTest("a commit that shrinks the layer count pulls the selected layer back inside it");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::SelectLayer, 3));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));
            f.layers = 2;                           // the new preset has two layers
            f.commit(s);
            expectEquals(s.getSelectedLayer(), 1);
        }

        beginTest("leaving the mode drops a queued item and cancels its staged preset; a list cannot be swapped while it is on");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));
            expect(! s.loadPerformance(makeList(2), file), "refused while the mode is on");
            s.perform(mode(0));
            expectEquals(s.getQueuedItem(), -1);
            expectEquals(f.cancels, 1);
        }

        beginTest("listeners hear every change on the message thread");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            CountingListener l; s.addListener(&l);
            s.loadPerformance(makeList(4), file);
            s.perform(mode(1));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));
            f.commit(s);
            expectEquals(l.calls, 4);
            s.removeListener(&l);
            s.perform(act(ControlActionType::SelectLayer, 2));
            expectEquals(l.calls, 4, "a removed listener hears nothing");
        }

        beginTest("the session keeps the performance path, current item and mode; not the selection or the queue");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            auto list = makeList(4);
            expect(list.save(file));
            mu_pp::PerformanceFile loaded; mu_pp::PerformanceFile::load(file, loaded);
            s.loadPerformance(loaded, file);
            s.perform(mode(1, 2));
            s.perform(act(ControlActionType::PerformanceItem, 0, 3)); f.commit(s);
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));   // queued only
            juce::ValueTree state("MuTantState");
            s.saveTo(state);

            LiveState t; t.setCallbacks(f.callbacks());
            t.restoreFrom(state);
            expect(t.isPerformanceMode(), "the mode returns");
            expectEquals(t.getCurrentItem(), 3);
            expectEquals(t.getQueuedItem(), -1, "the queue is not saved");
            expectEquals(t.getSelectedLayer(), 0, "the selected layer is not saved");
            expectEquals(t.getPerformance().count(), 4);

            f.supported = false;
            LiveState u; u.setCallbacks(f.callbacks());
            u.restoreFrom(state);
            expect(! u.isPerformanceMode(), "an unsupported build never comes back locked");
            expectEquals(u.getCurrentItem(), 3);

            file.deleteFile();
            Fake gone; LiveState v; v.setCallbacks(gone.callbacks());
            v.restoreFrom(state);
            expectEquals(v.getPerformance().count(), 0, "a performance file that is gone restores nothing");
            expect(! v.isPerformanceMode());

            juce::ValueTree empty("MuTantState"); empty.setProperty("performancePath", "x", nullptr);
            LiveState w; w.saveTo(empty);
            expect(! empty.hasProperty("performancePath"), "no performance: the properties are cleared");
        }

        beginTest("layer count and layer colour come through the owner's callbacks, with defaults when none are supplied");
        {
            LiveState bare;
            expectEquals(bare.getLayerCount(), 1, "no callback: one layer");
            expectEquals(bare.getLayerColourIndex(3), 3, "no callback: the palette in order");
            expectEquals(bare.getLayerColourIndex(11), 3, "no callback: wraps at 8");
            expectEquals(bare.getLayerColourIndex(-1), 0);

            Fake f; f.layers = 5;
            auto cb = f.callbacks();
            cb.layerColourIndex = [](int layer) { return 7 - layer; };
            LiveState s; s.setCallbacks(cb);
            expectEquals(s.getLayerCount(), 5);
            expectEquals(s.getLayerColourIndex(0), 7);
            expectEquals(s.getLayerColourIndex(2), 5);
            f.layers = 2;
            expectEquals(s.getLayerCount(), 2, "the count is read live, not cached");
        }

        clipTests(file);
    }

    // The clip bank's live half: launch, queue identity, the pad gestures, structural edits and stepping.
    void clipTests(const juce::File& file)
    {
        // A performance with the mode on, so the clip actions are allowed; layer 0 selected.
        auto ready = [&file](Fake& f, LiveState& s, int selected = 0)
        {
            s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(2), file);
            s.perform(mode(1, selected));
        };
        const auto launch = [](int layer, int clip) { return act(ControlActionType::LaunchClip, layer, clip); };

        beginTest("a clip launch while playing queues, the commit makes it playing and records it with the layer");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b0111);
            expect(s.getClipMask(0) == 0b0111 && s.getClipMask(1) == 0);
            expect(s.perform(launch(0, 1)));
            expectEquals(s.getQueuedClip(0), 1);
            expectEquals(s.getPlayingClip(0), -1, "playing moves only at the commit");
            expectEquals((int) f.launched.size(), 1);
            expect(s.perform(launch(0, 1)), "the queued clip again: already on its way");
            expectEquals((int) f.launched.size(), 1, "and it is not staged twice");
            f.slotCommit(s, 0);
            expectEquals(s.getPlayingClip(0), 1);
            expectEquals(s.getQueuedClip(0), -1);
            expectEquals(f.recorded[0], 1, "the owner is told, to save it as the layer's active clip");
            expectEquals(f.mutes, 0, "a launch never mutes");
        }

        beginTest("a stopped transport applies the clip at once: playing, nothing queued");
        {
            Fake f; f.stopped = true; LiveState s; ready(f, s);
            s.setClipMask(2, 0b1000);
            expect(s.perform(launch(2, 3)));
            expectEquals(s.getPlayingClip(2), 3);
            expectEquals(s.getQueuedClip(2), -1);
            expectEquals(f.recorded[2], 3);
        }

        beginTest("an empty pad, a bad index, a missing layer, the mode off and a refused launch change nothing");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b0010);
            expect(! s.perform(launch(0, 0)), "empty pad");
            expect(! s.perform(launch(0, 8)) && ! s.perform(launch(0, -1)), "clip out of range");
            expect(! s.perform(launch(4, 1)) && ! s.perform(launch(-1, 1)), "no such layer");
            f.refuseLaunch = true;
            expect(! s.perform(launch(0, 1)), "the owner refuses (a full preset is staged)");
            expectEquals(s.getQueuedClip(0), -1);
            expectEquals(s.getPlayingClip(0), -1);
            expectEquals((int) f.launched.size(), 0);
            s.perform(mode(0));
            f.refuseLaunch = false;
            expect(! s.perform(launch(0, 1)), "mode off");
            expect(! s.perform(act(ControlActionType::ClipNext)) && ! s.perform(act(ControlActionType::ClipPrev)), "mode off: stepping too");
        }

        beginTest("the playing pad mutes and unmutes; with another clip queued the first press cancels the queue");
        {
            Fake f; f.stopped = true; LiveState s; ready(f, s);
            s.setClipMask(0, 0b0110);
            s.perform(launch(0, 1));
            expectEquals(s.getPlayingClip(0), 1);
            expect(s.perform(launch(0, 1)));
            expectEquals(f.mutes, 1);
            expect(s.isLayerMuted(0), "the strip's mute is read through the owner");
            expect(s.perform(launch(0, 1)));
            expectEquals(f.mutes, 2);
            expect(! s.isLayerMuted(0));

            f.stopped = false;
            s.perform(launch(0, 2));
            expectEquals(s.getQueuedClip(0), 2);
            expect(s.perform(launch(0, 1)), "the playing pad while 2 is queued");
            expectEquals(s.getQueuedClip(0), -1, "cancels the queue");
            expectEquals(f.slotCancels, 1);
            expectEquals(f.mutes, 2, "and does not mute");
            expect(! f.slotPend[0], "the staged launch is gone");
            expect(s.perform(launch(0, 1)));
            expectEquals(f.mutes, 3, "the next press mutes");
            expectEquals(s.getPlayingClip(0), 1, "the playing clip never moved");
        }

        beginTest("a layer-preset load staged over a queued clip supersedes it; so does a cancel");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b0011);
            s.perform(launch(0, 0)); f.slotCommit(s, 0);
            s.perform(launch(0, 1));
            expectEquals(s.getQueuedClip(0), 1);
            f.foreignSlotStage(0);                  // a .muPattern loaded meanwhile replaced the staged payload
            s.reconcile();
            expectEquals(s.getQueuedClip(0), -1, "clip 1 is no longer on its way");
            expectEquals(s.getPlayingClip(0), 0, "the old clip still plays until the new payload lands");
            f.slotCommit(s, 0);
            expectEquals(s.getPlayingClip(0), 0, "the load that landed was not the clip: playing is not moved to it");

            s.perform(launch(0, 1));
            expectEquals(s.getQueuedClip(0), 1);
            f.cancelFromElsewhere(0);               // a structural edit cancelled every staged swap
            s.reconcile();
            expectEquals(s.getQueuedClip(0), -1, "a cancelled launch clears the queue");
            expectEquals(s.getPlayingClip(0), 0);
        }

        beginTest("a non-clip layer-preset stage, an added or reset layer forget that layer's clips only");
        {
            Fake f; f.stopped = true; LiveState s; ready(f, s);
            s.setClipMask(0, 0b11); s.setClipMask(1, 0b11);
            s.perform(launch(0, 1)); s.perform(launch(1, 0));
            s.clearLayerClips(0);
            expectEquals(s.getPlayingClip(0), -1);
            expectEquals(s.getPlayingClip(1), 0, "another layer keeps its clip");
            expectEquals((int) s.getClipMask(0), 3, "the bank itself is not touched here");
        }

        beginTest("a full preset forgets every clip: on its commit, when applied at once, not while only staged");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b11); s.setClipMask(1, 0b11);
            s.perform(launch(0, 1)); f.slotCommit(s, 0);
            s.perform(launch(1, 0)); f.slotCommit(s, 1);
            s.perform(launch(0, 0));                // queued over the playing clip
            f.foreignStage();                       // a full preset is staged: it has not landed
            s.reconcile();
            expectEquals(s.getPlayingClip(0), 1, "still the old layers until it lands");
            expectEquals(s.getPlayingClip(1), 0);
            f.commit(s);                            // lands: the commit hook
            for (int i = 0; i < 4; ++i)
            {
                expectEquals(s.getPlayingClip(i), -1);
                expectEquals(s.getQueuedClip(i), -1);
            }

            s.perform(launch(0, 1)); f.slotCommit(s, 0);
            expectEquals(s.getPlayingClip(0), 1);
            ++f.serial;                              // a stopped load applied a full preset with no commit hook
            s.reconcile();
            expectEquals(s.getPlayingClip(0), -1, "the next reconcile notices");

            s.perform(launch(0, 1)); f.slotCommit(s, 0);
            s.reconcile();
            expectEquals(s.getPlayingClip(0), 1, "an unchanged serial leaves the clips alone");
        }

        beginTest("a performance item cancelled by the performer does not read as a full preset landing");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b11);
            s.perform(launch(0, 1)); f.slotCommit(s, 0);
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));   // queues item 1 (stages a full preset)
            s.perform(mode(0));                                          // leaving the mode cancels it
            s.reconcile();
            expectEquals(s.getPlayingClip(0), 1, "the layers were never replaced");
        }

        beginTest("remapLayers: playing clips and masks follow their layer, a moved layer's queue is dropped");
        {
            Fake f; LiveState s; ready(f, s);
            for (int i = 0; i < 4; ++i) s.setClipMask(i, (uint8_t) (0b1111 << i));
            s.perform(launch(1, 4)); f.slotCommit(s, 1);
            s.perform(launch(2, 5)); f.slotCommit(s, 2);
            s.perform(launch(0, 0)); f.slotCommit(s, 0);
            s.perform(launch(0, 1));                // layer 0 has a launch waiting
            s.perform(launch(3, 3));                // and so does layer 3
            expectEquals(s.getQueuedClip(3), 3);

            s.remapLayers([](int i) { return i == 1 ? -1 : (i > 1 ? i - 1 : i); });   // layer 2 removed (index 1)
            expectEquals(s.getPlayingClip(0), 0);
            expectEquals(s.getQueuedClip(0), 1, "a layer that did not move keeps its wait");
            expectEquals(s.getPlayingClip(1), 5, "old layer 3 is now layer 2 and plays what it played");
            expectEquals((int) s.getClipMask(1), 0b1111 << 2);
            expectEquals(s.getQueuedClip(2), -1, "the moved layer's queue is dropped");
            expectEquals(s.getPlayingClip(2), -1);
            expectEquals(s.getPlayingClip(3), -1, "the vacated top layer starts clean");
            expectEquals((int) s.getClipMask(3), 0);

            LiveState t; Fake g; ready(g, t);
            t.setClipMask(0, 0b01); t.setClipMask(2, 0b10);
            t.perform(launch(0, 0)); g.slotCommit(t, 0);
            t.perform(launch(2, 1)); g.slotCommit(t, 2);
            t.remapLayers([](int i) { return i == 0 ? 2 : (i == 2 ? 0 : i); });         // layers 1 and 3 swapped
            expectEquals(t.getPlayingClip(2), 0);
            expectEquals(t.getPlayingClip(0), 1);
            expectEquals((int) t.getClipMask(0), 0b10);
            expectEquals((int) t.getClipMask(2), 0b01);
        }

        beginTest("clearing a clip: the playing one stops being playing, a queued one has its launch cancelled");
        {
            Fake f; LiveState s; ready(f, s);
            s.setClipMask(0, 0b111);
            s.perform(launch(0, 0)); f.slotCommit(s, 0);
            s.perform(launch(0, 2));
            s.clipCleared(0, 1);
            expectEquals(s.getPlayingClip(0), 0, "an unrelated clip changes nothing");
            expectEquals(s.getQueuedClip(0), 2);
            s.clipCleared(0, 2);
            expectEquals(s.getQueuedClip(0), -1);
            expectEquals(f.slotCancels, 1, "the staged launch was cancelled");
            s.clipCleared(0, 0);
            expectEquals(s.getPlayingClip(0), -1);
        }

        beginTest("ClipNext and ClipPrev step the selected layer through the stored clips, wrapping, never muting");
        {
            Fake f; LiveState s; ready(f, s, 1);    // layer 2 selected
            s.setClipMask(1, 0b10100101);           // clips 0 2 5 7
            expect(s.perform(act(ControlActionType::ClipNext)));
            expectEquals(s.getQueuedClip(1), 0, "from nothing, Next lands on the first stored clip");
            f.slotCommit(s, 1);
            s.perform(act(ControlActionType::ClipNext)); s.perform(act(ControlActionType::ClipNext));
            expectEquals(s.getQueuedClip(1), 5, "two presses before the commit land two clips on (empties skipped)");
            f.slotCommit(s, 1);
            s.perform(act(ControlActionType::ClipNext));
            expectEquals(s.getQueuedClip(1), 7);
            f.slotCommit(s, 1);
            s.perform(act(ControlActionType::ClipNext));
            expectEquals(s.getQueuedClip(1), 0, "wraps past 7");
            f.slotCommit(s, 1);
            s.perform(act(ControlActionType::ClipPrev));
            expectEquals(s.getQueuedClip(1), 7, "Prev wraps back");
            f.slotCommit(s, 1);
            s.perform(act(ControlActionType::ClipPrev));
            expectEquals(s.getQueuedClip(1), 5);
            expect(s.perform(act(ControlActionType::ClipNext)), "back onto the playing clip (7) while 5 is queued");
            expectEquals(s.getQueuedClip(1), -1, "cancels the wait instead of muting");
            expectEquals(s.getPlayingClip(1), 7);
            expectEquals(f.slotCancels, 1);
            s.perform(act(ControlActionType::ClipPrev)); s.perform(act(ControlActionType::ClipPrev));
            expectEquals(s.getQueuedClip(1), 2, "two presses from the playing clip, stepping from the queued one");
            expectEquals(s.getPlayingClip(1), 7);
            expectEquals(f.mutes, 0, "stepping never mutes");
        }

        beginTest("ClipNext and ClipPrev with no other stored clip refuse");
        {
            Fake f; f.stopped = true; LiveState s; ready(f, s);
            s.setClipMask(0, 0);
            expect(! s.perform(act(ControlActionType::ClipNext)), "nothing stored");
            s.setClipMask(0, 0b100);
            s.perform(launch(0, 2));
            expect(! s.perform(act(ControlActionType::ClipNext)));
            expect(! s.perform(act(ControlActionType::ClipPrev)));
            expectEquals(f.mutes, 0);
        }

        beginTest("a restored session forgets the clip state; masks and notifications");
        {
            Fake f; f.stopped = true; LiveState s; ready(f, s);
            s.setClipMask(0, 0b1);
            s.perform(launch(0, 0));
            CountingListener l; s.addListener(&l);
            s.setClipMask(0, 0b1);
            expectEquals(l.calls, 0, "an unchanged mask is no change");
            s.setClipMask(0, 0b11);
            expectEquals(l.calls, 1);
            juce::ValueTree state("MuTantState");
            s.saveTo(state);
            s.restoreFrom(state);
            expectEquals(s.getPlayingClip(0), -1);
            expectEquals((int) s.getClipMask(0), 3, "the stored clips are the bank's, not the session's");
            s.removeListener(&l);
        }

        beginTest("without the owner's clip callbacks the actions are refused and the getters are safe");
        {
            LiveState s;
            expectEquals(s.getPlayingClip(-1), -1);
            expectEquals(s.getPlayingClip(99), -1);
            expectEquals(s.getQueuedClip(99), -1);
            expectEquals((int) s.getClipMask(99), 0);
            expect(! s.isLayerMuted(0));
            expect(LiveState::handles(ControlActionType::LaunchClip) && LiveState::handles(ControlActionType::ClipNext)
                   && LiveState::handles(ControlActionType::ClipPrev));
            expect(actionFromName(actionName(ControlActionType::ClipNext)) == ControlActionType::ClipNext);
            expect(actionFromName(actionName(ControlActionType::ClipPrev)) == ControlActionType::ClipPrev);
        }
    }
};

static LiveStateTests liveStateTests;
