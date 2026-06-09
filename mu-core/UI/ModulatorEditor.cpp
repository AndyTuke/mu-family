#include "ModulatorEditor.h"

//==============================================================================
// Note value lookup table for timing dropdowns (id = index + 1)
namespace {
struct NoteEntry { NoteValue nv; NoteMod mod; const char* label; };
static constexpr NoteEntry kNoteEntries[] = {
    { NoteValue::Whole,        NoteMod::None,    "1"     },
    { NoteValue::Half,         NoteMod::None,    "1/2"   },
    { NoteValue::Quarter,      NoteMod::None,    "1/4"   },
    { NoteValue::Eighth,       NoteMod::None,    "1/8"   },
    { NoteValue::Sixteenth,    NoteMod::None,    "1/16"  },
    { NoteValue::ThirtySecond, NoteMod::None,    "1/32"  },
    { NoteValue::Whole,        NoteMod::Triplet, "1T"    },
    { NoteValue::Half,         NoteMod::Triplet, "1/2T"  },
    { NoteValue::Quarter,      NoteMod::Triplet, "1/4T"  },
    { NoteValue::Eighth,       NoteMod::Triplet, "1/8T"  },
    { NoteValue::Sixteenth,    NoteMod::Triplet, "1/16T" },
    { NoteValue::ThirtySecond, NoteMod::Triplet, "1/32T" },
    { NoteValue::Whole,        NoteMod::Dotted,  "1."    },
    { NoteValue::Half,         NoteMod::Dotted,  "1/2."  },
    { NoteValue::Quarter,      NoteMod::Dotted,  "1/4."  },
    { NoteValue::Eighth,       NoteMod::Dotted,  "1/8."  },
    { NoteValue::Sixteenth,    NoteMod::Dotted,  "1/16." },
    { NoteValue::ThirtySecond, NoteMod::Dotted,  "1/32." },
};
static constexpr int kNoteEntryCount = (int)(sizeof(kNoteEntries) / sizeof(kNoteEntries[0]));

static int noteToId(NoteValue nv, NoteMod mod)
{
    for (int i = 0; i < kNoteEntryCount; ++i)
        if (kNoteEntries[i].nv == nv && kNoteEntries[i].mod == mod)
            return i + 1;
    return 3; // fallback: 1/4
}

static void idToNote(int id, NoteValue& nv, NoteMod& mod)
{
    int i = id - 1;
    if (i >= 0 && i < kNoteEntryCount) { nv = kNoteEntries[i].nv; mod = kNoteEntries[i].mod; }
    else                               { nv = NoteValue::Quarter;  mod = NoteMod::None;       }
}

static void populateNoteDropdown(DropdownSelect& dd)
{
    for (int i = 0; i < kNoteEntryCount; ++i)
        dd.addItem(kNoteEntries[i].label, i + 1);
}
} // namespace

//==============================================================================
ModulatorEditor::AssignmentRow::AssignmentRow(const std::string& assignId, int driveChar,
                                              const ModDestProvider* provider)
    : id(assignId)
{
    if (provider && provider->populate)
        provider->populate(destCombo, driveChar);

    // shared BipolarSliderRow replaces inline depth + curve juce::Slider setup.
    bipolarPair.onDepthChange = [this](float v) { if (onDepthChange) onDepthChange(v); };
    bipolarPair.onCurveChange = [this](float v) { if (onCurveChange) onCurveChange(v); };

    destCombo.onChange = [this, provider](int id_)
    {
        if (!provider || !provider->resolveId || !onDestChange) return;
        const std::string dest = provider->resolveId(id_);
        if (!dest.empty()) onDestChange(dest);
    };
    removeBtn.onClick = [this] { if (onRemove) onRemove(); };

    addAndMakeVisible(destCombo);
    addAndMakeVisible(bipolarPair);
    addAndMakeVisible(removeBtn);
}

void ModulatorEditor::AssignmentRow::resized()
{
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int numW = s(20), removeW = s(22);
    const int pairW = s(BipolarSliderRow::kDepthWidth) + s(2) + s(BipolarSliderRow::kCurveWidth);
    const int destW = w - numW - pairW - removeW - s(8);
    destCombo  .setBounds(numW + s(2),                                 0, destW,  h);
    bipolarPair.setBounds(numW + s(2) + destW + s(2),                  0, pairW,  h);
    removeBtn  .setBounds(w - removeW, (h - s(18)) / 2, removeW, s(18));
}

