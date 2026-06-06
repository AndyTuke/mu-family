// mu-link-server — runnable headless audio server (Stage L2).
//
// Opens the default hardware output, creates the shared-memory bus, and sums every
// attached client into the master while publishing the sample-accurate transport. This
// is the local-only mu-link backend; the L3 GUI app reuses the same AudioServer with a
// device picker + connected-client view on top. Run it, then launch a mu standalone to
// hear it sum through the bus.
//
//   Build:  cmake --build build --config Debug --target mu-link-server
//   Run:    build/mu-link/mu-link-server_artefacts/Debug/mu-link-server.exe

#include <juce_audio_devices/juce_audio_devices.h>
#include "AudioServer.h"

#include <iostream>

int main(int, char**)
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager for the device backend

    mu_link::AudioServer server;
    if (! server.start())
    {
        std::cerr << "mu-link: failed to open the audio device or the shared-memory bus.\n";
        return 1;
    }

    if (auto* device = server.audioDeviceManager().getCurrentAudioDevice())
        std::cout << "mu-link server running on \"" << device->getName().toStdString() << "\" @ "
                  << device->getCurrentSampleRate() << " Hz, block "
                  << device->getCurrentBufferSizeSamples() << " frames.\n";

    std::cout << "Shared-memory bus is live — mu standalones will auto-attach.\n"
                 "Press Enter to quit.\n";
    std::cin.get();

    server.stop();
    return 0;
}
