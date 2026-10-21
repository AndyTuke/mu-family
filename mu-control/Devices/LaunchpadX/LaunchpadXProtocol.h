#pragma once

#include "Surface/SurfaceEvent.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The Launchpad X wire protocol as pure functions: control numbering, input decoding and the SysEx messages.
// No port, no state. Numbering in Programmer mode: pad = 10 * row + column (row 1 is the bottom, column 1 the
// left), top round buttons CC 91..98, right-hand column CC 19, 29 .. 89 (bottom to top), logo CC 99 (a Special button:
// its press and release are reported, and it can be lit; a product that gives it no meaning simply never maps it).
namespace mu_control::launchpadx
{

constexpr int kLightingBatchMax = 81;   // entries one lighting message may carry

struct PadLight
{
    int      index = 0;                    // pad note or button CC number
    LedMode  mode  = LedMode::Static;      // only used by the palette form
    uint32_t rgb   = 0;                    // 0xRRGGBB, 0 = off
};

// Numbering. Surface y counts from the top, so the bottom row is y = 7 (the mapping puts layer 1 on y = 0). The one Special
// control is the logo, CC 99.
std::optional<int>              indexOf(const SurfaceControlId& id);
std::optional<SurfaceControlId> controlOf(int index, bool isCc);

// Mode and setup messages.
std::vector<uint8_t> programmerMode(bool on);
std::vector<uint8_t> aftertouchOff();
std::vector<uint8_t> readLayout();
std::vector<uint8_t> deviceInquiry();
bool                 isInquiryReply(const uint8_t* data, size_t size);

// Input as the pad sends it (note-on velocity 0 is a release; CC 127 press, 0 release). Used by the tests as
// the fake pad. A control without an index gives an empty message.
std::vector<uint8_t> encodeInput(const SurfaceControlId& id, bool down);

// Every event in a buffer of complete MIDI messages; SysEx, aftertouch and unknown notes are skipped.
std::vector<SurfaceEvent> decodeInput(const uint8_t* data, size_t size);

// Lighting SysEx (type 3 = RGB, each channel 0..127), split into messages of at most kLightingBatchMax entries.
// With `rgb` false the palette forms are used instead: static (0), flashing (1), pulsing (2).
std::vector<std::vector<uint8_t>> lightingMessages(const std::vector<PadLight>& lights, bool rgb);

} // namespace mu_control::launchpadx
