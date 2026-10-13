// The Performance chooser (docs/design-ui-family.md 12.5): the model's rules (add, replace, reorder, remove,
// missing, save, rename, delete, use), the screen's layout, and its wiring in ShellChrome (opening, the
// pick round trip through the Preset Browser, Use, and the entry points). No display, device or processor.

#include "TestShellBackend.h"
#include "Control/LiveState.h"
#include "UI/ModalDialog.h"
#include "UI/PerformanceChooser.h"
#include "UI/PerformanceChooserModel.h"
#include "UI/ShellChrome.h"

namespace
{
using mu_ui::perf::PerformanceChooserModel;
using Status = mu_pp::PerformanceFile::ItemStatus;

struct ScaleGuard
{
    float saved = mu_ui::scale;
    explicit ScaleGuard(float v = 1.0f) { mu_ui::scale = v; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

struct Host : juce::Component
{
    mu_ui::ShellChrome* chrome = nullptr;
    void resized() override { if (chrome != nullptr) chrome->resized(); }
};

struct Panel : juce::Component {};

// Performs the live-state actions, as the processor does.
struct Sink : mu_core::ControlSink
{
    explicit Sink(mu_core::LiveState& s) : state(s) {}
    mu_core::LiveState& state;
    bool perform(const mu_core::ControlAction& a) override { return mu_core::LiveState::handles(a.type) && state.perform(a); }
};

// A scratch tree: presets/ with a few preset files, content/Performances/ for the lists.
struct Folders
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-chooser-test");
    juce::File presets = root.getChildFile("Presets"), content = root.getChildFile("content");
    Folders()
    {
        root.deleteRecursively();
        presets.createDirectory();
        content.createDirectory();
        for (auto n : { "Alpha", "Bravo", "Charlie", "Delta" })
            presets.getChildFile(juce::String(n) + ".muTest").replaceWithText("<Preset/>");
    }
    ~Folders() { root.deleteRecursively(); }
    juce::File preset(const juce::String& n) const { return presets.getChildFile(n + ".muTest"); }
    juce::File performances() const { return mu_ui::perf::performancesDir(content); }
};

PerformanceChooserModel makeModel(const Folders& f)
{
    PerformanceChooserModel m;
    m.setContext(f.performances(), f.presets, "MuTantState");
    m.startNew();
    return m;
}

juce::TextButton* findButton(juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            if (b->getButtonText() == text) return b;
    return nullptr;
}

// Answers the newest ModalDialog in `host` with the button labelled `label`.
bool answerDialog(juce::Component& host, const juce::String& label)
{
    for (int i = host.getNumChildComponents(); --i >= 0;)
        if (auto* dlg = dynamic_cast<mu_ui::ModalDialog*>(host.getChildComponent(i)))
            for (auto* c : dlg->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*>(c))
                    if (b->getButtonText() == label)
                    {
                        b->onClick();
                        delete dlg;   // it queued its own delete; there is no message loop here
                        return true;
                    }
    return false;
}

int dialogCount(juce::Component& host)
{
    int n = 0;
    for (auto* c : host.getChildren())
        if (dynamic_cast<mu_ui::ModalDialog*>(c) != nullptr) ++n;
    return n;
}
} // namespace

struct PerformanceChooserTests : juce::UnitTest
{
    PerformanceChooserTests() : juce::UnitTest("Performance chooser (model, layout, shell wiring)", "mu-core") {}

    void modelItems()
    {
        beginTest("model: adding fills a row with this product's relative pointer and marks the list changed");
        {
            Folders f; auto m = makeModel(f);
            expect(! m.isDirty() && m.working().count() == 0, "starts empty and clean");
            expect(m.setItem(0, f.preset("Alpha")), "added");
            expect(m.isUsed(0) && ! m.isUsed(1), "row 0 only");
            expectEquals(m.itemName(0), juce::String("Alpha"));
            expectEquals(m.working().items[0].product, juce::String("MuTantState"));
            expectEquals(m.working().items[0].path, juce::String("Alpha.muTest"));
            expect(m.isDirty(), "changed");
            expect(! m.setItem(PerformanceChooserModel::kItems, f.preset("Alpha")), "past the last row is refused");
        }

        beginTest("model: replacing a row points it at the new preset");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha"));
            m.setItem(0, f.preset("Bravo"));
            expectEquals(m.itemName(0), juce::String("Bravo"));
            expectEquals(m.working().count(), 1);
        }

