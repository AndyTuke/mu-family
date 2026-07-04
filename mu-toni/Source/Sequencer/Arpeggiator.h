#pragma once

#include "Audio/Chords.h"
#include "Audio/Scales.h"
#include <cmath>

// mu-toni arpeggiator — the deterministic note-generation core.
// See docs/mu-toni/design-sequencer.md. Pure functions (no state, no allocation),
// so the whole arp is a modulation target and identical every loop.
//
// Pipeline:  params → buildPool (chord·inversion·octaves·snap) → buildScan
//            (skewed-triangle Direction) → stepMidi (pool index → MIDI note).
namespace mu_toni
{

struct ArpParams
{
    int   scale        = 0;      // 0..11  (Scales.h)
    int   rootNote     = 0;      // 0..11  (C..B)
    int   rootOctave   = 4;      // 0..8   (C4 = MIDI 60)
    int   chord        = 2;      // Chords.h index — default Major
    int   inversion    = 0;      // bipolar: +k up-inversions, −k drop voicings
    int   octavesSpan  = 2;      // 1..4
    float direction    = 100.0f; // −100..+100 (skewed-triangle scan)
    bool  diatonicSnap = false;
};

inline constexpr int kMaxPool = kMaxChordTones * 4;  // 7 tones × 4 octaves
inline constexpr int kMaxScan = 2 * kMaxPool;        // up-run + interior down-run

// ── Sorted, unique semitone pool (offsets above the root) ───────────────────
inline int buildPool(const ArpParams& p, int* pool, int maxPool) noexcept
{
    const Chord& c = kChords[(size_t) clampChordIndex(p.chord)];
    const int    n = c.count;

    int voiced[kMaxChordTones];
    for (int i = 0; i < n; ++i) voiced[i] = c.intervals[(size_t) i];

    // Bipolar inversion (intervals are ascending, so lowest = first entries).
    int inv = p.inversion;
    if (inv >  n) inv =  n;
    if (inv < -n) inv = -n;
    for (int k = 0; k <  inv && k < n; ++k) voiced[k]         += 12;   // raise lowest
    for (int k = 0; k < -inv && k < n; ++k) voiced[n - 1 - k] -= 12;   // drop highest

    auto sortAsc = [](int* a, int len)
    {
        for (int i = 1; i < len; ++i)
        {
            const int v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; --j; }
            a[j + 1] = v;
        }
    };
    sortAsc(voiced, n);

    int span = p.octavesSpan;
    if (span < 1) span = 1;
    if (span > 4) span = 4;

    int count = 0;
    for (int oct = 0; oct < span; ++oct)
        for (int i = 0; i < n && count < maxPool; ++i)
        {
            int s = voiced[i] + 12 * oct;
            if (p.diatonicSnap) s = snapToScale(s, p.scale);
            pool[count++] = s;
        }

    sortAsc(pool, count);

    // Drop adjacent duplicates (octave stacking / snap can collide).
    int w = 0;
    for (int i = 0; i < count; ++i)
        if (w == 0 || pool[i] != pool[w - 1]) pool[w++] = pool[i];
    return w;
}

// ── Skewed-triangle scan → sequence of pool indices for one cycle ───────────
// fUp = (Dir+100)/200 = share of the cycle ascending. The full side plays every
// note; the short side plays M interior notes (endpoints excluded), evenly.
inline int buildScan(int poolLen, float direction, int* scan, int maxScan) noexcept
{
    const int N = poolLen;
    if (N <= 0) return 0;
    if (N == 1) { if (maxScan >= 1) scan[0] = 0; return 1; }

    float d = direction;
    if (d < -100.0f) d = -100.0f;
    if (d >  100.0f) d =  100.0f;
    const float fUp = (d + 100.0f) / 200.0f;

    int count = 0;
    auto push = [&](int idx) { if (count < maxScan) scan[count++] = idx; };

    if (fUp >= 0.999f) { for (int i = 0;     i < N;  ++i) push(i); return count; }  // up-arp
    if (fUp <= 0.001f) { for (int i = N - 1; i >= 0; --i) push(i); return count; }  // down-arp

    const bool  upFull    = (fUp >= 0.5f);
    const float longFrac  = upFull ? fUp : (1.0f - fUp);
    const float shortFrac = upFull ? (1.0f - fUp) : fUp;
    const int   interior  = N - 2;                       // indices 1 .. N-2

    int M = 0;
    if (interior > 0)
        M = (int) std::lround((double) interior * (shortFrac / longFrac));
    if (M > interior) M = interior;
    if (M < 0) M = 0;

    // Full sweep.
    if (upFull) for (int i = 0;     i < N;  ++i) push(i);
    else        for (int i = N - 1; i >= 0; --i) push(i);

    // Interior return sweep (opposite direction), M notes evenly spaced.
    for (int j = 0; j < M; ++j)
    {
        int off;                                          // 0..(N-3) from the near endpoint
        if (M == 1) off = (N - 2) / 2;                    // middle interior note
        else        off = (int) std::lround((double) j * (N - 3) / (M - 1));
        // upFull → returning downward from N-2; else returning upward from 1.
        push(upFull ? (N - 2 - off) : (1 + off));
    }
    return count;
}

// ── MIDI note for absolute step index (stateless convenience) ───────────────
// A live engine caches pool/scan and rebuilds only on param change; this
// recomputes each call for simplicity/testability.
inline int stepMidi(const ArpParams& p, int stepIndex) noexcept
{
    int pool[kMaxPool];
    const int N = buildPool(p, pool, kMaxPool);
    if (N <= 0) return -1;

    int scan[kMaxScan];
    const int cyc = buildScan(N, p.direction, scan, kMaxScan);
    if (cyc <= 0) return -1;

    int i = stepIndex % cyc;
    if (i < 0) i += cyc;

    const int semi     = pool[scan[i]];
    const int rootMidi = 12 * (p.rootOctave + 1) + p.rootNote;  // C4 = 60
    return rootMidi + semi;
}

} // namespace mu_toni
