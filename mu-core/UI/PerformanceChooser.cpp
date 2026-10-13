#include "PerformanceChooser.h"
#include "UI/ConfirmDialog.h"

namespace mu_ui
{

using Id = MuLookAndFeel::ColourIds;

// ─── Row ─────────────────────────────────────────────────────────────────────

PerformanceChooser::Row::Row(PerformanceChooser& owner, int idx) : index(idx), chooser(owner)
{
    for (auto* b : { &addBtn, &replaceBtn, &upBtn, &downBtn, &removeBtn })
        addChildComponent(*b);

    addBtn    .onClick = [this] { chooser.actionPick(index); };
    replaceBtn.onClick = [this] { chooser.actionPick(index); };
    addBtn    .setTooltip("Pick a preset for this item");
    replaceBtn.setTooltip("Pick a different preset for this item");
    upBtn     .setTooltip("Move this item up");
    downBtn   .setTooltip("Move this item down");
    removeBtn .setTooltip("Remove this item");
    upBtn     .onClick = [this] { chooser.model.moveUp(index);     chooser.refresh(); };
    downBtn   .onClick = [this] { chooser.model.moveDown(index);   chooser.refresh(); };
    removeBtn .onClick = [this] { chooser.model.removeItem(index); chooser.refresh(); };
}

// Shows the buttons that apply: "+ Preset" on an empty row, the four row actions on a used one.
void PerformanceChooser::Row::update()
{
    const auto& m = chooser.model;
    const bool used = m.isUsed(index);
    addBtn.setVisible(! used);
    for (auto* b : { &replaceBtn, &upBtn, &downBtn, &removeBtn })
        b->setVisible(used);
    upBtn.setEnabled(m.canMoveUp(index));
    downBtn.setEnabled(m.canMoveDown(index));
    resized();
    repaint();
}

void PerformanceChooser::Row::resized()
{
    using mu_ui::s;
    const int gap  = s(MuLookAndFeel::kPanelPad);
    const int btnW = s(MuLookAndFeel::kPerfRowBtnW);
    const int btnH = getHeight() - gap * 2;

    // Buttons from the right edge inwards: Remove, Down, Up, Replace.
    int x = getWidth();
    for (auto* b : { &removeBtn, &downBtn, &upBtn, &replaceBtn })
    {
        x -= btnW;
        b->setBounds(x, gap, btnW, btnH);
        x -= gap;
    }
    productRect = { x - s(MuLookAndFeel::kPerfProductW) + gap, 0, s(MuLookAndFeel::kPerfProductW) - gap, getHeight() };
    lcdRect     = { s(MuLookAndFeel::kPerfNumW), gap, juce::jmax(0, productRect.getX() - gap - s(MuLookAndFeel::kPerfNumW)), btnH };
    addBtn.setBounds(lcdRect);
}

void PerformanceChooser::Row::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    using S = mu_pp::PerformanceFile::ItemStatus;
    const auto& m     = chooser.model;
    const auto status = m.status(index);
    const auto& L     = MuLookAndFeel::lighting();
    const auto accent = MuLookAndFeel::appAccent(*this);
    const bool bad    = status == S::Missing || status == S::OtherProduct;

    // The number lamp: dark when empty, a dim accent lens when playable, a dim red one when missing or foreign.
    const float d = sf((float) MuLookAndFeel::kStatusLampD);
    const juce::Rectangle<float> disc((float) s(MuLookAndFeel::kPanelPad), (float) getHeight() * 0.5f - d * 0.5f, d, d);
    g.setColour(MuLookAndFeel::lampBase());
    g.fillEllipse(disc);
    if (status != S::Empty)
    {
        juce::Path lens;
        lens.addEllipse(disc.reduced(1.0f));
        const auto clr = bad ? MuLookAndFeel::colour(Id::indicatorFault) : accent;
        MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(clr, L.lampDim),
                                disc.getCentre(), d * L.lampHotSpotReach);
    }

    const auto font = MuLookAndFeel::lcdFont(sf((float) MuLookAndFeel::kLcdTextH));
    g.setFont(font);
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.drawText(juce::String(index + 1), (int) disc.getRight() + s(MuLookAndFeel::kPanelPad), 0,
               s(MuLookAndFeel::kPerfNumW) - (int) disc.getRight() - s(MuLookAndFeel::kPanelPad), getHeight(),
               juce::Justification::centredLeft, false);

    if (status == S::Empty) return;   // the "+ Preset" button fills the name's place

    // The name LCD and the product / status label.
    MuLookAndFeel::drawLcdGlass(g, lcdRect.toFloat(), accent, false);
    g.setFont(font);
    g.setColour(bad ? MuLookAndFeel::colour(Id::indicatorFault) : MuLookAndFeel::lcdLitColour(*this));
    g.drawText(m.itemName(index), lcdRect.reduced(s(MuLookAndFeel::kLcdTextPadX), 0), juce::Justification::centredLeft, true);
    MuLookAndFeel::drawLcdFront(g, lcdRect.toFloat());

    g.setColour(bad ? MuLookAndFeel::colour(Id::indicatorFault) : MuLookAndFeel::colour(Id::mutedText));
    g.drawText(status == S::OtherProduct ? m.productLabel(index) + " (other product)" : m.productLabel(index),
               productRect, juce::Justification::centredLeft, true);
}

