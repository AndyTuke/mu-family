// <product>-ui-audit: the status-bar hard rule, checked on the product's REAL editor.
//
// "The status bar shows the name and value of the last control touched, in every app, without fail." This console app builds the
// product's real processor and editor (createPluginFilter / createEditor, as a host would), then changes every knob, every nudge
// box and every dropdown it can find, one at a time, and checks the status bar said something after each. A dropdown that has other
// choices (an engine, a mode) is stepped through them, and the controls each choice brings are audited too. Anything that changed
// a control and left the bar empty is listed. Exit 0 = every control reported, 1 = some did not.
//
// Test-only: EXCLUDE_FROM_ALL, never part of a release. Built by mu_add_ui_audit (cmake/UiAudit.cmake).

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/StatusBar.h"
#include "UI/LayerContent.h"
#include "Plugin/ProcessorBase.h"
#include "Sequencer/LayerTypeInfo.h"
#include <functional>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <typeinfo>
#include <vector>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

struct Finding { juce::String kind, label, where; };

void pump(int ms)
{
    struct Stopper : juce::Timer { void timerCallback() override { juce::MessageManager::getInstance()->stopDispatchLoop(); } } stopper;
    stopper.startTimer(ms);
    juce::MessageManager::getInstance()->runDispatchLoop();
}

juce::String typeName(const juce::Component& c)
{
    juce::String n(typeid(c).name());
    return n.replace("class ", "").replace("struct ", "");
}

// True when the component and every ancestor is visible (the editor is never on a desktop here, so isShowing() is not usable).
bool visibleChain(const juce::Component* c, const juce::Component* root)
{
    for (; c != nullptr && c != root; c = c->getParentComponent())   // the editor itself is never on a desktop here: stop at it
        if (! c->isVisible()) return false;
    return true;
}

// Preset pickers, file lists, overlays: choosing in them loads or saves something, not a control to audit.
bool deniedAncestor(const juce::Component* c)
{
    for (c = c->getParentComponent(); c != nullptr; c = c->getParentComponent())
    {
        const auto n = typeName(*c);
        if (n.containsIgnoreCase("TransportBar") || n.containsIgnoreCase("PresetHeader") || n.containsIgnoreCase("PresetBrowser")
            || n.containsIgnoreCase("SaveDialog") || n.containsIgnoreCase("Overlay") || n.containsIgnoreCase("ModalDialog")
            || n.containsIgnoreCase("ModulatorPanel") || n.containsIgnoreCase("ModulatorEditor") || n.containsIgnoreCase("ModMatrixPanel")
            || n.containsIgnoreCase("LayerHeader") || n.containsIgnoreCase("ClipStrip") || n.containsIgnoreCase("SceneGrid"))
            return true;
    }
    return false;
}

juce::String where(const juce::Component* c)
{
    juce::StringArray chain;
    for (c = c->getParentComponent(); c != nullptr && chain.size() < 4; c = c->getParentComponent())
        chain.add(typeName(*c));
    return chain.joinIntoString(" < ");
}

struct Audit
{
    StatusBar* bar = nullptr;
    juce::Component* root = nullptr;
    std::set<const juce::Component*> done;
    std::vector<Finding> missing;
    int checked = 0;

    template <class T> void collect(juce::Component& c, std::vector<T*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*>(child)) out.push_back(t);
            collect(*child, out);
        }
    }

    bool verbose = std::getenv("UIAUDIT_VERBOSE") != nullptr;
    bool silent()
    {
        if (verbose) std::printf("    bar: [%s] [%s]\n", bar->getShownName().toRawUTF8(), bar->getShownValue().toRawUTF8());
        return bar->getShownName().isEmpty() && bar->getShownValue().isEmpty();
    }
    void clearBar() { bar->showParam({}, {}); }

    void auditKnob(KnobWithLabel& k)
    {
        auto& s = k.getSlider();
        const double lo = s.getMinimum(), hi = s.getMaximum(), now = k.getValue();
        if (hi <= lo) return;
        // A move of at least one step of the knob's own interval (a whole-number knob would not change on a smaller nudge).
        const double move = std::max(s.getInterval(), (hi - lo) * 0.13);
        double target = now + move;
        if (target > hi) target = now - move;
        if (target < lo || std::abs(target - now) < 1.0e-9) target = (now - lo) < (hi - now) ? hi : lo;
        clearBar();
        k.setValue(target, juce::sendNotificationSync);
        ++checked;
        if (silent()) missing.push_back({ "knob", k.getLabel(), where(&k) });
        k.setValue(now, juce::sendNotificationSync);
    }

    void auditNudge(NudgeInput& n)
    {
        clearBar();
        const int now = n.getValue();
        n.setValue(now + 1, true);
        ++checked;
        if (silent())
        {
            n.setValue(now - 1, true);
            if (silent()) missing.push_back({ "nudge box", {}, where(&n) });
        }
        n.setValue(now, true);
    }

    void auditDropdown(DropdownSelect& d, bool stepThroughChoices);

    // The controls not yet audited, in the tree now.
    void sweep(bool stepThroughChoices)
    {
        std::vector<KnobWithLabel*> knobs;   collect(*root, knobs);
        std::vector<NudgeInput*> nudges;     collect(*root, nudges);
        std::vector<DropdownSelect*> boxes;  collect(*root, boxes);
        for (auto* k : knobs)   if (done.insert(k).second && visibleChain(k, root) && ! deniedAncestor(k)) auditKnob(*k);
        for (auto* n : nudges)  if (done.insert(n).second && visibleChain(n, root) && ! deniedAncestor(n)) auditNudge(*n);
        for (auto* d : boxes)   if (done.insert(d).second && visibleChain(d, root) && ! deniedAncestor(d)) auditDropdown(*d, stepThroughChoices);
    }
};

