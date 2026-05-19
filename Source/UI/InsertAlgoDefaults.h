#pragma once

// shared first-visit defaults for each insert-effect algorithm.
//
// Consumed by both [VoiceSection.cpp](VoiceSection.cpp) (the per-rhythm voice-
// insert page) and [MixerChannel_Insert.cpp](MixerChannel_Insert.cpp) (the
// master-insert pair page). Before this header, each file owned its own copy
// of an InsertAlgoSnapshot[13] table — and they had already drifted at index
// 7 / 8 (the Compressor / Limiter defaults: voice page held drvDither=5,
// driveTone=200; master page held drvDither=0, driveTone=200). Same algorithm,
// two different first-visit baselines depending on which page the user touched
// it from. Single source of truth eliminates that.
//
// Indices match `driveChar` from the algorithm dispatch table in
// [InsertProcessor.cpp](../Audio/InsertProcessor.cpp).
//
// VoiceSection::InsertAlgoSnapshot and MixerChannel::InsertAlgoSnapshot are
// `using` aliases to this struct in their respective .h files so existing
// external references continue to compile unchanged.

// field names renamed `drive*` / `drv*` → `insert*` in lockstep with
// VoiceParams. Field ORDER preserved so the brace-init table below doesn't
// shift element positions.
struct InsertAlgoDefaults
{
    float insertDrive  = 0.0f;
    float insertOutput = 0.0f;
    float insertDither = 0.0f;
    float insertTone   = 20000.0f;
    float insertEqMid  = 0.0f;
    float insertBits   = 16.0f;
    float insertRate   = 48000.0f;
};

namespace mu_ui {

// First-visit defaults — drives the per-algorithm A/B-snapshot baseline when
// the user activates an algo on a slot for the first time. Reused for both
// per-voice and master-insert paths.
//
// Field order: driveDrive | driveOutput | drvDither | driveTone | eqMidGain | drvBits | driveRate
inline const InsertAlgoDefaults kInsertAlgoDefaults[13] = {
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 0  None
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 1  Soft Clip  (0% drive = transparent)
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 2  Hard Clip
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 3  Fold
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 4  Bitcrusher (16-bit, 48 kHz, flat)
    { 100.0f, 0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 5  Clipper    (100% = full range)
    { 50.0f,  0.0f,  50.0f,  1000.0f,  0.0f, 16.0f, 48000.0f },  // 6  EQ         (0 dB all bands, 1 kHz mid)
    // VoiceSection had drvDither=5 / driveTone=200; MixerChannel had
    // drvDither=0 / driveTone=200. Adopted the VoiceSection values as canonical
    // — drvDither encodes attack time for compressor/limiter, 5 ms is the
    // documented default ("−12 dB, 10 ms atk, 200 ms rel"). The mixer values
    // were the drifted ones.
    { 30.0f,  0.0f,  5.0f,   200.0f,   0.0f, 16.0f, 48000.0f },  // 7  Compressor (−12 dB, 5 ms atk, 200 ms rel)
    { 30.0f,  0.0f,  5.0f,   200.0f,   0.0f, 16.0f, 48000.0f },  // 8  Limiter    (−12 dB, 5 ms atk, 200 ms)
    { 50.0f,  0.0f,  0.0f,   440.0f,   0.0f, 16.0f, 48000.0f },  // 9  Ring Mod   (50% mix, 440 Hz)
    { 0.0f,   0.0f,  0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 10 Tape Sat   (0% drive = transparent)
    // Karplus-Strong: drvDrive=0→Note C, drvBits=1→Octave 1,
    // drvDither=70→Feedback 70%, driveTone=20000→LP fully open.
    { 0.0f,   0.0f,  70.0f,  20000.0f, 0.0f,  1.0f, 48000.0f },  // 11 Karplus-Strong
    // Vocoder: drvDrive=0 → Saw; drvBits=4 → Note F (idx 3);
    // drvDither=3 → Octave 3; drvOut=-20 → Unison index 1 (3 voices).
    { 0.0f,  -20.0f, 3.0f,   20000.0f, 0.0f,  4.0f, 48000.0f },  // 12 Vocoder
};

} // namespace mu_ui
