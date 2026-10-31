// Signalsmith Stretch prototype for mu-Tilate (docs/mu-tilate/design-engine.md, "How the stretcher is driven").
//
// Question 1: can a varying LOCAL rate (play the slice start unstretched for a hold, ramp to a plateau rate that
//   makes the slice land on its target length) be driven by feeding a varying input count per output block, without
//   smeared attacks or flutter at the hold-to-ramp join, compared with a uniform stretch?
// Question 2: does outputSeek hide the stretcher's latency, so the first output sample is the slice's first sample?
// Question 3: what does it cost: CPU per voice (build this in Release) and heap allocation on the audio path.
//
// Standalone: no JUCE, no mu-core. Build and run: see tests/prototypes/stretch/README.md.

#include "signalsmith-stretch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// ── Allocation counting (the audio path must not allocate) ──────────────────────────────────────────────────
static std::atomic<long>   gAllocs { 0 };
static std::atomic<size_t> gBytes  { 0 };
static bool                gCounting = false;
void* operator new(size_t n)                 { if (gCounting) { ++gAllocs; gBytes += n; } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n)               { if (gCounting) { ++gAllocs; gBytes += n; } if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void  operator delete(void* p) noexcept      { std::free(p); }
void  operator delete[](void* p) noexcept    { std::free(p); }
void  operator delete(void* p, size_t) noexcept   { std::free(p); }
void  operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
using Stretch = signalsmith::stretch::SignalsmithStretch<float>;
using Buf = std::vector<float>;

// ── Test material: a slice with a hard transient and a decaying body ────────────────────────────────────────
Buf makeKick(int n)
{
    Buf b((size_t) n);
    double phase = 0.0;
    unsigned seed = 1;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        phase += 2.0 * kPi * (48.0 + 110.0 * std::exp(-t * 28.0)) / kSr;
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((seed >> 9) / 4194304.0) - 1.0;
        b[(size_t) i] = (float) (0.9 * std::sin(phase) * std::exp(-t * 7.0) + 0.5 * noise * std::exp(-t * 900.0));
    }
    return b;
}
Buf makeSnare(int n)
{
    Buf b((size_t) n);
    unsigned seed = 7;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((seed >> 9) / 4194304.0) - 1.0;
        b[(size_t) i] = (float) (0.6 * noise * std::exp(-t * 22.0) + 0.5 * std::sin(2.0 * kPi * 190.0 * t) * std::exp(-t * 30.0));
    }
    return b;
}
Buf makePad(int n)   // a soft start: the case where transient protection should be turned down
{
    Buf b((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const double env = (1.0 - std::exp(-t * 40.0)) * std::exp(-t * 2.5);
        b[(size_t) i] = (float) (0.3 * env * (std::sin(2 * kPi * 220.0 * t) + std::sin(2 * kPi * 277.2 * t + 0.4) + std::sin(2 * kPi * 330.0 * t + 0.9)));
    }
    return b;
}

// ── The two drive profiles: how many input samples each output block consumes ───────────────────────────────
struct Profile
{
    std::string name;
    double holdMs = 0, rampMs = 0;   // 0/0 = uniform
    bool   dryHead = false;          // fallback: play holdMs of the source unstretched, cross-fade rampMs into a uniform stretch
};

// dIn/dOut at output sample t: 1 in the hold, a linear ramp to the plateau p, then p.
double localRate(const Profile& pr, double t, double p)
{
    const double hold = pr.holdMs * 0.001 * kSr, ramp = pr.rampMs * 0.001 * kSr;
    if (t < hold) return 1.0;
    if (t < hold + ramp && ramp > 0) return 1.0 + (p - 1.0) * ((t - hold) / ramp);
    return p;
}
// The plateau rate that consumes exactly Lin input over Lout output.
double solvePlateau(const Profile& pr, int Lin, int Lout)
{
    const double hold = pr.holdMs * 0.001 * kSr, ramp = pr.rampMs * 0.001 * kSr;
    if (hold + ramp <= 0) return (double) Lin / Lout;
    return ((double) Lin - hold - ramp * 0.5) / ((double) Lout - hold - ramp * 0.5);
}

struct Render
{
    Buf out;
    double joinFlutter = 0;   // worst short-term / trend ratio around the hold-to-ramp and ramp-to-plateau joins
    long allocsInProcess = 0;
};

// Renders one slice to exactly Lout samples. useSeek: align the first output sample to the slice start with outputSeek.
Render renderSlice(Stretch& st, const Buf& slice, int Lout, const Profile& pr, bool useSeek, int block)
{
    const int Lin = (int) slice.size();
    const double p = solvePlateau(pr, Lin, Lout);
    Render r;
    r.out.assign((size_t) Lout, 0.0f);

    // Input is the slice followed by silence (the next slice would follow in a loop; silence keeps this clean).
    Buf in(slice);
    in.resize((size_t) Lin + 8 * (size_t) st.blockSamples(), 0.0f);

    gAllocs = 0;
    gCounting = true;   // everything from here is the audio-thread path: reset, outputSeek and process
    st.reset();
    int inPos = 0;
    if (useSeek)
    {
        // Pre-roll: the stretcher consumes the first outputSeekLength() input samples to line its output up with the slice start.
        const float* ch[1] = { in.data() };
        const int n = std::min((int) in.size(), st.outputSeekLength(1.0f / (float) p));
        st.outputSeek(ch, n);
        inPos = n;
    }

    double inAccum = (double) inPos;   // fractional input position, error-diffused into integer counts
    int outPos = 0;
    while (outPos < Lout)
    {
        const int nOut = std::min(block, Lout - outPos);
        const double t = outPos + 0.5 * nOut;
        // Input position the plan wants at the end of this block, by integrating the local rate over the block.
        double step = 0.0;
        for (int k = 0; k < nOut; k += 16) step += localRate(pr, outPos + k, p) * std::min(16, nOut - k);
        inAccum += step;
        int want = (int) std::lround(inAccum);
        want = std::min(want, (int) in.size());
        const int nIn = std::max(0, want - inPos);
        const float* inCh[1] = { in.data() + inPos };
        float* outCh[1]      = { r.out.data() + outPos };
        st.process(inCh, nIn, outCh, nOut);
        inPos += nIn;
        outPos += nOut;
        (void) t;
    }
    gCounting = false;
    r.allocsInProcess = gAllocs.load();
    return r;
}


// The fallback drive: the slice start is the source itself for holdMs, then a rampMs cross-fade into the uniform stretch.
void applyDryHead(Render& r, const Buf& slice, const Profile& pr)
{
    const int hold = (int) (pr.holdMs * 0.001 * kSr), xf = std::max(1, (int) (pr.rampMs * 0.001 * kSr));
    for (int i = 0; i < hold + xf && i < (int) r.out.size() && i < (int) slice.size(); ++i)
    {
        const float w = i < hold ? 0.0f : (float) (i - hold) / (float) xf;   // weight of the stretched signal
        r.out[(size_t) i] = (1.0f - w) * slice[(size_t) i] + w * r.out[(size_t) i];
    }
}

// ── Metrics ─────────────────────────────────────────────────────────────────────────────────────────────────
// Smoothed rectified envelope (about 1 ms).
Buf envelope(const Buf& x)
{
    Buf e(x.size());
    float s = 0;
    const float a = std::exp(-1.0f / (0.001f * (float) kSr));
    for (size_t i = 0; i < x.size(); ++i) { s = a * s + (1 - a) * std::fabs(x[i]); e[i] = s; }
    return e;
}
// Position (ms) of the envelope's peak in the first 60 ms, and its 10 to 90 percent rise time (ms).
void attackOf(const Buf& x, double& peakMs, double& riseMs)
{
    const auto e = envelope(x);
    const size_t lim = std::min(e.size(), (size_t) (0.06 * kSr));
    size_t pk = 0;
    for (size_t i = 0; i < lim; ++i) if (e[i] > e[pk]) pk = i;
    const float top = e[pk];
    size_t i10 = 0, i90 = pk;
    for (size_t i = 0; i <= pk; ++i) { if (e[i] < 0.1f * top) i10 = i; if (e[i] < 0.9f * top) i90 = i; }
    peakMs = pk * 1000.0 / kSr;
    riseMs = (i90 > i10 ? i90 - i10 : 0) * 1000.0 / kSr;
}
// Mean absolute difference (dB) of 8 octave-band energies between two same-length windows: spectral similarity.
double bandDistanceDb(const Buf& a, size_t offA, const Buf& b, size_t offB, size_t n)
{
    auto bands = [&](const Buf& x, size_t off)
    {
        std::vector<double> e(8, 1e-12);
        for (int band = 0; band < 8; ++band)
        {
            const double f = 100.0 * std::pow(2.0, band);
            double re = 0, im = 0;   // single-bin DFT at the band centre (cheap, enough for a similarity check)
            for (size_t i = 0; i < n && off + i < x.size(); ++i)
            {
                const double w = 0.5 - 0.5 * std::cos(2 * kPi * (double) i / (double) n);
                re += w * x[off + i] * std::cos(2 * kPi * f * (double) i / kSr);
                im += w * x[off + i] * std::sin(2 * kPi * f * (double) i / kSr);
            }
            e[(size_t) band] = re * re + im * im + 1e-12;
        }
        return e;
    };
    const auto ea = bands(a, offA), eb = bands(b, offB);
    double s = 0;
    for (size_t i = 0; i < 8; ++i) s += std::fabs(10.0 * std::log10(ea[i] / eb[i]));
    return s / 8.0;
}
// Worst ratio of the 2 ms RMS to the 12 ms RMS around `centreMs`: a dip or burst at a join shows as > 1. Windows that
// would reach before the start of the buffer are skipped.
double flutterAround(const Buf& x, double centreMs)
{
    const long c = (long) (centreMs * 0.001 * kSr), w = (long) (0.012 * kSr), s = (long) (0.002 * kSr);
    auto rms = [&](long a, long n) { double acc = 0; for (long i = a; i < a + n; ++i) acc += x[(size_t) i] * x[(size_t) i]; return std::sqrt(acc / (double) n) + 1e-9; };
    double worst = 1.0;
    for (long a = c - w / 2; a <= c + w / 2; a += s)
    {
        if (a - w / 2 < 0 || a + w / 2 + s > (long) x.size()) continue;
        const double ratio = rms(a, s) / rms(a - w / 2, w);
        worst = std::max(worst, std::max(ratio, 1.0 / ratio));
    }
    return worst;
}
// First instant (ms) the rectified signal passes 10 percent of its peak in the first 60 ms: when the attack arrives.
double onsetMs(const Buf& x)
{
    const size_t lim = std::min(x.size(), (size_t) (0.06 * kSr));
    float pk = 0;
    for (size_t i = 0; i < lim; ++i) pk = std::max(pk, std::fabs(x[i]));
    for (size_t i = 0; i < lim; ++i) if (std::fabs(x[i]) > 0.1f * pk) return i * 1000.0 / kSr;
    return 0;
}
// How well the output's first 12 ms reproduces the source's first 12 ms: best normalised correlation over lags of
// +-3 ms (corr, 1 = identical waveform) and the lag it was found at (ms, positive = output late).
void startMatch(const Buf& out, const Buf& src, double& corr, double& lagMs, double& energyRatio)
{
    const int n = (int) (0.012 * kSr), maxLag = (int) (0.003 * kSr);
    double best = -2; int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < n; ++i)
        {
            const int j = i - lag;
            if (j < 0 || j >= (int) out.size() || i >= (int) src.size()) continue;
            ab += out[(size_t) j] * src[(size_t) i]; aa += out[(size_t) j] * out[(size_t) j]; bb += src[(size_t) i] * src[(size_t) i];
        }
        const double c = ab / (std::sqrt(aa * bb) + 1e-12);
        if (c > best) { best = c; bestLag = lag; }
    }
    corr = best; lagMs = bestLag * 1000.0 / kSr;
    double eo = 0, es = 0;
    const int m = (int) (0.010 * kSr);
    for (int i = 0; i < m; ++i) { eo += out[(size_t) i] * out[(size_t) i]; es += src[(size_t) i] * src[(size_t) i]; }
    energyRatio = eo / (es + 1e-12);
}

