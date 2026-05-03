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
    float ampLevel  = 1.0f;          // 0..2
    float ampEnvAtk = 0.005f;
    float ampEnvDec = 0.3f;
    float ampEnvSus = 0.8f;
    float ampEnvRel = 0.5f;

    // ─── Drive (insert after filter, before amp) ──────────────────────────
    int   driveChar   = 0;           // 0=Soft, 1=Hard, 2=Fold, 3=Bit
    float driveDrive  = 0.0f;        // 0..100%  (0 = unity through all characters)
    float driveOutput = 0.0f;        // -24..0 dB
    float driveTone   = 20000.0f;    // 20..20000 Hz (LP after drive; 20kHz = flat)
};