void ModulatorEditor::AssignmentRow::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
    g.drawText(juce::String(rowNumber) + ".",
               s(2), 0, s(18), getHeight(), juce::Justification::centred, false);
}

//==============================================================================
ModulatorEditor::ModulatorEditor()
{
    modeDropdown.addItem("Smooth",  1);
    modeDropdown.addItem("Stepped", 2);
    addAndMakeVisible(modeDropdown);
    addAndMakeVisible(polarityCtrl);
    addAndMakeVisible(lfoEditor);
    addAndMakeVisible(stepEditor);

    // Loop timing row
    loopLabel.setText("Loop", juce::dontSendNotification);
    loopLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    loopLabel.setJustificationType(juce::Justification::centredRight);
    loopLabel.setColour(juce::Label::textColourId,
                        MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    populateNoteDropdown(loopDropdown);
    loopMult.setShowStepButtons(false);
    loopMult.setLabelInline(true);
    addAndMakeVisible(loopLabel);
    addAndMakeVisible(loopDropdown);
    addAndMakeVisible(loopMult);

    // Step timing row — Stepped mode only
    stepLabel.setText("Step", juce::dontSendNotification);
    stepLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    stepLabel.setJustificationType(juce::Justification::centredRight);
    stepLabel.setColour(juce::Label::textColourId,
                        MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    populateNoteDropdown(stepDropdown);
    stepMult.setShowStepButtons(false);
    stepMult.setLabelInline(true);
    addAndMakeVisible(stepLabel);
    addAndMakeVisible(stepDropdown);
    addAndMakeVisible(stepMult);

    // Dice — randomises the modulator's values (stepValues / curvePoints.y)
    // without changing its mode, polarity, loop / step timing or node count.
    diceBtn.setTooltip("Randomise modulator values");
    diceBtn.onClick = [this] { randomiseValues(); };
    addAndMakeVisible(diceBtn);

    rowsViewport.setViewedComponent(&rowsBox, false);
    rowsViewport.setScrollBarsShown(false, false);
    addAndMakeVisible(rowsViewport);

    rowPageLabel.setJustificationType(juce::Justification::centred);
    rowPageLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    rowPageLabel.setColour(juce::Label::textColourId,
                           MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    addAndMakeVisible(rowPrevBtn);
    addAndMakeVisible(rowNextBtn);
    addAndMakeVisible(rowPageLabel);
    rowPrevBtn.onClick = [this] { scrollRowPage(-1); };
    rowNextBtn.onClick = [this] { scrollRowPage(+1); };

    addAndMakeVisible(addBtn);

    wireHeader();
    wireTiming();
}

void ModulatorEditor::setPlayheadBeat(double beat)
{
    if (!cs) return;
    const double loopBeatsRaw = cs->getLoopLengthBeats();
    const double loopBeats    = loopBeatsRaw > 0.0 ? loopBeatsRaw : 1.0;
    const float  phase        = static_cast<float>(std::fmod(beat / loopBeats, 1.0));
    if (cs->mode == ControlSequence::Mode::Smooth)
        lfoEditor.setPlayheadPhase(phase);
    else
        stepEditor.setPlayheadPhase(phase);
}

void ModulatorEditor::setData(ControlSequence* cs_, ModulationMatrix* matrix_,
                               juce::Colour colour, int index,
                               std::atomic<bool>* lock)
{
    cs             = cs_;
    matrix         = matrix_;
    rhythmModLock  = lock;
    modColour      = colour;
    modIndex       = index;
    stepEditor.setBarColour(modColour);
    // loadFromCS() can `push_back` to cs->curvePoints (seeding default points)
    // and `resize` cs->stepValues — both can race with the audio thread's
    // `cs->evaluate()` if we don't hold modLock during the mutation.
    lockMod();
    loadFromCS();
    unlockMod();
    rebuildRows();
    resized();
    repaint();
}

void ModulatorEditor::setInsertAlgorithm(int driveChar)
{
    currentDriveChar = driveChar;
    rebuildRows();
    resized();
    repaint();
}

void ModulatorEditor::setDestProvider(const ModDestProvider* p)
{
    destProvider = p;
    rebuildRows();
}

void ModulatorEditor::lockMod()
{
    if (!rhythmModLock) return;
    bool expected = false;
    while (!rhythmModLock->compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;
}

void ModulatorEditor::unlockMod()
{
    if (rhythmModLock)
        rhythmModLock->store(false, std::memory_order_release);
}

void ModulatorEditor::randomiseValues()
{
    if (!cs) return;

    // Stepped: stepValues are stored as the matrix-contract -100..+100 (bipolar)
    //          or 0..+100 (unipolar). Walk every entry, replace with a fresh
    //          random in-range, leave the array length untouched.
    // Smooth:  curvePoints.y is stored as the editor-side -1..+1 (bipolar) /
    //          0..+1 (unipolar). Walk every point, replace y; leave x,
    //          hasBezierHandle, handleX, handleY alone so the curve's node
    //          layout + segment types are preserved.
    auto& rng = juce::Random::getSystemRandom();
    const bool bipolar = cs->polarity == ControlSequence::Polarity::Bipolar;

    lockMod();
    if (cs->mode == ControlSequence::Mode::Stepped)
    {
        for (auto& v : cs->stepValues)
            v = bipolar ? rng.nextFloat() * 200.0f - 100.0f
                        : rng.nextFloat() * 100.0f;
    }
    else
    {
        for (auto& p : cs->curvePoints)
            p.y = bipolar ? rng.nextFloat() * 2.0f - 1.0f
                          : rng.nextFloat();
    }
    unlockMod();

    // Re-bind both editors to the mutated CS so they pull fresh data + repaint.
    lfoEditor .setPoints(cs->curvePoints);
    stepEditor.setSteps(cs->stepValues);
    lfoEditor .repaint();
    stepEditor.repaint();
    if (onChange) onChange();
}

void ModulatorEditor::loadFromCS()
{
    if (!cs) return;

    const bool smooth   = (cs->mode == ControlSequence::Mode::Smooth);
    const bool unipolar = (cs->polarity == ControlSequence::Polarity::Unipolar);
    modeDropdown.setSelectedId(smooth ? 1 : 2);
    polarityCtrl.setSelectedIndex(unipolar ? 0 : 1);
    lfoEditor.setUnipolar(unipolar);
    stepEditor.setUnipolar(unipolar);
    lfoEditor.setVisible(smooth);
    stepEditor.setVisible(!smooth);

    if (smooth)
    {
        if (cs->curvePoints.empty())
        {
            cs->curvePoints.push_back({ 0.0f, 0.0f, false, 0.0f, 0.0f });
            cs->curvePoints.push_back({ 1.0f, 0.0f, false, 0.0f, 0.0f });
        }
        lfoEditor.setPoints(cs->curvePoints);
    }
    else
    {
        syncStepValues();
    }

    lfoEditor.setStepFraction((float) cs->getStepFraction());
    loopDropdown.setSelectedId(noteToId(cs->loopNoteValue, cs->loopNoteMod));
    loopMult.setValue(cs->loopMultiplier);
    stepDropdown.setSelectedId(noteToId(cs->stepNoteValue, cs->stepNoteMod));
    stepMult.setValue(cs->stepMultiplier);
}

void ModulatorEditor::syncStepValues()
{
    if (!cs) return;
    const int rawCount = cs->getStepCount();
    // Sanity-bound the count: anything beyond a few thousand indicates a corrupt
    // ControlSequence pointer (e.g. dangling after a Rhythm vector erase).
    if (rawCount < 1 || rawCount > 4096)
    {
        DBG("ModulatorEditor::syncStepValues: garbage step count " << rawCount
            << " -- cs likely dangles. Skipping.");
        jassertfalse;
        return;
    }
    cs->stepValues.resize((size_t)rawCount, 0.0f);
    stepEditor.setSteps(cs->stepValues);
    stepEditor.setStepCount(rawCount);
    stepEditor.setStepFraction((float) cs->getStepFraction());   // tile cells by step width (partial last)
}

void ModulatorEditor::wireHeader()
{
    modeDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        lockMod();
        cs->mode = (id == 1) ? ControlSequence::Mode::Smooth : ControlSequence::Mode::Stepped;
        unlockMod();
        const bool smooth = (cs->mode == ControlSequence::Mode::Smooth);
        lfoEditor.setVisible(smooth);
        stepEditor.setVisible(!smooth);
        if (!smooth) syncStepValues();
        else { lockMod(); loadFromCS(); unlockMod(); }
        updateStepQuantization();
        resized();
        if (onChange) onChange();
    };

    polarityCtrl.onChange = [this](int idx)
    {
        if (!cs) return;
        const bool unipolar = (idx == 0);
        lockMod();
        cs->polarity = unipolar ? ControlSequence::Polarity::Unipolar
                                : ControlSequence::Polarity::Bipolar;
        // Clamp the stored curve anchors into the new visible range so a bipolar→unipolar
        // switch can't leave negative y below the floor (off-range draw + audio overshoot).
        const float yFloor = unipolar ? 0.0f : -1.0f;
        for (auto& p : cs->curvePoints) p.y = juce::jlimit(yFloor, 1.0f, p.y);
        unlockMod();
        lfoEditor.setUnipolar(unipolar);
        stepEditor.setUnipolar(unipolar);
        updateStepQuantization();
        if (onChange) onChange();
    };
}

void ModulatorEditor::wireTiming()
{
    loopDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        NoteValue nv; NoteMod mod;
        idToNote(id, nv, mod);
        lockMod();
        cs->loopNoteValue = nv;
        cs->loopNoteMod   = mod;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        lfoEditor.setStepFraction((float) cs->getStepFraction());
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    loopMult.onChange = [this](int v)
    {
        if (!cs) return;
        lockMod();
        cs->loopMultiplier = v;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        lfoEditor.setStepFraction((float) cs->getStepFraction());
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    stepDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        NoteValue nv; NoteMod mod;
        idToNote(id, nv, mod);
        lockMod();
        cs->stepNoteValue = nv;
        cs->stepNoteMod   = mod;
        syncStepValues();
        lfoEditor.setStepFraction((float) cs->getStepFraction());
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    stepMult.onChange = [this](int v)
    {
        if (!cs) return;
        lockMod();
        cs->stepMultiplier = v;
        syncStepValues();
        lfoEditor.setStepFraction((float) cs->getStepFraction());
        unlockMod();
        repaint();
        if (onChange) onChange();
    };

    lfoEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
    {
        if (!cs) return;
        lockMod();
        cs->curvePoints = pts;
        unlockMod();
        if (onChange) onChange();
    };
    stepEditor.onStepChanged = [this](int idx, float val)
    {
        if (!cs || idx < 0 || idx >= (int)cs->stepValues.size()) return;
        lockMod();
        cs->stepValues[idx] = val;
        unlockMod();
        if (onChange) onChange();
    };

    addBtn.onClick = [this] { addTarget(); };
}

void ModulatorEditor::addTarget()
{
    if (!cs || !matrix) return;
    ModulationAssignment a;
    a.id            = cs->id + "_assign_" + juce::Uuid().toString().toStdString();
    a.sourceId      = cs->id + "_output";
    // Default destination = the dropdown's first item via the provider. Empty
    // string is fine — the row's dropdown will show no selection until the
    // user picks one.
    a.destinationId = (destProvider && destProvider->resolveId)
                          ? destProvider->resolveId(1)
                          : std::string{};
    a.depth         = 0.0f;
    lockMod();
    matrix->addAssignment(a);
    unlockMod();
    rebuildRows();
    resized();
    repaint();
    if (onChange) onChange();
}

void ModulatorEditor::updateRowPager()
{
    const int total = (int)rows.size();
    const int viewH = rowsViewport.getHeight();
    const int rpp   = juce::jmax(1, viewH / (kRowH + 2));
    const bool multi = total > rpp;

    rowPrevBtn  .setVisible(multi);
    rowNextBtn  .setVisible(multi);
    rowPageLabel.setVisible(total > 0);

    if (total == 0) return;

    if (multi)
    {
        const int curTop = viewH > 0 ? (rowsViewport.getViewPositionY() / (kRowH + 2)) : 0;
        const int shown  = juce::jmin(rpp, total - curTop);
        rowPrevBtn.setEnabled(curTop > 0);
        rowNextBtn.setEnabled(curTop + rpp < total);
        rowPageLabel.setText(juce::String(curTop + 1) + " \xe2\x80\x93 " +
                             juce::String(curTop + shown) + " / " + juce::String(total),
                             juce::dontSendNotification);
    }
    else
    {
        rowPageLabel.setText(juce::String(total) + (total == 1 ? " target" : " targets"),
                             juce::dontSendNotification);
    }
}

void ModulatorEditor::scrollRowPage(int delta)
{
    const int curY = rowsViewport.getViewPositionY();
    const int newY = juce::jmax(0, curY + delta * (kRowH + 2));
    rowsViewport.setViewPosition(0, newY);
    updateRowPager();
    repaint();
}

void ModulatorEditor::updateStepQuantization()
{
    // In stepped mode, snap the step editor to 7 discrete levels when any assignment
    // targets pitch.octave (bipolar = -3..+3, i.e. 7 values; unipolar = 0..+3, 4 values).
    if (!cs || cs->mode != ControlSequence::Mode::Stepped)
    {
        stepEditor.setQuantization(0);
        return;
    }
    const bool bipolar = (cs->polarity == ControlSequence::Polarity::Bipolar);
    // mu-clid special case: when any assignment targets pitch.octave, snap the
    // step editor to 7 (bipolar = ±3 oct, with 0) or 4 (unipolar = 0..3 oct).
    // Generalised via the destination provider's resolveId so the rule only
    // fires for products that actually expose "pitch.octave" (mu-tant doesn't).
    if (destProvider && destProvider->resolveId)
    {
        for (const auto& row : rows)
        {
            const int id = row->destCombo.getSelectedId();
            if (id < 1) continue;
            if (destProvider->resolveId(id) == "pitch.octave")
            {
                stepEditor.setQuantization(bipolar ? 7 : 4);
                return;
            }
        }
    }
    stepEditor.setQuantization(0);
}

void ModulatorEditor::rebuildRows()
{
    for (auto& row : rows) rowsBox.removeChildComponent(row.get());
    rows.clear();
    rowsViewport.setViewPosition(0, 0);
    if (!cs || !matrix) return;

    const juce::String sourceKey = cs->id + "_output";
    for (const auto& a : matrix->getAssignments())
    {
        if (a.sourceId != sourceKey.toStdString()) continue;

        auto row = std::make_unique<AssignmentRow>(a.id, currentDriveChar, destProvider);
        row->rowNumber = (int)rows.size() + 1;
        rowsBox.addAndMakeVisible(*row);

        if (destProvider && destProvider->findDropdownId)
        {
            const int ddId = destProvider->findDropdownId(a.destinationId);
            if (ddId > 0) row->destCombo.setSelectedId(ddId);
        }

        row->bipolarPair.setDepth(a.depth, juce::dontSendNotification);
        row->bipolarPair.setCurve(a.curve, juce::dontSendNotification);

        const std::string rowId = a.id;
        row->onRemove = [this, rowId]
        {
            if (!matrix) return;
            lockMod();
            matrix->removeAssignment(rowId);
            unlockMod();
            if (onChange) onChange();
            juce::Component::SafePointer<ModulatorEditor> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (auto* p = safe.getComponent())
                    { p->rebuildRows(); p->resized(); p->repaint(); }
            });
        };
        row->onDestChange = [this, rowId](const std::string& dest)
        {
            if (!matrix) return;
            float d = 0.0f;
            float c = 0.0f;
            lockMod();
            for (const auto& a2 : matrix->getAssignments())
                if (a2.id == rowId) { d = a2.depth; c = a2.curve; break; }
            matrix->removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = cs->id + "_output";
            na.destinationId = dest;
            na.depth         = d;
            na.curve         = c;   // preserve curve through dest change
            matrix->addAssignment(na);
            unlockMod();
            updateStepQuantization();
            if (onChange) onChange();
        };
        row->onDepthChange = [this, rowId](float d)
        {
            if (!matrix) return;
            lockMod();
            matrix->setDepth(rowId, d);
            unlockMod();
            if (onChange) onChange();
        };
        row->onCurveChange = [this, rowId](float c)
        {
            if (!matrix) return;
            lockMod();
            matrix->setCurve(rowId, c);
            unlockMod();
            if (onChange) onChange();
        };

        rows.push_back(std::move(row));
    }
    updateStepQuantization();
}

void ModulatorEditor::resized()
{
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int headerH = s(kHeaderH);
    const int editorH = s(kEditorH);
    const int rowH    = s(kRowH);
    const int pagerH  = s(kPagerH);
    const int addBtnH = s(kAddBtnH);

    // ── Single header row: [● Mod X painted] [mode] [polarity] [Loop dd mult] [Step dd mult] ──
    const int nameW  = s(68);
    const int modeW  = s(78);
    const int polW   = s(44);
    const int lbW    = s(30);   // "Loop" / "Step" label
    const int ddW    = s(58);   // note-value dropdown (fits "1/32T")
    const int nmW    = s(46);   // nudge "× N"
    const int gap2   = s(2);
    const int gap4   = s(4);
    const int gap8   = s(8);

    int x = nameW;
    modeDropdown.setBounds(x, 0, modeW, headerH); x += modeW + gap4;
    polarityCtrl.setBounds(x, 0, polW,  headerH); x += polW + gap8;
    loopLabel   .setBounds(x, 0, lbW,   headerH); x += lbW + gap2;
    loopDropdown.setBounds(x, 0, ddW,   headerH); x += ddW + gap2;
    loopMult    .setBounds(x, 0, nmW,   headerH); x += nmW + gap8;

    // Dice button — anchored top-right of the header row, square (headerH × headerH).
    const int diceW = headerH;
    const int diceX = w - diceW;
    diceBtn.setBounds(diceX, 0, diceW, headerH);

    // Step group (Stepped mode only) flows from the left, immediately after the
    // Loop group — left-justified with the other controls. Only the dice is
    // right-anchored; the step group sits well clear of it.
    if (stepDropdown.isVisible())
    {
        stepLabel   .setBounds(x, 0, lbW, headerH); x += lbW + gap2;
        stepDropdown.setBounds(x, 0, ddW, headerH); x += ddW + gap2;
        stepMult    .setBounds(x, 0, nmW, headerH); x += nmW + gap8;
    }
    // The "N steps" readout (drawn in paint, Stepped mode) is left-justified here,
    // immediately after the step group's × multiplier.
    stepReadoutX = x;

    // ── LFO / Step editor ──────────────────────────────────────────────────────
    lfoEditor .setBounds(0, headerH, w, editorH);
    stepEditor.setBounds(0, headerH, w, editorH);

    // ── Assignment rows viewport ───────────────────────────────────────────────
    const int editorBottom = headerH + editorH + gap4;
    const int viewH = juce::jmax(0, h - editorBottom - pagerH - addBtnH - gap4);
    rowsViewport.setBounds(0, editorBottom, w, viewH);

    const int contentH = juce::jmax(viewH, (int)rows.size() * (rowH + gap2));
    rowsBox.setSize(w, contentH);
    int ry = 0;
    for (auto& row : rows) { row->setBounds(0, ry, w, rowH); ry += rowH + gap2; }

    // ── Pager row ─────────────────────────────────────────────────────────────
    const int pagerY = editorBottom + viewH + gap2;
    const int btnW   = s(20);
    rowPrevBtn  .setBounds(0,          pagerY, btnW, pagerH);
    rowPageLabel.setBounds(btnW + gap2,   pagerY, w - btnW * 2 - gap4, pagerH);
    rowNextBtn  .setBounds(w - btnW,   pagerY, btnW, pagerH);

    // ── Add button ─────────────────────────────────────────────────────────────
    addBtn.setBounds(0, h - addBtnH, w, addBtnH);

    updateRowPager();
}

void ModulatorEditor::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillAll();

    const int headerH = s(kHeaderH);
    const float dotSize = sf(8.0f);

    // Header: colour dot + modulator name
    const float dotY = (headerH - dotSize) * 0.5f;
    g.setColour(modColour);
    g.fillEllipse(sf(8.0f), dotY, dotSize, dotSize);
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    g.drawText("Mod " + juce::String::charToString(char('A' + modIndex)),
               s(20), 0, s(54), headerH, juce::Justification::centredLeft, false);

    // Step count drawn left-justified immediately after the step group's ×
    // multiplier (Stepped mode only). Bounded on the right by the dice (right-
    // anchored at getWidth() - headerH, opaque fill) so it never underlaps it.
    if (cs && cs->mode == ControlSequence::Mode::Stepped)
    {
        const int diceW     = headerH;       // dice is a headerH × headerH square
        const int rightEdge = getWidth() - diceW - s(4);   // small gap before the dice
        const int textW     = juce::jmax(0, rightEdge - stepReadoutX);
        if (textW > 0)
        {
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
            g.drawText(juce::String(cs->getStepCount()) + " steps",
                       stepReadoutX, 0, textW, headerH,
                       juce::Justification::centredLeft, false);
        }
    }
}
