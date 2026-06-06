// mu-link-tone — a minimal reference client, now built on the mu-core MuLinkClient.
//
// Originally the standalone reference producer loop; with L4 that loop moved into
// mu-core/Link/MuLinkClient (shared by every product), so this tool just drives it: set
// an onRender sine, attach, wait. It doubles as a live smoke test of MuLinkClient.
//
//   Build:  cmake --build build --config Debug --target mu-link-tone
//   Run:    mu-link-tone [freqHz]        (default 440)

#include "Link/MuLinkClient.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    const double freqHz = (argc > 1) ? std::atof(argv[1]) : 440.0;

    mu_link::MuLinkClient client;

    // Render a sine into every channel. Phase persists across calls (the producer thread
    // calls this back-to-back); we recompute the increment from the published sample rate.
    double phase = 0.0;
    const double twoPi = 6.283185307179586;
    client.onRender([&] (float* const* output, int numChannels, int numFrames,
                         const mu_link::TransportSnapshot& t)
    {
        const double sr  = t.sampleRate != 0 ? (double) t.sampleRate : 48000.0;
        const double inc = twoPi * freqHz / sr;
        for (int i = 0; i < numFrames; ++i)
        {
            const float s = (float) (0.2 * std::sin(phase));
            phase += inc;
            if (phase >= twoPi) phase -= twoPi;
            for (int c = 0; c < numChannels; ++c)
                output[c][i] = s;
        }
    });

    if (! client.attach("mu-link-tone", mu_link::kMaxChannels))
    {
        std::cerr << "mu-link-tone: mu-link is not running (start mu-link-server first).\n";
        return 1;
    }

    std::cout << "mu-link-tone: attached as slot " << client.slotIndex() << ", emitting "
              << freqHz << " Hz. Press Enter to stop.\n";
    std::cin.get();

    client.detach();
    std::cout << "mu-link-tone: detached.\n";
    return 0;
}
