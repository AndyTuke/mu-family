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

    // ─── Filter ──────────────────────────────────────────────────────────
    int   filterType     = 0;        // 0=LP, 1=HP, 2=BP
    float filterCutoff   = 8000.0f;  // Hz
    float filterRes      = 0.2f;     // 0..0.99
    float filterEnvAtk   = 0.01f;
    float filterEnvDec   = 0.3f;
    float filterEnvSus   = 0.0f;
    float filterEnvRel   = 0.3f;
    float filterEnvDepth = 0.0f;     // semitones of cutoff sweep, 0..48

    // ─── Amp ─────────────────────────────────────────────────────────────
    float ampLevel    = 0.5f;        // 0..2  (Stage 19: −6 dB default for 6 dB headroom budget per voice)
    float ampEnvAtk   = 0.005f;
    float ampEnvDec   = 0.3f;
    float ampEnvSus   = 0.8f;
    float ampEnvRel   = 0.5f;
    bool  ampRelToEnd = false;       // true when Release is at max (100): amp envelope bypassed, sample plays to natural end

    // ─── Drive / Insert (after filter, before amp) ───────────────────────
    int   driveChar   = 0;           // 0=None, 1=Soft, 2=Hard, 3=Fold, 4=Bitcrusher
    // Soft / Hard / Fold params:
    float driveDrive  = 0.0f;        // 0..100% input drive
    float driveOutput = 0.0f;        // -24..0 dB output level
    // Bitcrusher params:
    float drvBits     = 16.0f;       // 1..16 bit depth
    float driveRate   = 48000.0f;    // 100..48000 Hz target sample rate (48000 = no reduction)
    float drvDither   = 0.0f;        // 0..100% TPDF dither amount
    // Shared:
    float driveTone   = 20000.0f;    // 20..20000 Hz (1-pole LP post-drive; 20kHz = flat)

    // ─── Accent ──────────────────────────────────────────────────────────
    float accentDb    = 0.0f;        // 0..12 dB boost applied to accented steps

    // ─── Modulation scratchpad (audio thread only, not persisted) ────────
    float pitchMod    = 0.0f;        // semitone offset added by modulators, -24..+24
};
