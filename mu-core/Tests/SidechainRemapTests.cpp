// Sidechain sources follow their layer when layers are removed or swapped (mu-core SidechainRemap.h).
// A strip that ducked from a deleted layer must stop ducking; one that ducked from a layer that moved
// must follow it; "off" and the external DAW bus never move. mu-Clid applies the rules to the mixer
// engine's atomics, mu-Tant to the APVTS scSrc parameters (0 = off, 1..8 = channel 0..7, 9 = external).

#include <juce_core/juce_core.h>
#include "Audio/SidechainRemap.h"

class SidechainRemapTest : public juce::UnitTest
{
public:
    SidechainRemapTest() : juce::UnitTest ("Sidechain renumbering", "Mixer") {}

    void runTest() override
    {
        constexpr int N = 8, kExt = 8;

        beginTest ("Removing a layer: its followers go off, higher sources shift down, lower ones stay");
        {
            // Remove layer 2 of 5.
            expectEquals (mu_mix::sidechainAfterRemove(2, 2, N), -1, "ducked from the deleted layer: off");
            expectEquals (mu_mix::sidechainAfterRemove(3, 2, N), 2,  "layer 3 became layer 2");
            expectEquals (mu_mix::sidechainAfterRemove(4, 2, N), 3);
            expectEquals (mu_mix::sidechainAfterRemove(1, 2, N), 1,  "below the removed one: unchanged");
            expectEquals (mu_mix::sidechainAfterRemove(0, 2, N), 0);
            expectEquals (mu_mix::sidechainAfterRemove(-1, 2, N), -1, "off stays off");
            expectEquals (mu_mix::sidechainAfterRemove(kExt, 2, N), kExt, "the external bus never moves");
            expectEquals (mu_mix::sidechainAfterRemove(7, 7, N), -1, "the top layer removed");
            expectEquals (mu_mix::sidechainAfterRemove(7, 0, N), 6, "the first layer removed shifts everything");
        }

        beginTest ("Swapping two layers: followers of one now follow the other");
        {
            expectEquals (mu_mix::sidechainAfterSwap(1, 1, 4, N), 4);
            expectEquals (mu_mix::sidechainAfterSwap(4, 1, 4, N), 1);
            expectEquals (mu_mix::sidechainAfterSwap(2, 1, 4, N), 2, "an uninvolved source stays");
            expectEquals (mu_mix::sidechainAfterSwap(-1, 1, 4, N), -1);
            expectEquals (mu_mix::sidechainAfterSwap(kExt, 1, 4, N), kExt);
        }

        beginTest ("The APVTS encoding (0 off, 1..8 channel 0..7, 9 external) follows the same rules");
        {
            expectEquals (mu_mix::sidechainParamAfterRemove(0, 2, N), 0, "off stays 0");
            expectEquals (mu_mix::sidechainParamAfterRemove(3, 2, N), 0, "param 3 = channel 2 = the deleted layer -> off");
            expectEquals (mu_mix::sidechainParamAfterRemove(4, 2, N), 3, "param 4 = channel 3 -> channel 2 = param 3");
            expectEquals (mu_mix::sidechainParamAfterRemove(2, 2, N), 2, "param 2 = channel 1: below, unchanged");
            expectEquals (mu_mix::sidechainParamAfterRemove(9, 2, N), 9, "external stays 9");
            expectEquals (mu_mix::sidechainParamAfterSwap(2, 1, 4, N), 5, "param 2 = channel 1 -> channel 4 = param 5");
            expectEquals (mu_mix::sidechainParamAfterSwap(5, 1, 4, N), 2);
            expectEquals (mu_mix::sidechainParamAfterSwap(9, 1, 4, N), 9);
        }

        beginTest ("A whole strip table survives a removal: every strip still ducks from the same layer");
        {
            // Layers A B C D E in slots 0..4; B (slot 1) is deleted. Sources by LAYER name before:
            // A: off, B: A, C: B, D: E, E: D.
            int src[5] = { -1, 0, 1, 4, 3 };
            for (int& s : src) s = mu_mix::sidechainAfterRemove(s, 1, N);
            // The strips shift down: A C D E now in slots 0..3.
            const int after[4] = { src[0], src[2], src[3], src[4] };
            expectEquals (after[0], -1, "A: still off");
            expectEquals (after[1], -1, "C ducked from the deleted B: off");
            expectEquals (after[2], 3,  "D ducked from E, which is now slot 3");
            expectEquals (after[3], 2,  "E ducked from D, which is now slot 2");
        }
    }
};

static SidechainRemapTest sidechainRemapTest;
