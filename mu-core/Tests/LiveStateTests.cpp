// mu_core::LiveState: the live state behind Performance mode, driven with fake callbacks (no processor,
// no hot-swap machinery). Guards the action rules in docs/design-plugin-family.md "Performance mode and live
// state" section 5: the mode gate, item switching and cancelling, wrapping Prev / Next, SelectLayer bounds,
// the commit hook, change notification and the host-session persistence.

#include "Control/LiveState.h"

namespace
{
using namespace mu_core;

struct Fake
{
    bool supported = true;
    bool stopped   = false;               // a stopped transport applies a staged item at once
    bool loadable[8] = { true, true, true, true, false, false, true, false };
    int  layers = 4;
    std::vector<int> staged;
    int  cancels = 0;

    LiveState::Callbacks callbacks()
    {
        LiveState::Callbacks cb;
        cb.supportsPerformanceMode = [this] { return supported; };
        cb.canLoad    = [this](int i) { return i >= 0 && i < 8 && loadable[i]; };
        cb.stageItem  = [this](int i) { staged.push_back(i); return stopped; };
        cb.cancelItem = [this] { ++cancels; };
        cb.layerCount = [this] { return layers; };
        return cb;
    }
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

ControlAction act(ControlActionType t, int layer = 0, int index = -1, float value = 1.0f)
{
    ControlAction a;
    a.type = t; a.layer = layer; a.index = index; a.value = value;
    return a;
}
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
            expect(! s.perform(act(ControlActionType::PerformanceMode)), "no performance loaded: refused");
            expect(! s.isPerformanceMode());
            expect(s.loadPerformance(makeList(4), file));
            f.supported = false;
            expect(! s.perform(act(ControlActionType::PerformanceMode)), "unsupported product: refused");
            f.supported = true;
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 1)), "mode off: item refused");
            expect(! s.perform(act(ControlActionType::SelectLayer, 1)), "mode off: select refused");
            expect(! s.perform(act(ControlActionType::PerformanceNext)), "mode off: next refused");
            expect(s.perform(act(ControlActionType::PerformanceMode, 2)), "on");
            expect(s.isPerformanceMode());
            expectEquals(s.getSelectedLayer(), 2, "entering seeds the selected layer");
            expect(s.perform(act(ControlActionType::PerformanceMode, 0, -1, 0.0f)), "off");
            expect(! s.isPerformanceMode());
            expect(s.perform(act(ControlActionType::PerformanceMode, 0, -1, -1.0f)), "a negative value toggles on");
            expect(s.isPerformanceMode());
            expect(s.perform(act(ControlActionType::PerformanceMode, 0, -1, -1.0f)), "and off");
            expect(! s.isPerformanceMode());
        }

        beginTest("an item stages and queues; the commit makes it current; a loaded item's own press is a no-op");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(act(ControlActionType::PerformanceMode));
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)));
            expectEquals(s.getQueuedItem(), 2);
            expectEquals(s.getCurrentItem(), -1, "current moves only at the commit");
            expectEquals((int) f.staged.size(), 1);
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)), "the queued item again: already on its way");
            expectEquals((int) f.staged.size(), 1, "and it is not staged twice");
            s.fullPresetCommitted();
            expectEquals(s.getCurrentItem(), 2);
            expectEquals(s.getQueuedItem(), -1);
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 2)), "the playing item with nothing queued");
            expectEquals((int) f.staged.size(), 1, "nothing staged");
            s.fullPresetCommitted();   // a preset that was not ours
            expectEquals(s.getCurrentItem(), 2, "a foreign commit changes nothing");
        }

        beginTest("pressing the playing item while another is queued cancels the switch");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(act(ControlActionType::PerformanceMode));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1)); s.fullPresetCommitted();
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
            s.perform(act(ControlActionType::PerformanceMode));
            expect(s.perform(act(ControlActionType::PerformanceItem, 0, 3)));
            expectEquals(s.getCurrentItem(), 3);
            expectEquals(s.getQueuedItem(), -1);
        }

        beginTest("an item that cannot load, or an index out of range, is refused and stages nothing");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(8), file);
            s.perform(act(ControlActionType::PerformanceMode));
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 4)), "item 4 is missing");
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, 8)), "past the end");
            expect(! s.perform(act(ControlActionType::PerformanceItem, 0, -1)), "no index");
            expectEquals((int) f.staged.size(), 0);
        }

        beginTest("Prev and Next wrap, skip unusable items and step from the queued item");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());   // loadable: 0 1 2 3 and 6
            s.loadPerformance(makeList(8), file);
            s.perform(act(ControlActionType::PerformanceMode));
            expect(s.perform(act(ControlActionType::PerformanceNext)));
            expectEquals(s.getQueuedItem(), 0, "from nothing, Next lands on the first loadable item");
            s.fullPresetCommitted();
            s.perform(act(ControlActionType::PerformanceNext)); s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 2, "two presses before the commit land two items on");
            s.fullPresetCommitted();
            s.perform(act(ControlActionType::PerformanceNext)); s.fullPresetCommitted();   // 3
            s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 6, "4 and 5 are missing: Next skips to 6");
            s.fullPresetCommitted();
            s.perform(act(ControlActionType::PerformanceNext));
            expectEquals(s.getQueuedItem(), 0, "and wraps past 7 (also missing) to 0");
            s.fullPresetCommitted();
            s.perform(act(ControlActionType::PerformancePrev));
            expectEquals(s.getQueuedItem(), 6, "Prev wraps back");
        }

        beginTest("Prev and Next with no other loadable item refuse");
        {
            Fake f; for (bool& b : f.loadable) b = false; f.loadable[2] = true;
            LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(8), file);
            s.perform(act(ControlActionType::PerformanceMode));
            s.perform(act(ControlActionType::PerformanceItem, 0, 2)); s.fullPresetCommitted();
            expect(! s.perform(act(ControlActionType::PerformanceNext)));
            expect(! s.perform(act(ControlActionType::PerformancePrev)));
        }

        beginTest("SelectLayer is absolute, bounded by the live layer count, and bumps the revision only on a change");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(2), file);
            s.perform(act(ControlActionType::PerformanceMode));
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

        beginTest("leaving the mode drops a queued item and cancels its staged preset; a list cannot be swapped while it is on");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            s.loadPerformance(makeList(4), file);
            s.perform(act(ControlActionType::PerformanceMode));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));
            expect(! s.loadPerformance(makeList(2), file), "refused while the mode is on");
            s.perform(act(ControlActionType::PerformanceMode, 0, -1, 0.0f));
            expectEquals(s.getQueuedItem(), -1);
            expectEquals(f.cancels, 1);
        }

        beginTest("listeners hear every change on the message thread");
        {
            Fake f; LiveState s; s.setCallbacks(f.callbacks());
            CountingListener l; s.addListener(&l);
            s.loadPerformance(makeList(4), file);
            s.perform(act(ControlActionType::PerformanceMode));
            s.perform(act(ControlActionType::PerformanceItem, 0, 1));
            s.fullPresetCommitted();
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
            s.perform(act(ControlActionType::PerformanceMode, 2));
            s.perform(act(ControlActionType::PerformanceItem, 0, 3)); s.fullPresetCommitted();
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
    }
};

static LiveStateTests liveStateTests;
