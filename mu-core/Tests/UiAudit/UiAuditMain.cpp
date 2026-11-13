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
#include "UI/StatusSink.h"
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
    // Runs the message loop (timers, async callbacks) for `ms` milliseconds.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
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

    bool verbose = juce::SystemStats::getEnvironmentVariable("UIAUDIT_VERBOSE", {}).isNotEmpty();
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
    void auditButton(juce::Button& b);
    void probe(juce::Component& c);
    void sweepCustom(juce::Component& c);
    int unnamedButtons = 0, switchesClicked = 0;

    // The controls not yet audited, in the tree now.
    void sweep(bool stepThroughChoices)
    {
        std::vector<KnobWithLabel*> knobs;   collect(*root, knobs);
        std::vector<NudgeInput*> nudges;     collect(*root, nudges);
        std::vector<DropdownSelect*> boxes;  collect(*root, boxes);
        for (auto* k : knobs)   if (done.insert(k).second && visibleChain(k, root) && ! deniedAncestor(k)) auditKnob(*k);
        for (auto* n : nudges)  if (done.insert(n).second && visibleChain(n, root) && ! deniedAncestor(n)) auditNudge(*n);
        for (auto* d : boxes)   if (done.insert(d).second && visibleChain(d, root) && ! deniedAncestor(d)) auditDropdown(*d, stepThroughChoices);
        std::vector<juce::Button*> buttons;  collect(*root, buttons);
        for (auto* b : buttons) if (done.insert(b).second && visibleChain(b, root) && b->isEnabled()) auditButton(*b);
        sweepCustom(*root);
    }
};

// A switch (or any button): it must have a name for the status bar, and a toggle or radio choice is clicked and must report.
// A plain action button is only checked for its name (clicking it could open a dialog or add something).
void Audit::auditButton(juce::Button& b)
{
    // A button inside another audited control (a dropdown's arrow, a knob's reset) belongs to that control.
    for (auto* p = b.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<KnobWithLabel*>(p) || dynamic_cast<DropdownSelect*>(p) || dynamic_cast<NudgeInput*>(p)) return;

    if (mu_ui::statusNameOf(b).isEmpty())
    {
        ++unnamedButtons;
        missing.push_back({ "button (no name)", b.getButtonText().isNotEmpty() ? b.getButtonText() : b.getName(), where(&b) });
        return;
    }
    if (! b.getClickingTogglesState() && b.getRadioGroupId() == 0) return;

    // The shell binds buttons on its refresh timer: the pump before the sweep has given this one its reporter.
    juce::Button* previouslyOn = nullptr;
    if (b.getRadioGroupId() != 0 && ! b.getToggleState())
        if (auto* parent = b.getParentComponent())
            for (auto* sibling : parent->getChildren())
                if (auto* other = dynamic_cast<juce::Button*>(sibling))
                    if (other != &b && other->getRadioGroupId() == b.getRadioGroupId() && other->getToggleState()) previouslyOn = other;

    clearBar();
    b.triggerClick();   // asynchronous: the button goes down, up, then clicks
    pump(220);
    ++checked;
    ++switchesClicked;
    if (verbose) std::printf("  switch [%s] bound=%d toggles=%d radio=%d\n", b.getButtonText().toRawUTF8(), (int) b.getProperties().contains(mu_ui::kStatusBoundProperty), (int) b.getClickingTogglesState(), b.getRadioGroupId());
    if (silent()) missing.push_back({ "switch", mu_ui::statusNameOf(b), where(&b) });

    if (previouslyOn != nullptr) { previouslyOn->triggerClick(); pump(220); }   // put a radio choice back
    else if (b.getRadioGroupId() == 0) { b.triggerClick(); pump(220); }          // put a toggle back
}

// A custom control: a visible leaf component that takes mouse clicks and is not one of the standard controls audited above.
bool isCustomCandidate(juce::Component& c, const juce::Component* root)
{
    bool onThis = false, onChildren = false;
    c.getInterceptsMouseClicks(onThis, onChildren);
    // A custom control that holds child components (a step row with its own cells over a bar) is probed on its own surface.
    static const char* composites[] = { "GatingDesigner", "StepRows", "TrackStepRow" };
    bool composite = false;
    for (auto* n : composites) composite = composite || typeName(c).contains(n);
    if ((c.getNumChildComponents() != 0 && ! composite) || ! onThis || c.getWidth() < 6 || c.getHeight() < 6) return false;
    if (! visibleChain(&c, root) || deniedAncestor(&c)) return false;
    if (c.getProperties().contains(mu_ui::kStatusExemptProperty)) return false;
    if (dynamic_cast<juce::Button*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::ComboBox*>(&c)
        || dynamic_cast<juce::Label*>(&c) || dynamic_cast<juce::TextEditor*>(&c) || dynamic_cast<StatusBar*>(&c)) return false;
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<KnobWithLabel*>(p) || dynamic_cast<DropdownSelect*>(p) || dynamic_cast<NudgeInput*>(p) || dynamic_cast<StatusBar*>(p)
            || dynamic_cast<juce::Slider*>(p) || dynamic_cast<juce::ComboBox*>(p) || dynamic_cast<juce::TextEditor*>(p))
            return false;
    return true;
}