struct Row { std::string material, mode; double ratio; double onset, corr, lag, energy, tailDb, flutter; long allocs; };

} // namespace

int main(int argc, char** argv)
{
    const int block = argc > 1 ? std::atoi(argv[1]) : 256;
    const bool splitComputation = argc > 2 ? std::atoi(argv[2]) != 0 : false;
    const int windowPreset = argc > 3 ? std::atoi(argv[3]) : 0;   // 0 default (120 ms block), 1 cheaper (100 ms), 2 short (30 ms block, 7.5 ms hop)
    auto setup = [&](Stretch& x) {
        if (windowPreset == 1) x.presetCheaper(1, (float) kSr, splitComputation);
        else if (windowPreset == 2) x.configure(1, (int) (0.03 * kSr), (int) (0.0075 * kSr), splitComputation);
        else x.presetDefault(1, (float) kSr, splitComputation);
    };
    const int Lin = (int) (0.5 * kSr);

    struct Mat { const char* name; Buf buf; };
    std::vector<Mat> mats { { "kick", makeKick(Lin) }, { "snare", makeSnare(Lin) }, { "pad", makePad(Lin) } };
    const std::vector<Profile> profiles {
        { "uniform", 0, 0 }, { "hold 10 ms, ramp 30 ms", 10, 30 }, { "hold 25 ms, ramp 60 ms", 25, 60 },
        { "dry head 10 ms + 5 ms xfade", 10, 5, true }
    };
    const double ratios[] = { 1.25, 1.6, 0.8 };   // output length / input length: slower, much slower, faster

    // Memory and latency: one stretcher, configured once.
    gAllocs = 0; gBytes = 0; gCounting = true;
    Stretch st(1234L);
    setup(st);
    gCounting = false;
    std::printf("Signalsmith Stretch %d.%d.%d, mono, %.0f Hz, splitComputation=%d, window preset %d, output block %d\n",
                (int) Stretch::version[0], (int) Stretch::version[1], (int) Stretch::version[2], kSr, splitComputation ? 1 : 0, windowPreset, block);
    std::printf("configure: %ld allocations, %.1f KiB; blockSamples %d, intervalSamples %d, inputLatency %d (%.1f ms), outputLatency %d (%.1f ms)\n\n",
                gAllocs.load(), gBytes.load() / 1024.0, st.blockSamples(), st.intervalSamples(),
                st.inputLatency(), st.inputLatency() * 1000.0 / kSr, st.outputLatency(), st.outputLatency() * 1000.0 / kSr);

    std::vector<Row> rows;
    long worstAllocs = 0;
    for (const auto& m : mats)
    {
        std::printf("%-6s original: onset at %.2f ms\n", m.name, onsetMs(m.buf));
        for (double ratio : ratios)
        {
            const int Lout = (int) std::lround(Lin * ratio);
            for (const auto& pr : profiles)
            {
                Profile drive = pr;
                if (pr.dryHead) drive.holdMs = drive.rampMs = 0;   // the stretch under a dry head is uniform
                auto r = renderSlice(st, m.buf, Lout, drive, true, block);
                if (pr.dryHead) applyDryHead(r, m.buf, pr);
                double corr = 0, lag = 0, energy = 0;
                startMatch(r.out, m.buf, corr, lag, energy);
                const double onset = onsetMs(r.out);
                // Tail similarity: a window at 60 percent of the slice, mapped to the same point of the source.
                const size_t winOut = (size_t) (0.6 * Lout), winIn = (size_t) (0.6 * Lin);
                const double tail = bandDistanceDb(r.out, winOut, m.buf, winIn, 4096);
                // The joins sit at hold and hold+ramp (uniform has none: sample the same instants for comparison).
                const double h = pr.holdMs > 0 ? pr.holdMs : 25.0, rr = pr.rampMs > 0 ? pr.rampMs : 60.0;
                const double fl = std::max(flutterAround(r.out, h), flutterAround(r.out, h + rr));
                rows.push_back({ m.name, pr.name, ratio, onset, corr, lag, energy, tail, fl, r.allocsInProcess });
                worstAllocs = std::max(worstAllocs, r.allocsInProcess);
            }
        }
        std::printf("\n");
    }

    std::printf("%-6s %-24s %5s | %8s %6s %7s %7s | %7s %7s | %6s\n", "slice", "drive", "ratio", "onset ms", "corr", "lag ms", "E 10ms", "tail dB", "flutter", "allocs");
    for (const auto& r : rows)
        std::printf("%-6s %-24s %5.2f | %8.2f %6.3f %7.2f %7.2f | %7.2f %7.2f | %6ld\n", r.material.c_str(), r.mode.c_str(), r.ratio, r.onset, r.corr, r.lag, r.energy, r.tailDb, r.flutter, r.allocs);
    std::printf("\nWorst allocations inside process(): %ld\n", worstAllocs);

    // ── CPU: eight voices, ten seconds of output each, a new slice (with outputSeek) every half second ─────────
    const Profile weighted { "hold 10 ms, ramp 30 ms", 10, 30 };
    std::vector<Stretch> voices;
    voices.reserve(8);
    for (int v = 0; v < 8; ++v) { voices.emplace_back((long) (100 + v)); setup(voices.back()); }
    const int Lout = (int) std::lround(Lin * 1.25);
    const int slices = (int) (10.0 * kSr / Lout);
    const auto t0 = std::chrono::steady_clock::now();
    double sink = 0;
    for (int v = 0; v < 8; ++v)
        for (int s = 0; s < slices; ++s)
        {
            const auto r = renderSlice(voices[(size_t) v], mats[(size_t) (s % 2)].buf, Lout, weighted, true, block);
            sink += r.out[(size_t) Lout / 2];
        }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double audioSecs = 8.0 * slices * Lout / kSr;
    std::printf("\nCPU: %d voices x %.1f s output = %.1f s of audio rendered in %.3f s: %.1f %% of one core for all voices, %.2f %% per voice (sink %.3f)\n",
                8, slices * Lout / kSr, audioSecs, secs, 100.0 * secs / (audioSecs / 8.0), 100.0 * secs / audioSecs, sink);
    return 0;
}
