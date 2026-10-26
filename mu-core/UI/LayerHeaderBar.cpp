#include "LayerHeaderBar.h"

LayerHeaderBar::LayerHeaderBar()
{
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setEditable(true, true, false);    // single- or double-click to rename (matches mu-clid)
    nameLabel.getProperties().set("muLcd", true); // drawn on the name LCD, not engraved
    nameLabel.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::headingText));
    nameLabel.onTextChange = [this] { if (onNameChanged) onNameChanged(nameLabel.getText()); };
    addAndMakeVisible(nameLabel);

    setNoun(noun);
    resetBtn .onClick = [this] { if (onReset)  onReset();  };
    deleteBtn.onClick = [this] { if (onDelete) onDelete(); };
    saveBtn  .onClick = [this] { if (onSave)   onSave();   };
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(deleteBtn);
    addAndMakeVisible(saveBtn);

    presetDD.setPlaceholderText(juce::String::fromUTF8("preset\xe2\x80\xa6"));
    presetDD.onChange = [this](int id)
    {
        if (onPresetSelected) onPresetSelected(id);
        if (onPresetFileChosen && id >= 1 && id <= presetFiles.size()) onPresetFileChosen(presetFiles[id - 1]);
    };
    addAndMakeVisible(presetDD);
    addChildComponent(clipStrip);   // hidden until a product with a clip bank asks for it

    // Notes In: id = channel + 1 (Omni is 1), the wording of mu-Toni's own Settings row.
    notesInDD.addItem("Omni", 1);
    for (int c = 1; c <= 16; ++c) notesInDD.addItem("Channel " + juce::String(c), c + 1);
    notesInDD.getComboBox().setTooltip("Which MIDI channel this layer plays from. Omni listens to every channel.");
    notesInDD.getComboBox().addMouseListener(this, false);
    notesInDD.onChange = [this](int id)
    {
        // Routing is set before a performance: the shield already swallows the pointer, and a stray pick is put back.
        if (clipStrip.isReadOnly()) { refreshNotesIn(); return; }
        if (notesInSet) notesInSet(id - 1);
        if (onNotesInStatus) onNotesInStatus("Notes In", notesInText(id - 1));
    };
    addChildComponent(notesInDD);   // hidden until a product opts in

    // The preset box names the clip: the queued one in the queued colour, else the playing one.
    clipStrip.onDisplayName = [this](const juce::String& text, bool queued) { presetDD.setDisplayOverride(text, queued); };
}

void LayerHeaderBar::setNoun(const juce::String& n)
{
    noun = n.toLowerCase();
    clipStrip.setNoun(noun);
    resetBtn.setTooltip("Reset this " + noun + " to defaults");
    deleteBtn.setTooltip(deleteBtn.isEnabled() ? "Delete this " + noun : "The last " + noun + " cannot be deleted");
}

juce::String LayerHeaderBar::notesInText(int channel)
{
    return channel >= 1 && channel <= 16 ? "Channel " + juce::String(channel) + " only" : juce::String("Omni (all channels)");
}

void LayerHeaderBar::setNotesInControl(std::function<int()> get, std::function<void(int)> set)
{
    notesInGet = std::move(get);
    notesInSet = std::move(set);
    updateNotesInVisibility();
    refreshNotesIn();
}

void LayerHeaderBar::setShowNotesIn(bool show)
{
    showNotesIn = show;
    updateNotesInVisibility();
}

void LayerHeaderBar::updateNotesInVisibility()
{
    const bool on = showNotesIn && static_cast<bool>(notesInGet);
    if (notesInDD.isVisible() == on) return;
    notesInDD.setVisible(on);
    resized();
    repaint();
}

void LayerHeaderBar::refreshNotesIn()
{
    if (! notesInGet) return;
    const int id = juce::jlimit(0, 16, notesInGet()) + 1;
    if (notesInDD.getSelectedId() != id) notesInDD.setSelectedId(id, false);
}

void LayerHeaderBar::mouseEnter(const juce::MouseEvent& e)
{
    if (e.eventComponent == &notesInDD.getComboBox() && onNotesInStatus && notesInGet)
        onNotesInStatus("Notes In", notesInText(notesInGet()));
}

void LayerHeaderBar::setTypeTag(const juce::String& text)
{
    if (typeTag == text) return;
    typeTag = text;
    resized();
    repaint();
}

