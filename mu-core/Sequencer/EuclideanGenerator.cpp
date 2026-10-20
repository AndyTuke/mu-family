#include "EuclideanGenerator.h"

#include <algorithm>

namespace mu_core {

namespace {

constexpr int kMaxLevels = 32;   // levels needed grow with log(steps) (worst case Fibonacci steps), far below this

// How each Bjorklund level is built from the one below it.
struct Plan
{
    bool leftoverIsRest[kMaxLevels] {};   // is the level's rest-word the previous rest-word (else the previous hit-word)?
    int  restsPerHit[kMaxLevels] {};      // rest-words glued after the previous hit-word to make this level's hit-word
    int  levels = 0;
};

// Writes the hit-word (or rest-word) of the given level at out[pos..] and returns the next write position.
size_t emit(const Plan& plan, int level, bool isRest, std::vector<bool>& out, size_t pos)
{
    if (level == 0)
    {
        out[pos] = ! isRest;
        return pos + 1;
    }

    const int below = level - 1;
    if (isRest)
        return emit(plan, below, plan.leftoverIsRest[below], out, pos);

    pos = emit(plan, below, false, out, pos);
    for (int i = 0; i < plan.restsPerHit[below]; ++i)
        pos = emit(plan, below, true, out, pos);
    return pos;
}

} // namespace

std::vector<bool> EuclideanGenerator::generate(int steps, int hits)
{
    std::vector<bool> pattern;
    generate(steps, hits, pattern);
    return pattern;
}

void EuclideanGenerator::generate(int steps, int hits, std::vector<bool>& out)
{
    steps = std::max(steps, 1);
    hits  = std::clamp(hits, 0, steps);

    out.assign((size_t)steps, false);
    if (hits == 0)
        return;

    // Bjorklund's pair-up algorithm (Bjorklund 2003; Toussaint 2005 "The Euclidean Algorithm
    // Generates Traditional Musical Rhythms"). Words start as hit ("x") and rest (".") with
    // counts hits / rests; each round glues one hit-word to one rest-word, and whatever is left
    // over becomes the new rest-word. Rounds stop once at most one rest-word remains. A word at
    // level L is described by the two level L-1 words it is built from, so the pattern is emitted
    // by a shallow recursion with no storage (audio-thread safe). This is the textbook phase:
    // E(3,8) = x..x..x. (tresillo), E(5,8) = x.xx.xx. (cinquillo).
    Plan plan;
    int hitWords = hits, restWords = steps - hits;
    while (restWords > 0 && (plan.levels == 0 || restWords > 1) && plan.levels < kMaxLevels)
    {
        if (restWords >= hitWords)
        {
            // Every hit-word takes rest-words until fewer rests than hits are left (one pass per rest-word
            // in the textbook version; collapsed here so the number of levels stays logarithmic).
            plan.leftoverIsRest[plan.levels] = true;
            plan.restsPerHit[plan.levels++]  = restWords / hitWords;
            restWords %= hitWords;
        }
        else
        {
            // More hit-words than rest-words: each rest-word pairs with one hit-word, the spare hit-words become the rests.
            plan.leftoverIsRest[plan.levels] = false;
            plan.restsPerHit[plan.levels++]  = 1;
            const int paired = restWords;
            restWords = hitWords - paired;
            hitWords  = paired;
        }
    }

    size_t pos = 0;
    for (int i = 0; i < hitWords; ++i)  pos = emit(plan, plan.levels, false, out, pos);
    for (int i = 0; i < restWords; ++i) pos = emit(plan, plan.levels, true,  out, pos);
}

} // namespace mu_core
