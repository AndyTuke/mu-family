#pragma once
// status-ok: a modal dialog, not a control on the editor; its own controls are the standard ones.

#include "UI/ModalDialog.h"
#include "UI/ConfirmDialog.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Plugin/ProcessorBase.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// "Bounce to WAV" (Standalone only): asks for a file, a length, a tail and a sample rate, then renders the current state
// offline through the processor's bounceToWav hook (the standalone app's headless render) and reports where the file went.
// One dialog for every product; the editor's shell offers it from the Settings page when ProcessorBase::canBounce().
namespace mu_ui {

class BounceDialog
{
public:
    // `defaultName` seeds the file name ("<name>_bounced.wav" in the app's Bounces folder).
    static void show(juce::Component* anchor, ProcessorBase& proc, const juce::String& defaultName)
    {
        auto state = std::make_shared<State>();
        state->settings.file = proc.getContentDir().getChildFile("Bounces")
                                   .getChildFile(juce::File::createLegalFileName(defaultName.isNotEmpty() ? defaultName : proc.getName())
                                                 + "_bounced.wav");
        const double rate = proc.getSampleRate();
        state->settings.sampleRate = rate >= 44100.0 && rate <= 192000.0 ? rate : 48000.0;

        auto* dlg = new ModalDialog();
        dlg->width(420).title("Bounce to WAV")
            .message("Renders the current sound and patterns to a 24-bit WAV file. The live transport pauses while it renders.")
            .icon(ModalDialog::Icon::Info)
            .content(std::make_unique<Content>(state), Content::kHeight)
            .button("Cancel", 0).button("Bounce", 1, true).cancelResult(0);

        juce::Component::SafePointer<juce::Component> safeAnchor(anchor);
        dlg->onResult = [state, safeAnchor, &proc](int result)
        {
            if (result != 1 || safeAnchor == nullptr) return;
            // Let the dialog close and a "Rendering" notice paint, then render (it runs on this thread and takes a moment).
            auto* busy = new ModalDialog();
            busy->width(360).title("Bouncing").message("Rendering " + state->settings.file.getFileName() + "...")
                 .icon(ModalDialog::Icon::Info).cancelResult(0);
            busy->show(safeAnchor.getComponent());
            juce::Component::SafePointer<ModalDialog> safeBusy(busy);
            juce::Timer::callAfterDelay(80, [state, safeAnchor, safeBusy, &proc]
            {
                const auto error = proc.bounceToWav ? proc.bounceToWav(state->settings) : juce::String("Bounce is not available here.");
                if (safeBusy != nullptr) safeBusy->close(0);
                if (safeAnchor == nullptr) return;
                if (error.isEmpty())
                    messageAsync(safeAnchor.getComponent(), "Bounce finished", "Saved " + state->settings.file.getFullPathName());
                else
                    messageAsync(safeAnchor.getComponent(), "Bounce failed", error);
            });
        };
        dlg->show(anchor);
    }

private:
    // The values the dialog edits, shared with its result callback (the content component is gone by then).
    struct State
    {
        ProcessorBase::BounceSettings settings;
    };

    class Content : public juce::Component
    {
    public:
        static constexpr int kHeight = 96;

        explicit Content(std::shared_ptr<State> s)
            : state(std::move(s)), length("Length (s)", 1, 300, 8), tail("Tail (s)", 0, 30, 1)
        {
            length.setShowStepButtons(false);
            tail.setShowStepButtons(false);
            length.setValue((int) state->settings.seconds);
            tail.setValue((int) state->settings.tailSeconds);
            length.onChange = [this](int v) { state->settings.seconds = v; };
            tail.onChange   = [this](int v) { state->settings.tailSeconds = v; };
            addAndMakeVisible(length);
            addAndMakeVisible(tail);

            for (const int hz : { 44100, 48000, 88200, 96000 })
                rate.addItem(juce::String(hz) + " Hz", hz);
            rate.setSelectedId(nearestRate(state->settings.sampleRate), false);
            rate.setStatusName("Bounce Sample Rate");
            rate.onChange = [this](int hz) { state->settings.sampleRate = hz; };
            addAndMakeVisible(rate);

            chooseBtn.setButtonText("Choose file...");
            chooseBtn.onClick = [this] { chooseFile(); };
            addAndMakeVisible(chooseBtn);
            addAndMakeVisible(fileLabel);
            fileLabel.setFont(MuLookAndFeel::uiFont(mu_ui::sf(11.0f)));
            fileLabel.setJustificationType(juce::Justification::centredLeft);
            fileLabel.setMinimumHorizontalScale(0.6f);
            showFile();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop(s(52));
            const int w = (top.getWidth() - s(16)) / 3;
            length.setBounds(top.removeFromLeft(w));
            top.removeFromLeft(s(8));
            tail.setBounds(top.removeFromLeft(w));
            top.removeFromLeft(s(8));
            rate.setBounds(top.withTrimmedTop(s(14)).withHeight(s(28)));

            r.removeFromTop(s(10));
            auto fileRow = r.removeFromTop(s(28));
            chooseBtn.setBounds(fileRow.removeFromRight(s(110)));
            fileRow.removeFromRight(s(8));
            fileLabel.setBounds(fileRow);
        }

    private:
        static int nearestRate(double hz)
        {
            int best = 48000;
            for (const int c : { 44100, 48000, 88200, 96000 })
                if (std::abs(c - hz) < std::abs(best - hz)) best = c;
            return best;
        }

        void showFile() { fileLabel.setText(state->settings.file.getFullPathName(), juce::dontSendNotification); }

        void chooseFile()
        {
            chooser = std::make_unique<juce::FileChooser>("Bounce to WAV", state->settings.file, "*.wav");
            juce::Component::SafePointer<Content> safe(this);
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safe](const juce::FileChooser& fc)
                                 {
                                     const auto f = fc.getResult();
                                     if (safe == nullptr || f == juce::File()) return;
                                     safe->state->settings.file = f.hasFileExtension("wav") ? f : f.withFileExtension("wav");
                                     safe->showFile();
                                 });
        }

        std::shared_ptr<State> state;
        NudgeInput length, tail;
        DropdownSelect rate;
        juce::TextButton chooseBtn;
        juce::Label fileLabel;
        std::unique_ptr<juce::FileChooser> chooser;
    };
};

} // namespace mu_ui