int LayerHeaderBar::typeTagWidth() const
{
    if (typeTag.isEmpty()) return 0;
    return juce::roundToInt(MuLookAndFeel::namePlateWidth(typeTag, mu_ui::sf((float) MuLookAndFeel::kNamePlateH))) + mu_ui::s(MuLookAndFeel::kSpaceS);
}

void LayerHeaderBar::setLayerName(const juce::String& n)
{
    nameLabel.setText(n, juce::dontSendNotification);
    clipStrip.setLayerName(n);
}
void LayerHeaderBar::setColour(juce::Colour c)
{
    colour = c;
    clipStrip.setColour(c);   // the pads wear the layer's colour
    presetDD.setLcdColour(c);   // the preset selector lights in the layer colour
    notesInDD.setLcdColour(c);  // and so does the channel selector beside it
    lookAndFeelChanged();       // name display: lit lettering in metal style
    repaint();
}

void LayerHeaderBar::lookAndFeelChanged()
{
    // Metal: the name is an LCD lit in the layer colour; flat: plain heading text.
    const bool metal = MuLookAndFeel::isMetal(*this);
    auto lit = metal ? colour.brighter(MuLookAndFeel::lighting().lcdOwnLift) : MuLookAndFeel::colour(MuLookAndFeel::headingText);
    if (ghostName) lit = lit.withAlpha(MuLookAndFeel::lighting().lcdGhost * 2.5f);   // unlit, but legible
    nameLabel.setColour(juce::Label::textColourId, lit);
    nameLabel.setFont(metal ? MuLookAndFeel::lcdFont(mu_ui::sf((float) MuLookAndFeel::kLcdNameTextH))
                            : juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(15.0f))));
    repaint();
}

void LayerHeaderBar::setPresetItems(const juce::StringArray& names)
{
    presetDD.clear();
    for (int i = 0; i < names.size(); ++i)
        presetDD.addItem(names[i], i + 1);   // 1-based ids
}

void LayerHeaderBar::setPresetFiles(const juce::Array<juce::File>& files)
{
    presetFiles = files;
    juce::StringArray names;
    for (const auto& f : files) names.add(f.getFileNameWithoutExtension());
    setPresetItems(names);
}

void LayerHeaderBar::showPresetFile(const juce::File& file)
{
    presetDD.setSelectedId(presetFiles.indexOf(file) + 1, false);   // not in the list → 0 = none
}

void LayerHeaderBar::setNameEditable(bool editable) { nameLabel.setEditable(editable, editable, false); }

void LayerHeaderBar::setSelectedPresetId(int id)             { presetDD.setSelectedId(id, false); }
void LayerHeaderBar::setPresetPlaceholder(const juce::String& t) { presetDD.setPlaceholderText(t); }
void LayerHeaderBar::setStagingBadge(bool show)             { if (staging != show) { staging = show; repaint(); } }

void LayerHeaderBar::setShowReset(bool show)
{
    showReset = show;
    resetBtn.setVisible(show);
    resized();
}

void LayerHeaderBar::setShowSave(bool show)
{
    showSave = show;
    saveBtn.setVisible(show);
    resized();
}

void LayerHeaderBar::setGhostName(bool ghost)
{
    ghostName = ghost;
    lookAndFeelChanged();
}

void LayerHeaderBar::setShowClips(bool show)
{
    showClips = show;
    clipStrip.setVisible(show);
    resized();
}

void LayerHeaderBar::setShowDelete(bool show)
{
    showDelete = show;
    deleteBtn.setVisible(show);
    resized();
}

void LayerHeaderBar::setDeleteEnabled(bool enabled)
{
    if (deleteBtn.isEnabled() == enabled) return;
    deleteBtn.setEnabled(enabled);
    deleteBtn.setTooltip(enabled ? "Delete this " + noun : "The last " + noun + " cannot be deleted");
}

void LayerHeaderBar::commitNameEdit()
{
    if (nameLabel.getCurrentTextEditor() != nullptr)
        nameLabel.hideEditor(false);   // discardChanges = false → commits the text
}

