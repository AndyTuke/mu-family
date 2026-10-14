#include "Control/ControlAttachment.h"
#include "Manager/ControllerManager.h"

// Fills mu-core's factory slot when mu-control is linked into a standalone app, so the shell can ask for a
// manager without knowing this library. This file is compiled straight into the executable (INTERFACE
// sources), so the initialiser below always runs.
namespace
{

[[maybe_unused]] const bool registered = []
{
    mu_core::controlAttachmentFactory() = [](mu_core::ControlSink& sink, const mu_core::LiveView& view,
                                             const mu_core::SurfaceLook& look, const juce::File& settingsFolder)
        -> std::unique_ptr<mu_core::ControlAttachment>
    {
        return std::make_unique<mu_control::ControllerManager>(sink, view, look, settingsFolder);
    };
    return true;
}();

}
