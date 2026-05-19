#pragma once

struct VoiceParams
{
    // ─── Pitch ───────────────────────────────────────────────────────────
    int   pitchOctave    = 0;        // -4..+4
    int   pitchSemitones = 0;        // -12..+12
    float pitchFine      = 0.0f;     // cents, -100..+100
    float pitchEnvAtk    = 0.005f;
    float pitchEnvDec    = 0.1f;
    float pitchEnvSus    = 0.0f;
    float pitchEnvRel    = 0.1f;
    float pitchEnvDepth  = 0.0f;     // semitones swept at envelope peak, 0..24
    bool  pitchEnvLegato = false;    // false=Reset (default), true=Legato (continue from current level)

    // ─── Filter ──────────────────────────────────────────────────────────
    int   filterType     = 0;        // 0=LP12, 1=HP12, 2=BP12, 3=Notch, 4=LP24, 5=HP24, 6=BP24, 7=LP6, 8=Comb+, 9=AP12, 10=Notch24, 11=HP6, 12=Peak, 13=LoShf, 14=HiShf, 15=Comb-
    float filterCutoff   = 8000.0f;  // Hz
    float filterRes      = 0.2f;     // 0..0.99
    float filterEnvAtk   = 0.01f;
    float filterEnvDec   = 0.3f;
    float filterEnvSus   = 0.0f;
    float filterEnvRel   = 0.3f;
    float filterEnvDepth = 0.0f;     // semitones of cutoff sweep, 0..48
    bool  filterEnvLegato = false;   // false=Reset (default), true=Legato

    // ─── Amp ─────────────────────────────────────────────────────────────
    float ampLevel    = 1.0f;        // 0..2  (Issue #121: 0 dB default — kHeadroomTrim alone provides summing safety)
    float ampEnvAtk   = 0.005f;
    float ampEnvDec   = 0.3f;
    float ampEnvSus   = 0.8f;
    float ampEnvRel   = 0.5f;
    bool  ampRelToEnd = false;       // true when Release is at max (100): amp envelope bypassed, sample plays to natural end
    bool  ampEnvLegato = false;      // false=Reset (default), true=Legato

    // ─── Insert (after filter, before amp) ───────────────────────────────
    // renamed `drive*` / `drv*` C++ fields → `insert*` for clarity —
    // the algorithm dispatch table at this stage now covers EQ, Compressor,
    // Limiter, RingMod, TapeSat, Karplus, and Vocoder, which were never
    // "drive" algorithms. Only the in-memory field names change; APVTS IDs
    // and preset XML keys are still `drv*` for full back-compat (DAW
    // automation lanes and v0/v1/v2 preset files all keep working).
    // 0=None, 1=SoftClip, 2=HardClip, 3=Fold, 4=Bitcrusher, 5=Clipper, 6=EQ,
    // 7=Compressor, 8=Limiter, 9=RingMod, 10=TapeSat, 11=Karplus, 12=Vocoder.
    int   insertAlgo   = 0;
    // Soft Clip / Hard Clip / Fold params:
    float insertDrive  = 0.0f;        // 0..100% input drive
    float insertOutput = 0.0f;        // -24..0 dB output level
    // Bitcrusher params:
    float insertBits   = 16.0f;       // 1..16 bit depth
    float insertRate   = 48000.0f;    // 100..48000 Hz target sample rate (48000 = no reduction)
    float insertDither = 0.0f;        // 0..100% TPDF dither amount
    // Shared:
    float insertTone   = 20000.0f;    // 20..20000 Hz (1-pole LP post-drive; 20kHz = flat; also EQ mid freq / comp release ms)
    // EQ params (insertAlgo=6): low shelf and high shelf gains stored as 0..100 in insertDrive/insertDither fields
    float insertEqMid  = 0.0f;        // EQ mid peak gain, -18..+18 dB (#129)

    // ─── Accent ──────────────────────────────────────────────────────────
    float accentDb    = 0.0f;        // 0..12 dB boost applied to accented steps

    // ─── Modulation scratchpad (audio thread only, not persisted) ────────
    float pitchMod    = 0.0f;        // semitone offset added by modulators, -48..+48
};