// Presses, drags and releases the mouse on a custom control at a few points of its own surface; the status bar must say something.
void Audit::probe(juce::Component& c)
{
    const auto bounds = c.getLocalBounds().toFloat();
    static const juce::Point<float> at[] = { { 0.5f, 0.5f }, { 0.2f, 0.3f }, { 0.8f, 0.3f }, { 0.3f, 0.75f }, { 0.75f, 0.75f },
                                                { 0.5f, 0.08f }, { 0.25f, 0.15f }, { 0.8f, 0.12f }, { 0.12f, 0.5f }, { 0.9f, 0.9f } };   // the top rows, a left label strip, a corner
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier), none;
    bool tried = false;

    for (auto f : at)
    {
        const auto pos = juce::Point<float>(bounds.getX() + bounds.getWidth() * f.x, bounds.getY() + bounds.getHeight() * f.y);
        if (c.getNumChildComponents() > 0 && c.getComponentAt(pos.toInt()) != &c) continue;   // a child is there: it is audited itself
        tried = true;
        if (verbose) std::printf("  probe %s at %d,%d of %dx%d\n", typeName(c).toRawUTF8(), (int) pos.x, (int) pos.y, c.getWidth(), c.getHeight());
        clearBar();
        const auto moved = pos + juce::Point<float>(6.0f, 0.0f);
        c.mouseDown(juce::MouseEvent(source, pos, left, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, false));
        c.mouseDrag(juce::MouseEvent(source, moved, left, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, true));
        c.mouseUp(juce::MouseEvent(source, moved, none, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, true));
        pump(30);
        if (! silent()) { ++checked; return; }
    }
    if (tried) { ++checked; missing.push_back({ "custom control", typeName(c), where(&c) }); }
}

void Audit::sweepCustom(juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (isCustomCandidate(*child, root) && done.insert(child).second) probe(*child);
        sweepCustom(*child);
    }
}

// A pick in the popup: the item is selected and the combo's own change handler runs (this JUCE delivers a requested notification
// later, on the message loop, so it is called here directly: the same handler, at a known moment).
void pick(juce::ComboBox& combo, int id)
{
    combo.setSelectedId(id, juce::dontSendNotification);
    if (combo.onChange) combo.onChange();
}

void Audit::auditDropdown(DropdownSelect& d, bool stepThroughChoices)
{
    // A multi-select drop-down (its items are tick boxes, not combo items): tick the first item and put it back.
    if (d.isMultiSelect())
    {
        clearBar();
        d.toggle(1);
        ++checked;
        if (silent()) missing.push_back({ "multi-select dropdown", d.getText(), where(&d) });
        d.toggle(1);
        return;
    }
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
    pump(150);
    sweep(false);
    if (stepThroughChoices)
        for (size_t i = 1; i < ids.size(); ++i)
        {
            pick(combo, ids[i]);
            pump(150);
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
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());   // the way a host asks
    if (editor == nullptr) { std::fprintf(stderr, "%s: createEditor returned null\n", product.toRawUTF8()); failure = 3; return 0; }
    pump(700);   // long enough for the shell's refresh timer to give every button its status reporter

    // UIAUDIT_SHOT=<folder>: a PNG of the real editor as it stands (a look at the layout without opening the app).
    if (const auto shotDir = juce::SystemStats::getEnvironmentVariable("UIAUDIT_SHOT", {}); shotDir.isNotEmpty())
    {
        juce::File dir(shotDir);
        dir.createDirectory();
        const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        auto file = dir.getChildFile(product + (tag.isNotEmpty() ? "-" + tag : juce::String()) + ".png");
        file.deleteFile();
        if (auto out = file.createOutputStream()) juce::PNGImageFormat().writeImageToStream(img, *out);
    }

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
