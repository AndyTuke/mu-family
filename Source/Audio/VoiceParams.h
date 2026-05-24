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
    // Per-envelope legato fields removed in #614 — envelope retrigger now
    // follows the rhythm-level `patternLegato` flag uniformly. Tied hits
    // skip the noteOn/reset; untied hits always reset to zero.

    // ─── Filter ──────────────────────────────────────────────────────────
    int   filterType     = 0;        // 0=LP12, 1=HP12, 2=BP12, 3=Notch, 4=LP24, 5=HP24, 6=BP24, 7=LP6, 8=Comb+, 9=AP12, 10=Notch24, 11=HP6, 12=Peak, 13=LoShf, 14=HiShf, 15=Comb-
    float filterCutoff   = 8000.0f;  // Hz
    float filterRes      = 0.2f;     // 0..0.99
    float filterEnvAtk   = 0.01f;
    float filterEnvDec   = 0.3f;
    float filterEnvSus   = 0.0f;
    float filterEnvRel   = 0.3f;
    float filterEnvDepth = 0.0f;     // semitones of cutoff sweep, 0..48
    float filterLowCutHz = 0.0f;     // 0..1000 Hz, 4-pole HPF inline with the voice filter chain — 0 = bypass

    // ─── Amp ─────────────────────────────────────────────────────────────
    float ampLevel    = 1.0f;        // 0..2  (Issue #121: 0 dB default — kHeadroomTrim alone provides summing safety)
    float ampEnvAtk   = 0.005f;
    float ampEnvDec   = 0.3f;
    float ampEnvSus   = 0.8f;
    float ampEnvRel   = 0.5f;
    bool  ampRelToEnd = false;       // true when Release is at max (100): amp envelope bypassed, sample plays to natural end

    // ─── Insert (after filter, before amp) ───────────────────────────────
    // 0=None, 1=SoftClip, 2=HardClip, 3=Fold, 4=Bitcrusher, 5=Clipper, 6=EQ,
    // 7=Compressor, 8=Limiter, 9=RingMod, 10=TapeSat, 11=Karplus, 12=Vocoder,
    // 13=VocoderSt. Algorithm count is `mu_audio::kInsertAlgorithmCount`.
    int   insertAlgo   = 0;

    // Generic 4-slot parameter array. Each algorithm interprets the slots
    // through `mu_ui::kInsertAlgoSlots[insertAlgo]` (Source/Audio/InsertSlotConfig.h):
    //   SoftClip  : [0]=Drive 0..100, [1]=Output -24..0 dB, [2]=—, [3]=LPF 20..20000 Hz
    //   Bitcrusher: [0]=Bits 1..16,   [1]=Rate 100..48000 Hz, [2]=Dither 0..100 %, [3]=LPF
    //   EQ        : [0]=Low ±18 dB,   [1]=Mid ±18 dB,         [2]=Mid 200..8000 Hz, [3]=High ±18 dB
    //   …per the full table.
    // Stored as NORMALISED 0..1 in both VoiceParams and APVTS; the DSP calls
    // `mu_ui::normToActual(insertParam[N], insertAlgo, N)` to get the actual
    // value with the slot's skew applied. This eliminates the prior overloading
    // (insertDrive holding Note semantics when Karplus is active, insertOutput
    // holding encoded-Unison when Vocoder is active, etc.) — every algorithm
    // gets its own clean per-slot range, single source of truth in the config
    // table.
    static constexpr int kInsertSlotCount = 4;
    float insertParam[kInsertSlotCount] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // ─── Polyphony ───────────────────────────────────────────────────────
    // When true, VoiceEngine::trigger forces every hit to claim voices[0]
    // (skips the inactive-slot search and the round-robin steal) and only
    // exercises ampEnvs[0].
    bool  voiceMono   = false;

    // ─── Accent ──────────────────────────────────────────────────────────
    float accentDb    = 0.0f;        // 0..12 dB boost applied to accented steps

    // ─── Modulation scratchpad (audio thread only, not persisted) ────────
    float pitchMod    = 0.0f;        // semitone offset added by modulators, -48..+48
};