// ─── PerformanceChooser ──────────────────────────────────────────────────────

PerformanceChooser::PerformanceChooser()
{
    listBox.setModel(this);
    listBox.setRowHeight(mu_ui::s(MuLookAndFeel::kPerfListRowH));
    listBox.setColour(juce::ListBox::backgroundColourId, MuLookAndFeel::colour(Id::panelBackground));
    addAndMakeVisible(listBox);

    for (int i = 0; i < perf::PerformanceChooserModel::kItems; ++i)
    {
        rows[(size_t) i] = std::make_unique<Row>(*this, i);
        addAndMakeVisible(*rows[(size_t) i]);
    }

    for (auto* b : { &newBtn, &saveBtn, &renameBtn, &deleteBtn, &closeBtn, &useBtn })
        addAndMakeVisible(*b);
    newBtn   .onClick = [this] { actionNew(); };
    saveBtn  .onClick = [this] { actionSave(); };
    renameBtn.onClick = [this] { actionRename(); };
    deleteBtn.onClick = [this] { actionDelete(); };
    closeBtn .onClick = [this] { actionClose(); };
    useBtn   .onClick = [this] { actionUse(); };
    useBtn   .setTooltip("Load this list for Performance mode");
    renameBtn.setTooltip("Rename this performance (save or discard edits first)");

    // The Save card names or renames a performance: name and description only.
    saveDialog.setNameAndDescriptionOnly(true, "Performance name");
    saveDialog.onCancel = [this] { saveDialog.setVisible(false); useAfterSave = false; };
    saveDialog.onSave   = [this](const juce::String& name, const juce::String& desc, const juce::String&, bool)
    {
        if (name.isEmpty()) return;
        if (! model.nameTaken(name)) { commitSave(name, desc); return; }
        juce::Component::SafePointer<PerformanceChooser> safe(this);
        confirmAsync(this, "Overwrite Performance", "Overwrite \"" + name + "\"?", "Overwrite",
                     [safe, name, desc] { if (safe != nullptr) safe->commitSave(name, desc); });
    };
    addChildComponent(saveDialog);
}

PerformanceChooser::~PerformanceChooser() = default;

void PerformanceChooser::beginSession(const juce::File& performancesFolder, const juce::File& presetsFolder,
                                      const juce::String& ownProduct, const mu_pp::PerformanceFile& inUse,
                                      const juce::File& inUseFile)
{
    model.setContext(performancesFolder, presetsFolder, ownProduct);
    model.rescan();
    if (inUse.count() > 0 || inUseFile != juce::File()) model.adopt(inUse, inUseFile);
    else                                                 model.startNew();
    saveDialog.setVisible(false);
    useAfterSave = false;
    refresh();
}

void PerformanceChooser::refresh()
{
    for (auto& r : rows) r->update();
    listBox.updateContent();
    const int row = model.libraryRowOfWorking();
    if (row >= 0) listBox.selectRow(row, true, true);
    else          listBox.deselectAllRows();
    listBox.repaint();

    saveBtn  .setEnabled(model.isDirty() || ! model.hasFile());
    renameBtn.setEnabled(model.hasFile() && ! model.isDirty());   // rename only renames: edits are saved with Save
    deleteBtn.setEnabled(model.hasFile());
    useBtn   .setEnabled(model.canUse());
    useBtn   .setTooltip(model.canUse() ? juce::String("Load this list for Performance mode")
                                        : juce::String("Add at least one preset first"));
    repaint();
}

