#pragma once

#include "Audio/ToniVoice.h"
#include "Sequencer/ArpRunner.h"   // the arp clock and note scan, voice-independent (shared with mu-Toni MIDI)

// mu-Toni's runner: the shared arp clock driving one ToniVoice. See ArpRunner.h.
namespace mu_toni
{
using ArpVoiceRunner = BasicArpRunner<ToniVoice>;
} // namespace mu_toni