// A pick in the popup: the item is selected and the combo's own change handler runs (this JUCE delivers a requested notification
// later, on the message loop, so it is called here directly: the same handler, at a known moment).
void pick(juce::ComboBox& combo, int id)
{
    combo.setSelectedId(id, juce::dontSendNotification);
    if (combo.onChange) combo.onChange();
}

void Audit::auditDropdown(DropdownSelect& d, bool stepThroughChoices)
{
    auto& combo = d.getComboBox();
    const int n = combo.getNumItems();
    if (n < 2 || n > 40 || ! combo.isEnabled()) return;
    const int original = combo.getSelectedId();

    std::vector<int> ids;
    for (int i = 0; i < n; ++i) { const int id = combo.getItemId(i); if (id > 0 && id != original) ids.push_back(id); }
    if (ids.empty()) return;

    // One change is audited for the dropdown itself; with stepThroughChoices every other choice is selected in turn and what it
    // brings (an engine's own knobs) is audited while it is selected.
    clearBar();
    if (verbose) std::printf("  dropdown [%s] items=%d original=%d -> %d, combo.onChange %s, DropdownSelect.onChange %s\n", combo.getText().toRawUTF8(), n, original, ids.front(), combo.onChange ? "set" : "NULL", d.onChange ? "set" : "NULL");
    pick(combo, ids.front());
    ++checked;
    if (silent())
        missing.push_back({ "dropdown", combo.getText().isNotEmpty() ? combo.getText() : juce::String("(item ") + juce::String(ids.front()) + ")", where(&d) });
    pump(30);
    sweep(false);
    if (stepThroughChoices)
        for (size_t i = 1; i < ids.size(); ++i)
        {
            pick(combo, ids[i]);
            pump(30);
            sweep(false);
        }
    pick(combo, original);
    pump(30);
}

// Audits one fresh processor + editor. `prepare` may put the processor in a state first (a slot host: an engine in its slot).
// Returns the number of controls changed; findings are appended to `all` (with `tag` in front of the place).
int auditOne(const juce::String& product, const juce::String& tag, const std::function<void(juce::AudioProcessor&)>& prepare,
            std::vector<Finding>& all, int& failure)
{
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    pump(200);
    if (prepare) prepare(*processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());   // the way a host asks
    if (editor == nullptr) { std::fprintf(stderr, "%s: createEditor returned null\n", product.toRawUTF8()); failure = 3; return 0; }
    pump(300);

    Audit audit;
    audit.root = editor.get();
    std::vector<StatusBar*> bars;
    audit.collect(*editor, bars);
    if (bars.empty()) { std::fprintf(stderr, "%s: no status bar found in the editor\n", product.toRawUTF8()); failure = 4; return 0; }
    audit.bar = bars.front();

    if (audit.verbose)
    {
        std::vector<LayerContent*> contents;   audit.collect(*editor, contents);
        for (auto* c : contents) std::printf("  content %s: onStatusUpdate %s\n", typeName(*c).toRawUTF8(), c->onStatusUpdate ? "set" : "NULL");
    }
    audit.sweep(true);
    for (auto f : audit.missing) { if (tag.isNotEmpty()) f.where = tag + ": " + f.where; all.push_back(f); }

    editor.reset();
    processor.reset();
    return audit.checked;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String product = argc > 1 ? argv[1] : "product";

    std::vector<Finding> missing;
    int checked = 0, failure = 0;

#ifdef UIAUDIT_SLOT_HOST
    // A host of engines (mu-Tronic, mu-Host) opens on an empty slot: audit each engine the registry offers, one fresh editor each.
    static const std::pair<const char*, mu_core::LayerTypeId> engines[] = {
        { "Rhythm", mu_core::LayerTypeId::Rhythm }, { "Pattern", mu_core::LayerTypeId::Pattern }, { "Arp", mu_core::LayerTypeId::Arp },
        { "Kick", mu_core::LayerTypeId::Kick }, { "Bass", mu_core::LayerTypeId::Bass }, { "Hat", mu_core::LayerTypeId::Hat },
        { "Snare", mu_core::LayerTypeId::Snare }, { "Rumble", mu_core::LayerTypeId::Rumble }, { "FM Perc", mu_core::LayerTypeId::FmPerc },
        { "Chord", mu_core::LayerTypeId::Chord }, { "Phrase", mu_core::LayerTypeId::Phrase } };
    for (const auto& e : engines)
    {
        checked += auditOne(product, e.first,
                            [&e](juce::AudioProcessor& p) { if (auto* base = dynamic_cast<ProcessorBase*>(&p)) base->setLayerType(0, e.second); },
                            missing, failure);
        if (failure) return failure;
    }
#else
    checked = auditOne(product, {}, nullptr, missing, failure);
    if (failure) return failure;
#endif

    std::printf("%s: %d controls changed, %d left the status bar silent\n", product.toRawUTF8(), checked, (int) missing.size());
    for (const auto& f : missing)
        std::printf("  SILENT %-9s %-24s in %s\n", f.kind.toRawUTF8(), f.label.toRawUTF8(), f.where.toRawUTF8());
    return missing.empty() ? 0 : 1;
}
