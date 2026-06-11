#include "ActivationPanel.h"
#include "UI/ModalCard.h"
#include "UI/Components/MuLookAndFeel.h"

ActivationPanel::ActivationPanel (ProcessorBase& proc)
    : processorRef (proc)
{
    keyLabel.setText ("License key", juce::dontSendNotification);
    addAndMakeVisible (keyLabel);

    keyEditor.setMultiLine (false);
    keyEditor.setTextToShowWhenEmpty ("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX", juce::Colours::grey);
    keyEditor.onReturnKey = [this] { activatePressed(); };
    addAndMakeVisible (keyEditor);

    activateBtn.onClick = [this] { activatePressed(); };
    addAndMakeVisible (activateBtn);

    closeBtn.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible (closeBtn);

    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel);
}

juce::Rectangle<int> ActivationPanel::cardBounds() const
{
    using mu_ui::s;
    const int cardW = s (kCardW);
    const int cardH = s (kCardH);
    return { (getWidth() - cardW) / 2, (getHeight() - cardH) / 2, cardW, cardH };
}

void ActivationPanel::setStatus (const juce::String& text, bool error)
{
    using Id = MuLookAndFeel::ColourIds;
    statusLabel.setColour (juce::Label::textColourId,
                           MuLookAndFeel::colour (error ? Id::labelText : Id::headingText));
    statusLabel.setText (text, juce::dontSendNotification);
}

void ActivationPanel::activatePressed()
{
    const auto key = keyEditor.getText().trim();
    if (key.isEmpty())          { setStatus ("Enter the license key from your purchase email.", true); return; }
    if (! processorRef.activateOnlineFn) { setStatus ("Online activation isn't available in this build.", true); return; }

    activateBtn.setEnabled (false);
    setStatus ("Contacting the activation server\xe2\x80\xa6", false);

    auto fn = processorRef.activateOnlineFn;            // copy (safe to use off-thread)
    juce::Component::SafePointer<ActivationPanel> safe (this);

    juce::Thread::launch ([safe, fn, key]
    {
        const auto outcome = fn (key);                  // blocks on the network here
        juce::MessageManager::callAsync ([safe, outcome]
        {
            if (safe == nullptr) return;                // editor closed mid-activation
            safe->activateBtn.setEnabled (true);
            safe->setStatus (outcome.message, ! outcome.ok);
            if (outcome.ok && safe->onActivated)
                safe->onActivated();                    // shell hides demo banner + relayouts
        });
    });
}

void ActivationPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        if (onDismiss) onDismiss();
}

void ActivationPanel::resized()
{
    using mu_ui::s;
    auto card = cardBounds();
    const int pad = s (24);
    const int x = card.getX() + pad;
    const int w = card.getWidth() - 2 * pad;

    int y = card.getY() + s (70);          // below the title (painted)
    keyLabel.setBounds (x, y, w, s (18));   y += s (20);
    keyEditor.setBounds (x, y, w, s (28));  y += s (38);
    activateBtn.setBounds (x, y, s (110), s (28));
    y += s (40);
    statusLabel.setBounds (x, y, w, s (54));

    closeBtn.setBounds (card.getX() + card.getWidth() - s (84), card.getBottom() - s (40), s (60), s (28));
}

void ActivationPanel::paint (juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    mu_ui::fillModalDim (g);
    auto card = cardBounds();
    mu_ui::paintModalCard (g, card);

    const int tx = card.getX() + s (24);
    const int textW = card.getWidth() - s (48);

    // Title.
    g.setColour (MuLookAndFeel::colour (Id::headingText));
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (sf (22.0f))));
    g.drawText ("Activate " + (productName.isNotEmpty() ? productName : juce::String ("mu")),
                tx, card.getY() + s (22), textW, s (28), juce::Justification::centredLeft, false);

    // Subtitle.
    g.setColour (MuLookAndFeel::colour (Id::mutedText));
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (sf (11.0f))));
    g.drawText ("Enter your license key to unlock all features.",
                tx, card.getY() + s (48), textW, s (16), juce::Justification::centredLeft, false);

    // Offline fallback — the machine challenge code for an offline `.lic`.
    const int oy = statusLabel.getBottom() + s (8);
    g.setColour (MuLookAndFeel::colour (Id::mutedText));
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (sf (10.0f))));
    g.drawText ("No internet? Quote this machine code for an offline license file:",
                tx, oy, textW, s (14), juce::Justification::centredLeft, false);
    g.setColour (MuLookAndFeel::colour (Id::headingText));
    g.setFont (juce::Font (juce::FontOptions{}.withName (juce::Font::getDefaultMonospacedFontName())
                                              .withHeight (sf (13.0f))));
    g.drawText (processorRef.licenseChallengeCode(),
                tx, oy + s (15), textW, s (16), juce::Justification::centredLeft, false);
}