void PerformanceChooser::say(const juce::String& name, const juce::String& value) const
{
    if (onStatus) onStatus(name, value);
}

juce::String PerformanceChooser::currentName() const
{
    const auto& w = model.working();
    if (w.name.isNotEmpty()) return w.name;
    return model.hasFile() ? model.workingFile().getFileNameWithoutExtension() : juce::String();
}

juce::String PerformanceChooser::titleText() const
{
    auto t = currentName().isNotEmpty() ? currentName() : juce::String("New performance");
    if (model.isDirty()) t << " (unsaved)";
    return t;
}

// ─── Actions ─────────────────────────────────────────────────────────────────

// Runs `proceed` at once, or after the user agrees to drop the unsaved edits.
void PerformanceChooser::guardDirty(std::function<void()> proceed)
{
    if (! model.isDirty()) { proceed(); return; }
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    confirmAsync(this, "Unsaved Changes", "Discard unsaved changes?", "Discard",
                 [safe, proceed = std::move(proceed)] { if (safe != nullptr) proceed(); });
}

void PerformanceChooser::actionNew()
{
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    guardDirty([safe] { if (safe != nullptr) { safe->model.startNew(); safe->refresh(); } });
}

void PerformanceChooser::actionOpenSaved(int libraryRow)
{
    if (libraryRow < 0 || libraryRow >= (int) model.library().size()) return;
    const auto file = model.library()[(size_t) libraryRow].file;
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    guardDirty([safe, file]
    {
        if (safe == nullptr) return;
        const auto problem = safe->model.open(file);
        if (problem.isNotEmpty())
            safe->say("Performance", "could not open " + file.getFileName() + ": " + problem);
        safe->refresh();
    });
}

void PerformanceChooser::openSaveDialog()
{
    const bool renaming = model.hasFile();
    saveDialog.setTitle(renaming ? "Rename Performance" : "Save Performance");
    saveDialog.setDefaultName(currentName());
    saveDialog.setDefaultDescription(model.working().description);
    saveDialog.setVisible(true);
    saveDialog.toFront(true);
}

void PerformanceChooser::commitSave(const juce::String& name, const juce::String& description)
{
    saveDialog.setVisible(false);
    const bool thenUse = useAfterSave;
    if (! model.save(name, description))
    {
        useAfterSave = false;
        say("Performance", "could not save " + name);
        return;
    }
    if (! thenUse) say("Performance", name + " saved");
    refresh();
    if (thenUse) { useAfterSave = false; putIntoUse(true); }
}

void PerformanceChooser::actionSave()
{
    if (! model.hasFile()) { openSaveDialog(); return; }
    commitSave(currentName(), model.working().description);
}

void PerformanceChooser::actionRename()
{
    if (model.hasFile()) openSaveDialog();
}

void PerformanceChooser::actionDelete()
{
    if (! model.hasFile()) return;
    const auto file = model.workingFile();
    const auto name = file.getFileNameWithoutExtension();
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    confirmAsync(this, "Delete Performance", "Delete \"" + name + "\"? This cannot be undone.", "Delete",
                 [safe, file, name]
                 {
                     if (safe == nullptr) return;
                     if (safe->model.deleteFile(file)) safe->say("Performance", name + " deleted");
                     else                              safe->say("Performance", "could not delete " + name);
                     safe->refresh();
                 });
}

void PerformanceChooser::actionUse()
{
    if (! model.canUse()) { say("Performance", "Add at least one preset first"); return; }
    if (! model.hasFile()) { useAfterSave = true; openSaveDialog(); return; }   // name it, then use it
    if (model.isDirty())   { useAfterSave = true; commitSave(currentName(), model.working().description); return; }
    putIntoUse();
}

// Loads the saved list into the live state. It does not switch preset; the first item is chosen on the surface.
void PerformanceChooser::putIntoUse(bool justSaved)
{
    if (model.use(onUse))
    {
        say("Performance", currentName() + (justSaved ? " saved and loaded" : " loaded"));
        if (onClose) onClose();
    }
    else
        say("Performance", "could not use it: leave Performance mode first");
}

void PerformanceChooser::actionClose()
{
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    guardDirty([safe] { if (safe != nullptr && safe->onClose) safe->onClose(); });
}

