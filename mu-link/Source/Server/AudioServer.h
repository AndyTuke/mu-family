#pragma once

#include "../Ipc/MuLinkProtocol.h"
#include "../Ipc/AudioRing.h"
#include "../Clock/TransportClock.h"

// AudioServer — SKELETON for the next increment (not yet implemented).
//
// Owns the one hardware device (JUCE AudioDeviceManager, runtime-selectable WASAPI/
// ASIO) and runs the single audio callback. Per block it will:
//   1. clock.advance(numFrames); publish the TransportBlock to shared memory.
//   2. For each active client: ring.readFrames(...); zero-fill any underrun.
//   3. Sum client streams (later: per-client level/pan/mute via mu-core MixerEngine).
//   4. Write the mix to the device output buffer.
//   5. Emit MIDI-clock-out pulses for this block (clock.pulsesElapsed delta).
//
// The shared-memory mapping (Win32 CreateFileMapping for the TransportBlock, the
// ClientRegistry, and one ring region per client) lands alongside this. The pieces it
// composes — AudioRing (lock-free SPSC) and TransportClock (sample-accurate) — are
// already implemented and unit-tested.
namespace mu_link
{

class AudioServer
{
public:
    AudioServer() = default;

    // TODO(next): prepare(deviceManager, sampleRate, blockSize); start(); stop();
    //             audioDeviceIOCallback → steps 1–5 above.

private:
    TransportClock clock;
    AudioRing      clientRings[kMaxClients];
};

} // namespace mu_link
