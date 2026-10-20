// EuclideanGenerator: the output is the textbook Bjorklund / Toussaint phase (E(3,8) = x..x..x.),
// checked against an independent list-based Bjorklund, Toussaint's published rhythms, and the
// previous generator (every pattern must be a rotation of it, so only the downbeat phase moved).

#include <juce_core/juce_core.h>
#include "Sequencer/EuclideanGenerator.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::string toText(const std::vector<bool>& p)
{
    std::string s;
    for (bool b : p) s += b ? 'x' : '.';
    return s;
}

// Textbook Bjorklund with explicit word lists (Toussaint 2005): pair hit-words with rest-words,
// the leftovers become the new rest-words, and stop when at most one rest-word remains.
std::vector<bool> referenceBjorklund(int steps, int hits)
{
    if (hits <= 0) return std::vector<bool>((size_t) steps, false);
    if (hits >= steps) return std::vector<bool>((size_t) steps, true);

    using Word = std::vector<bool>;
    std::vector<Word> a((size_t) hits, Word{ true }), b((size_t) (steps - hits), Word{ false });
    bool first = true;
    while (first || b.size() > 1)
    {
        first = false;
        const size_t m = std::min(a.size(), b.size());
        std::vector<Word> na, nb;
        for (size_t i = 0; i < m; ++i)
        {
            Word w = a[i];
            w.insert(w.end(), b[i].begin(), b[i].end());
            na.push_back(w);
        }
        const auto& src = a.size() > m ? a : b;
        for (size_t i = m; i < src.size(); ++i) nb.push_back(src[i]);
        a = na;
        b = nb;
    }
    Word out;
    for (auto& w : a) out.insert(out.end(), w.begin(), w.end());
    for (auto& w : b) out.insert(out.end(), w.begin(), w.end());
    return out;
}

// The generator before the phase change (maximally even, error-accumulator phase).
std::vector<bool> previousGenerator(int steps, int hits)
{
    std::vector<bool> out((size_t) steps, false);
    int error = steps - 1;
    for (int i = 0; i < steps; ++i)
    {
        error += hits;
        if (error >= steps) { error -= steps; out[(size_t) i] = true; }
    }
    return out;
}

bool isRotation(const std::vector<bool>& a, std::vector<bool> b)
{
    if (a.size() != b.size()) return false;
    for (size_t r = 0; r < b.size(); ++r)
    {
        if (a == b) return true;
        std::rotate(b.begin(), b.begin() + 1, b.end());
    }
    return a.empty();
}
}

struct EuclideanGeneratorTests : juce::UnitTest
{
    EuclideanGeneratorTests() : juce::UnitTest("EuclideanGenerator", "mu-core") {}

    void runTest() override
    {
        using mu_core::EuclideanGenerator;

        beginTest("Toussaint's published rhythms, in the textbook phase");
        {
            struct Row { int hits, steps; const char* text; };
            const Row table[] = {
                { 2,  5, "x.x.."                    },
                { 3,  4, "x.xx"                     },   // calypso
                { 3,  7, "x.x.x.."                  },
                { 3,  8, "x..x..x."                 },   // tresillo
                { 4,  7, "x.x.x.x"                  },
                { 4,  9, "x.x.x.x.."                },
                { 4, 11, "x..x..x..x."              },
                { 5,  6, "x.xxxx"                   },
                { 5,  7, "x.xx.xx"                  },
                { 5,  8, "x.xx.xx."                 },   // cinquillo
                { 5,  9, "x.x.x.x.x"                },
                { 5, 11, "x.x.x.x.x.."              },
                { 5, 12, "x..x.x..x.x."             },
                { 5, 16, "x..x..x..x..x..."         },
                { 7,  8, "x.xxxxxx"                 },
                { 7, 12, "x.xx.x.xx.x."             },
                { 7, 16, "x..x.x.x..x.x.x."         },
                { 9, 16, "x.xx.x.x.xx.x.x."         },
                { 11, 24, "x..x.x.x.x.x..x.x.x.x.x." },
                { 13, 24, "x.xx.x.x.x.x.xx.x.x.x.x." },
            };
            for (auto& r : table)
                expectEquals(toText(EuclideanGenerator::generate(r.steps, r.hits)), std::string(r.text),
                             "E(" + juce::String(r.hits) + "," + juce::String(r.steps) + ")");
        }

        beginTest("edge cases: no hits, all hits, one hit, one step, clamped arguments");
        {
            expectEquals(toText(EuclideanGenerator::generate(8, 0)), std::string("........"));
            expectEquals(toText(EuclideanGenerator::generate(8, 8)), std::string("xxxxxxxx"));
            expectEquals(toText(EuclideanGenerator::generate(8, 1)), std::string("x......."));
            expectEquals(toText(EuclideanGenerator::generate(1, 1)), std::string("x"));
            expectEquals(toText(EuclideanGenerator::generate(1, 0)), std::string("."));
            expectEquals(toText(EuclideanGenerator::generate(0, 3)), std::string("x"));     // steps clamp to 1
            expectEquals(toText(EuclideanGenerator::generate(4, 9)), std::string("xxxx"));   // hits clamp to steps
            expectEquals(toText(EuclideanGenerator::generate(4, -2)), std::string("...."));
        }

        beginTest("every (hits, steps) up to 80 steps, and sampled larger ones up to 256, equal the reference Bjorklund");
        {
            int mismatches = 0;
            for (int n = 1; n <= 256; n = (n < 80 ? n + 1 : n + 17))
                for (int k = 0; k <= n; k += (n < 80 ? 1 : 3))
                    if (EuclideanGenerator::generate(n, k) != referenceBjorklund(n, k))
                        ++mismatches;
            expectEquals(mismatches, 0);
        }

        beginTest("every pattern is a rotation of the previous generator with the same hit count");
        {
            int notRotation = 0, hitDiffers = 0;
            for (int n = 1; n <= 96; ++n)
                for (int k = 0; k <= n; ++k)
                {
                    const auto now = EuclideanGenerator::generate(n, k);
                    const auto before = previousGenerator(n, k);
                    if (std::count(now.begin(), now.end(), true) != std::count(before.begin(), before.end(), true))
                        ++hitDiffers;
                    if (! isRotation(now, before))
                        ++notRotation;
                }
            expectEquals(hitDiffers, 0);
            expectEquals(notRotation, 0);
        }

        beginTest("the non-allocating overload gives the same pattern into a reused buffer");
        {
            std::vector<bool> scratch;
            scratch.reserve(256);
            for (int n = 1; n <= 64; ++n)
                for (int k = 0; k <= n; ++k)
                {
                    EuclideanGenerator::generate(n, k, scratch);
                    expect(scratch == EuclideanGenerator::generate(n, k));
                }
        }
    }
};

static EuclideanGeneratorTests euclideanGeneratorTests;