void PerformanceChooser::actionPick(int row)
{
    if (! onPickPreset) return;
    juce::Component::SafePointer<PerformanceChooser> safe(this);
    onPickPreset([safe, row](const juce::File& preset)
    {
        if (safe == nullptr) return;
        safe->model.setItem(row, preset);
        safe->refresh();
    });
}

// ─── ListBoxModel ────────────────────────────────────────────────────────────

int PerformanceChooser::getNumRows() { return (int) model.library().size(); }

void PerformanceChooser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    using mu_ui::s;
    using mu_ui::sf;
    if (row >= (int) model.library().size()) return;

    if (selected)
    {
        g.setColour(MuLookAndFeel::colour(Id::segmentActiveBg));
        g.fillRect(0, 0, w, h);
    }
    g.setColour(MuLookAndFeel::colour(selected ? Id::headingText : Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf((float) MuLookAndFeel::kLcdTextH))));
    g.drawText(model.library()[(size_t) row].name, s(MuLookAndFeel::kPanelPad), 0, w - s(MuLookAndFeel::kPanelPad) * 2, h,
               juce::Justification::centredLeft, true);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) (h - 1), (float) w, (float) (h - 1), 0.5f);
}

void PerformanceChooser::listBoxItemClicked(int row, const juce::MouseEvent&) { actionOpenSaved(row); }

// ─── Layout and paint ────────────────────────────────────────────────────────

PerformanceChooser::Layout PerformanceChooser::computeLayout(juce::Rectangle<int> bounds)
{
    using mu_ui::s;
    Layout out;
    const int pad     = s(MuLookAndFeel::kPanelPad);
    const int footerH = s(MuLookAndFeel::kPerfFooterH);
    const int headerH = s(MuLookAndFeel::kPerfHeaderH);
    const int rowH    = s(MuLookAndFeel::kPerfItemRowH);
    const int listW   = s(MuLookAndFeel::kPerfListW);

    out.footer = { bounds.getX(), bounds.getBottom() - footerH, bounds.getWidth(), footerH };
    out.list   = { bounds.getX(), bounds.getY(), listW, bounds.getHeight() - footerH };
    const int rightX = bounds.getX() + listW + pad;
    const int rightW = bounds.getWidth() - listW - pad * 2;
    out.header = { rightX, bounds.getY(), rightW, headerH };
    for (int i = 0; i < perf::PerformanceChooserModel::kItems; ++i)
        out.rows[(size_t) i] = { rightX, bounds.getY() + headerH + i * rowH, rightW, rowH };
    return out;
}

void PerformanceChooser::resized()
{
    using mu_ui::s;
    const auto lay  = computeLayout(getLocalBounds());
    const int pad   = s(MuLookAndFeel::kPanelPad);
    const int btnW  = s(MuLookAndFeel::kPerfRowBtnW);

    listBox.setBounds(lay.list);
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds(lay.rows[i]);

    // Header buttons from the right: Delete, Rename, Save, New.
    int x = lay.header.getRight();
    for (auto* b : { &deleteBtn, &renameBtn, &saveBtn, &newBtn })
    {
        x -= btnW;
        b->setBounds(x, lay.header.getY() + pad, btnW, lay.header.getHeight() - pad * 2);
        x -= pad;
    }

    closeBtn.setBounds(lay.footer.getX() + pad, lay.footer.getY() + pad, btnW, lay.footer.getHeight() - pad * 2);
    useBtn  .setBounds(lay.footer.getRight() - pad - btnW, lay.footer.getY() + pad, btnW, lay.footer.getHeight() - pad * 2);
    saveDialog.setBounds(getLocalBounds());
}

void PerformanceChooser::visibilityChanged()
{
    if (! isVisible())
    {
        saveDialog.setVisible(false);
        useAfterSave = false;
    }
}

void PerformanceChooser::paint(juce::Graphics& g)
{
    using mu_ui::sf;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const auto lay = computeLayout(getLocalBounds());
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float) lay.list.getRight(), 0.0f, (float) lay.list.getRight(), (float) lay.list.getBottom(), 0.5f);
    g.drawLine(0.0f, (float) lay.footer.getY(), (float) getWidth(), (float) lay.footer.getY(), 0.5f);

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf((float) MuLookAndFeel::kLcdNameTextH))));
    g.drawText(titleText(), lay.header.withRight(juce::jmax(lay.header.getX(), newBtn.getX() - mu_ui::s(MuLookAndFeel::kPanelPad))),
               juce::Justification::centredLeft, true);
}

} // namespace mu_ui