void LayerHeaderBar::resized()
{
    using mu_ui::s;
    const int h         = getHeight();
    const int btnH      = s(MuLookAndFeel::kStripDisplayH);
    const int btnY      = (h - btnH) / 2;
    const int rightEdge = getWidth() - s(4);
    const int iconW     = s(kIconBtnW);
    const int presetW   = s(kPresetBtnW);

    int x = rightEdge;
    if (showDelete) { deleteBtn.setBounds(x - iconW, btnY, iconW, btnH);  x -= iconW + s(4); }
    if (showReset) { resetBtn.setBounds(x - iconW, btnY, iconW, btnH);  x -= iconW + s(4); }
    if (showSave) { saveBtn.setBounds(x - presetW, btnY, presetW, btnH);  x -= presetW + s(4); }

    // The clip strip sits right of the preset dropdown it relates to, left of Save; the dropdown yields the width.
    int ddRight = x - s(4);
    if (showClips)
    {
        const int stripW = ClipStrip::stripWidth();
        clipStrip.setBounds(ddRight - stripW, btnY, stripW, btnH);
        ddRight -= stripW + s(MuLookAndFeel::kClipStripGap);
    }

    // Notes In sits between the preset dropdown and the clip strip: an engraved caption, a 4 px gap, the selector.
    notesInCaptionR = {};
    if (notesInDD.isVisible())
    {
        const int selW = s(MuLookAndFeel::kNotesInW), capW = s(MuLookAndFeel::kNotesInLabelW);
        notesInDD.setBounds(ddRight - selW, btnY, selW, btnH);
        notesInCaptionR = { ddRight - selW - s(4) - capW, btnY, capW, btnH };
        ddRight = notesInCaptionR.getX() - s(MuLookAndFeel::kClipStripGap);
    }

    // Name occupies a fixed left slot; the preset dropdown fills the gap between.
    const int nameW  = s(120);
    const int nameX  = s(kNameX) + typeTagWidth();   // the type plate, when shown, sits between the dot and the name
    const int ddLeft = nameX + nameW + s(8);
    presetDD.setBounds(ddLeft, btnY, juce::jmax(s(80), ddRight - ddLeft), btnH);
    nameLabel.setBounds(nameX, btnY, nameW, btnH);   // same row as the preset selector
}

void LayerHeaderBar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s; using mu_ui::sf;
    const int h = getHeight();

    // Colour dot in the layer colour. Metal: the name sits on an LCD backlit in that colour
    // (its glare + bezel go on top in paintOverChildren); flat: a rounded border round it.
    g.setColour(colour);
    g.fillEllipse((float) s(kDotX), (h - s(10)) * 0.5f, (float) s(10), (float) s(10));
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawLcdGlass(g, nameDisplayBounds(), colour, true);
    else
        g.drawRoundedRectangle(nameLabel.getBounds().toFloat().reduced(1.0f), sf(4.0f), sf(1.5f));

    // The layer's type plate, left of the name LCD.
    if (typeTag.isNotEmpty())
    {
        const float plateH = sf((float) MuLookAndFeel::kNamePlateH);
        MuLookAndFeel::drawNamePlate(g, { (float) s(kNameX), ((float) h - plateH) * 0.5f, MuLookAndFeel::namePlateWidth(typeTag, plateH), plateH }, typeTag);
    }

    // The Notes In caption: engraved into the metal, or plain text in the flat look.
    if (! notesInCaptionR.isEmpty())
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kEngravedCaptionFont))));
        if (MuLookAndFeel::isMetal(*this))
            MuLookAndFeel::drawEngravedText(g, "Notes In", notesInCaptionR, juce::Justification::centredLeft,
                                            MuLookAndFeel::colour(Id::labelText));
        else
        {
            g.setColour(MuLookAndFeel::colour(Id::labelText));
            g.drawText("Notes In", notesInCaptionR, juce::Justification::centredLeft, true);
        }
    }

    // Optional pending hot-swap "SWP" pill on the preset dropdown (mu-clid).
    if (staging)
    {
        const int bw = s(28), bh = s(13);
        const int bx = presetDD.getX() - bw - s(2);
        const int by = (h - bh) / 2;
        g.setColour(juce::Colours::orange.withAlpha(0.85f));
        g.fillRoundedRectangle((float) bx, (float) by, (float) bw, (float) bh, sf(3.0f));
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(8.0f))));
        g.drawText("SWP", bx, by, bw, bh, juce::Justification::centred, false);
    }
}

juce::Rectangle<float> LayerHeaderBar::nameDisplayBounds() const
{
    // Same height and row as the preset selector, so the two displays line up.
    return nameLabel.getBounds().toFloat().withY((float) presetDD.getY())
                                         .withHeight((float) presetDD.getHeight());
}

void LayerHeaderBar::paintOverChildren(juce::Graphics& g)
{
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawLcdFront(g, nameDisplayBounds());
}