        beginTest("model: up and down swap neighbours and stop at the ends");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha")); m.setItem(1, f.preset("Bravo")); m.setItem(2, f.preset("Charlie"));
            expect(! m.canMoveUp(0), "the first cannot go up");
            expect(! m.canMoveDown(2), "the last used cannot go down into the empty row");
            expect(! m.canMoveUp(3) && ! m.canMoveDown(3), "an empty row does not move");
            expect(m.moveDown(0), "down");
            expectEquals(m.itemName(0) + "," + m.itemName(1) + "," + m.itemName(2), juce::String("Bravo,Alpha,Charlie"));
            expect(m.moveUp(2), "up");
            expectEquals(m.itemName(0) + "," + m.itemName(1) + "," + m.itemName(2), juce::String("Bravo,Charlie,Alpha"));
            expect(! m.moveUp(0) && ! m.moveDown(2) && ! m.moveDown(3), "refused moves change nothing");
            expectEquals(m.working().count(), 3);
        }

        beginTest("model: removing closes the gap so the used rows stay together");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha")); m.setItem(1, f.preset("Bravo")); m.setItem(2, f.preset("Charlie"));
            expect(m.removeItem(1), "removed");
            expectEquals(m.itemName(0) + "," + m.itemName(1), juce::String("Alpha,Charlie"));
            expect(! m.isUsed(2), "the tail row is empty");
            expect(! m.removeItem(2), "an empty row cannot be removed");
            expect(m.removeItem(0) && m.removeItem(0), "remove the rest");
            expectEquals(m.working().count(), 0);
        }

        beginTest("model: a gone file shows as missing, another product's as foreign, both stay in the list");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha"));
            m.setItem(1, f.preset("Bravo"));
            f.preset("Bravo").deleteFile();
            mu_pp::PerformanceFile list = m.working();
            list.setItem(2, "MuClidState", f.preset("Charlie"), f.presets, "Charlie");
            m.adopt(list, juce::File());
            expect(m.status(0) == Status::Ok, "ok");
            expect(m.status(1) == Status::Missing, "missing");
            expect(m.status(2) == Status::OtherProduct, "other product");
            expect(m.status(3) == Status::Empty, "empty");
            expectEquals(m.productLabel(0), juce::String("MuTant"));
            expectEquals(m.productLabel(1), juce::String("missing"));
            expectEquals(m.productLabel(2), juce::String("MuClid"));
            expectEquals(m.productLabel(3), juce::String());
            expect(m.setItem(1, f.preset("Delta")) && m.status(1) == Status::Ok, "a missing row can be replaced");
            expect(m.removeItem(2), "or a foreign one removed");
        }
    }

    void modelFiles()
    {
        beginTest("model: saving writes the file, lists it, and clears the changed flag");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha"));
            expect(! m.save("  ", "x"), "an empty name is refused");
            expect(m.save("Friday", "Opening set"), "saved");
            expect(f.performances().getChildFile("Friday.muPerformance").existsAsFile(), "file written");
            expect(! m.isDirty() && m.hasFile(), "clean, and it has a file");
            expectEquals(m.working().name, juce::String("Friday"));
            expectEquals(m.working().description, juce::String("Opening set"));
            expectEquals((int) m.library().size(), 1);
            expectEquals(m.libraryRowOfWorking(), 0);

            PerformanceChooserModel other = makeModel(f);
            other.rescan();
            expect(other.open(m.workingFile()).isEmpty(), "reads back");
            expectEquals(other.itemName(0), juce::String("Alpha"));
        }

        beginTest("model: renaming moves the file; a name already taken is reported");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha"));
            m.save("Friday", "");
            const auto old = m.workingFile();
            expect(m.save("Saturday", "new name"), "renamed");
            expect(! old.existsAsFile() && m.workingFile().existsAsFile(), "old file gone, new one there");
            expectEquals((int) m.library().size(), 1);

            PerformanceChooserModel second = makeModel(f);
            second.setItem(0, f.preset("Bravo"));
            second.rescan();
            expect(second.nameTaken("Saturday"), "taken by another performance");
            expect(! second.nameTaken("Sunday"), "free");
            expect(! m.nameTaken("Saturday"), "its own name is not taken");
        }

        beginTest("model: the library is sorted by name and lists a file that is not a performance");
        {
            Folders f; auto m = makeModel(f);
            f.performances().createDirectory();
            f.performances().getChildFile("zeta.muPerformance").replaceWithText("junk");
            f.performances().getChildFile("Beta.muPerformance").replaceWithText("junk");
            f.performances().getChildFile("alpha.muPerformance").replaceWithText("junk");
            f.performances().getChildFile("notes.txt").replaceWithText("x");
            m.rescan();
            expectEquals((int) m.library().size(), 3);
            expectEquals(m.library()[0].name + "," + m.library()[1].name + "," + m.library()[2].name, juce::String("alpha,Beta,zeta"));
        }

        beginTest("model: deleting the working file starts a new list");
        {
            Folders f; auto m = makeModel(f);
            m.setItem(0, f.preset("Alpha"));
            m.save("Friday", "");
            const auto file = m.workingFile();
            expect(m.deleteFile(file), "deleted");
            expect(! file.existsAsFile() && ! m.hasFile() && m.working().count() == 0, "now new and empty");
            expectEquals((int) m.library().size(), 0);
        }
    }

    void modelUse()
    {
        beginTest("model: use hands over a saved, unchanged list only");
        {
            Folders f; auto m = makeModel(f);
            int calls = 0;
            juce::File gotFile; int gotCount = 0;
            const auto loader = [&](mu_pp::PerformanceFile list, const juce::File& where)
            { ++calls; gotFile = where; gotCount = list.count(); return true; };

            expect(! m.canUse() && ! m.use(loader), "empty: nothing to use");
            m.setItem(0, f.preset("Alpha"));
            expect(m.canUse(), "something to play");
            expect(! m.use(loader), "unsaved: refused");
            m.save("Friday", "");
            m.setItem(1, f.preset("Bravo"));
            expect(! m.use(loader), "changed since saving: refused");
            expectEquals(calls, 0);
            m.save("Friday", "");
            expect(m.use(loader), "saved: handed over");
            expect(calls == 1 && gotFile == m.workingFile() && gotCount == 2, "with its file and both items");
            expect(! m.use([](mu_pp::PerformanceFile, const juce::File&) { return false; }), "the loader's refusal comes back");
            expect(! m.use({}), "no loader, no use");
        }
    }

    void layout()
    {
        beginTest("layout: header, eight rows and footer fit the overlay area at both display sizes");
        for (float scale : { 1.0f, 1.25f })
        {
            ScaleGuard g(scale);
            const juce::Rectangle<int> area(0, 0, mu_ui::s(1088), mu_ui::s(774));   // the worst-case overlay area
            const auto lay = mu_ui::PerformanceChooser::computeLayout(area);
            expectEquals(lay.list.getWidth(), mu_ui::s(MuLookAndFeel::kPerfListW));
            expectEquals(lay.rows[0].getHeight(), mu_ui::s(MuLookAndFeel::kPerfItemRowH));
            expect(lay.rows[0].getY() == lay.header.getBottom(), "rows start under the header");
            expect(lay.rows[7].getBottom() <= lay.footer.getY(), "rows end above the footer");
            expect(area.contains(lay.rows[7]) && area.contains(lay.header) && area.contains(lay.footer) && area.contains(lay.list), "inside");
            expect(lay.rows[0].getX() > lay.list.getRight(), "rows are right of the list");
            expect(lay.list.getBottom() == lay.footer.getY(), "the list stops at the footer");
        }

        beginTest("layout: a row has room for its name between the number and the product");
        {
            ScaleGuard g;
            Folders f;
            mu_ui::PerformanceChooser chooser;
            chooser.setBounds(0, 0, 1088, 774);
            chooser.beginSession(f.performances(), f.presets, "MuTantState", {}, juce::File());
            chooser.getModel().setItem(0, f.preset("Alpha"));
            chooser.refresh();
            expect(chooser.getModel().isUsed(0), "a used row");
            // The used row shows its four actions, an empty one only "+ Preset".
            const auto lay = mu_ui::PerformanceChooser::computeLayout(chooser.getLocalBounds());
            expect(lay.rows[0].getWidth() > mu_ui::s(MuLookAndFeel::kPerfNumW + MuLookAndFeel::kPerfProductW + 4 * MuLookAndFeel::kPerfRowBtnW) + 100,
                   "wide enough for a readable name");
        }
    }

    // A chrome over the test backend with a live state, as the standalone app has.
    struct Rig
    {
        Folders folders;
        mu_core::LiveState live;
        Sink sink { live };
        TestShellBackend b { true };
        Host host;
        Panel sidebar, mainPanel;
        std::unique_ptr<mu_ui::ShellChrome> chrome;
        juce::StringArray status;

        Rig()
        {
            mu_core::LiveState::Callbacks cb;
            cb.supportsPerformanceMode = [] { return true; };
            cb.canLoad    = [](int) { return true; };
            cb.stageItem  = [](int) { return mu_core::LiveState::Stage::Applied; };
            cb.cancelItem = [] {};
            cb.layerCount = [] { return 4; };
            cb.fullPending = [] { return false; };
            cb.stageSerial = [] { return (uint32_t) 0; };
            live.setCallbacks(cb);

            b.live = &live; b.sink = &sink; b.performanceSupported = true; b.loadTarget = &live;
            b.presetsDir = folders.presets; b.contentDir = folders.content;
            chrome = std::make_unique<mu_ui::ShellChrome>(host, b);
            host.chrome = chrome.get();
            chrome->setMainArea(&sidebar, &mainPanel);
            chrome->applyWindowSize();
            host.setVisible(true);
            chrome->getPerformanceChooser().onStatus = [this](const juce::String& n, const juce::String& v) { status.add(n + "|" + v); };
        }
        ~Rig() { host.chrome = nullptr; chrome.reset(); }

        mu_ui::PerformanceChooser& chooser() { return chrome->getPerformanceChooser(); }
    };

    void shellWiring()
    {
        beginTest("shell: the Performances button opens the chooser as a page over the main panel");
        {
            ScaleGuard g;
            Rig r;
            auto& browser = r.chrome->getPresetBrowser();
            auto* btn = findButton(browser, "Performances");
            expect(btn != nullptr && btn->isVisible(), "the footer button is there where the mode is supported");
            expect(! r.chrome->isPerformanceChooserVisible(), "closed to begin with");
            r.chrome->showPresetBrowser(true);
            btn->onClick();
            expect(r.chrome->isPerformanceChooserVisible() && r.chooser().isVisible(), "chooser shown");
            expect(! browser.isVisible() && ! r.mainPanel.isVisible(), "browser and main panel make way");
            expect(r.chooser().getBounds() == r.mainPanel.getBounds(), "it fills the main area");
            r.chooser().actionClose();
            expect(! r.chooser().isVisible() && r.mainPanel.isVisible(), "Close returns to the main panel");
        }

        beginTest("shell: the footer button is absent where Performance mode is not supported");
        {
            ScaleGuard g;
            Folders f;
            TestShellBackend plain(true);
            plain.presetsDir = f.presets; plain.contentDir = f.content;
            Host host;
            mu_ui::ShellChrome chrome(host, plain);
            host.chrome = &chrome;
            expect(! findButton(chrome.getPresetBrowser(), "Performances")->isVisible(), "hidden");
            chrome.showPerformanceChooser();
            expect(! chrome.isPerformanceChooserVisible(), "and the chooser will not open");
            host.chrome = nullptr;
        }

        beginTest("shell: the Perf toggle and Ctrl+P with none chosen open the chooser instead of a status line");
        {
            ScaleGuard g;
            Rig r;
            juce::StringArray lines;
            r.chrome->getTransportBar().onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            expect(! r.chrome->getTransportBar().enterPerformanceMode(), "does not enter");
            expect(r.chrome->isPerformanceChooserVisible(), "the chooser is up");
            expect(lines.isEmpty() && ! r.live.isPerformanceMode(), "no status line, mode still off");
            r.chrome->showPerformanceChooser();
            expect(r.chrome->isPerformanceChooserVisible(), "asking again keeps it open");
        }

        beginTest("shell: the chooser starts from the list in use, and is not reachable in the mode");
        {
            ScaleGuard g;
            Rig r;
            mu_pp::PerformanceFile p;
            p.name = "Gig";
            p.setItem(0, "MuTantState", r.folders.preset("Alpha"), r.folders.presets, "Alpha");
            p.setItem(1, "MuTantState", r.folders.preset("Bravo"), r.folders.presets, "Bravo");
            const auto file = r.folders.performances().getChildFile("Gig.muPerformance");
            r.folders.performances().createDirectory();
            p.save(file);
            r.live.loadPerformance(p, file);

            r.chrome->showPerformanceChooser();
            expectEquals(r.chooser().getModel().working().count(), 2);
            expect(r.chooser().getModel().workingFile() == file && ! r.chooser().getModel().isDirty(), "the list in use, unchanged");
            expectEquals((int) r.chooser().getModel().library().size(), 1);

            r.chooser().actionClose();
            mu_core::ControlAction on; on.type = mu_core::ControlActionType::PerformanceMode; on.index = 1;
            r.live.perform(on);
            r.chrome->showPerformanceChooser();
            expect(! r.chrome->isPerformanceChooserVisible(), "refused while the mode is on");
        }

        beginTest("shell: entering Performance mode closes an open chooser");
        {
            ScaleGuard g;
            Rig r;
            mu_pp::PerformanceFile p;
            p.setItem(0, "MuTantState", r.folders.preset("Alpha"), r.folders.presets, "Alpha");
            r.live.loadPerformance(p, r.folders.performances().getChildFile("x.muPerformance"));
            r.chrome->showPerformanceChooser();
            expect(r.chrome->isPerformanceChooserVisible(), "open");
            mu_core::ControlAction on; on.type = mu_core::ControlActionType::PerformanceMode; on.index = 1;
            r.live.perform(on);
            expect(! r.chrome->isPerformanceChooserVisible() && r.mainPanel.isVisible(), "closed, main panel back");
        }
    }

    void pickRoundTrip()
    {
        beginTest("pick: + Preset opens the browser in pick mode; picking fills the row and returns to the chooser");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& browser = r.chrome->getPresetBrowser();
            r.chooser().actionPick(0);
            expect(browser.isVisible() && browser.isPicking(), "browser up, in pick mode");
            expect(! r.chooser().isVisible(), "chooser steps aside");
            expect(findButton(browser, "Pick") != nullptr && ! findButton(browser, "Performances")->isVisible(), "Load reads Pick; no Performances button");

            browser.chooseRow(1);   // listed by name: Alpha, Bravo, Charlie, Delta
            expect(! browser.isVisible() && ! browser.isPicking(), "browser closed, pick mode over");
            expect(r.chooser().isVisible(), "back in the chooser");
            expect(r.chooser().getModel().isUsed(0) && r.chooser().getModel().itemName(0) == "Bravo", "row 0 is Bravo");
            expect(r.chooser().getModel().isDirty(), "the list is changed");
            expect(r.b.log.isEmpty(), "the preset was not loaded");
        }

        beginTest("pick: closing the browser cancels the pick but still returns to the chooser, edits kept");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            r.chooser().getModel().setItem(0, r.folders.preset("Alpha"));
            r.chooser().refresh();
            auto& browser = r.chrome->getPresetBrowser();
            r.chooser().actionPick(1);
            findButton(browser, "Close")->onClick();
            expect(r.chooser().isVisible() && ! browser.isVisible() && ! browser.isPicking(), "chooser back");
            expect(r.chooser().getModel().isUsed(0) && ! r.chooser().getModel().isUsed(1), "nothing added, earlier edit kept");
        }

        beginTest("pick: a browser opened normally afterwards loads again and does not return to the chooser");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            r.chooser().actionPick(0);
            r.chrome->hideAllOverlays();   // the pick is abandoned some other way
            expect(! r.chrome->getPresetBrowser().isPicking(), "pick mode ended with the browser");
            r.chrome->showPresetBrowser(true);
            r.chrome->getPresetBrowser().chooseRow(0);
            expectEquals(r.b.log.joinIntoString("|"), juce::String("load:Alpha.muTest"));
            expect(! r.chooser().isVisible(), "chooser stays closed");
        }
    }

    void screenActions()
    {
        beginTest("actions: Use loads the saved list without switching preset, then closes the chooser");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& m = r.chooser().getModel();
            m.setItem(0, r.folders.preset("Alpha"));
            m.setItem(1, r.folders.preset("Bravo"));
            m.save("Friday", "");
            r.chooser().refresh();
            r.chooser().actionUse();
            expect(r.live.getPerformance().count() == 2 && r.live.getPerformanceFile() == m.workingFile(), "the live state has the list");
            expect(r.live.getCurrentItem() == -1, "no item chosen yet");
            expect(r.b.log.isEmpty(), "no preset was loaded");
            expect(! r.chooser().isVisible(), "chooser closed");
            expect(r.status.contains("Performance|Friday loaded"), "told so");
        }

        beginTest("actions: Use on an unsaved list asks for a name, saves, then uses it");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            r.chooser().getModel().setItem(0, r.folders.preset("Alpha"));
            r.chooser().refresh();
            r.chooser().actionUse();
            expect(r.chooser().getSaveDialog().isVisible(), "the Save card is up");
            expect(r.live.getPerformance().count() == 0, "nothing used yet");
            r.chooser().getSaveDialog().onSave("Opening", "first set", "", false);
            expect(! r.chooser().getSaveDialog().isVisible(), "card closed");
            expect(r.folders.performances().getChildFile("Opening.muPerformance").existsAsFile(), "saved");
            expectEquals(r.live.getPerformance().name, juce::String("Opening"));
            expect(! r.chooser().isVisible(), "and used");
        }

        beginTest("actions: Use on a list changed since saving saves it first");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& m = r.chooser().getModel();
            m.setItem(0, r.folders.preset("Alpha"));
            m.save("Friday", "");
            m.setItem(1, r.folders.preset("Bravo"));
            r.chooser().refresh();
            r.chooser().actionUse();
            expectEquals(r.live.getPerformance().count(), 2);
            expect(! m.isDirty(), "saved on the way");
        }

        beginTest("actions: Use with nothing in the list says so and does nothing");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            r.chooser().actionUse();
            expect(r.chooser().isVisible() && r.live.getPerformance().count() == 0, "still open, nothing used");
            expect(r.status.contains("Performance|Add at least one preset first"), "told why");
        }

        beginTest("actions: a changed list asks before it is thrown away (New, Close, opening another)");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& m = r.chooser().getModel();
            m.setItem(0, r.folders.preset("Alpha"));
            m.save("One", "");
            m.setItem(1, r.folders.preset("Bravo"));
            r.chooser().refresh();

            r.chooser().actionNew();
            expectEquals(dialogCount(r.host), 1);
            expect(m.working().count() == 2, "nothing happens until answered");
            expect(answerDialog(r.host, "Cancel"), "cancelled");
            expect(m.working().count() == 2 && m.isDirty(), "list kept");
            r.chooser().actionNew();
            expect(answerDialog(r.host, "Discard"), "discarded");
            expect(m.working().count() == 0 && ! m.hasFile(), "new, empty list");

            m.setItem(0, r.folders.preset("Charlie"));
            r.chooser().refresh();
            r.chooser().actionOpenSaved(0);
            expect(answerDialog(r.host, "Discard"), "discarded to open One");
            expectEquals(m.itemName(0), juce::String("Alpha"));

            m.setItem(1, r.folders.preset("Delta"));
            r.chooser().actionClose();
            expect(r.chooser().isVisible(), "Close waits for the answer");
            expect(answerDialog(r.host, "Discard"), "discarded");
            expect(! r.chooser().isVisible(), "closed");
            expectEquals(dialogCount(r.host), 0);
        }

        beginTest("actions: Delete asks first, then removes the file");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& m = r.chooser().getModel();
            m.setItem(0, r.folders.preset("Alpha"));
            m.save("Gone", "");
            const auto file = m.workingFile();
            r.chooser().refresh();
            r.chooser().actionDelete();
            expect(file.existsAsFile(), "kept until answered");
            expect(answerDialog(r.host, "Delete"), "confirmed");
            expect(! file.existsAsFile() && ! m.hasFile(), "deleted, list is new again");
        }

        beginTest("actions: saving over another performance's name asks to overwrite");
        {
            ScaleGuard g;
            Rig r;
            r.chrome->showPerformanceChooser();
            auto& m = r.chooser().getModel();
            m.setItem(0, r.folders.preset("Alpha"));
            m.save("First", "");
            r.chooser().actionNew();
            m.setItem(0, r.folders.preset("Bravo"));
            r.chooser().refresh();
            r.chooser().actionSave();
            expect(r.chooser().getSaveDialog().isVisible(), "Save on a new list asks for a name");
            r.chooser().getSaveDialog().onSave("First", "", "", false);
            expectEquals(dialogCount(r.host), 1);
            expect(answerDialog(r.host, "Overwrite"), "confirmed");
            expectEquals(m.itemName(0), juce::String("Bravo"));
            expectEquals((int) m.library().size(), 1);
        }
    }

    void runTest() override
    {
        modelItems();
        modelFiles();
        modelUse();
        layout();
        shellWiring();
        pickRoundTrip();
        screenActions();
    }
};

static PerformanceChooserTests performanceChooserTests;
