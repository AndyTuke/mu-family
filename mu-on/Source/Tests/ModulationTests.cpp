// Unit tests for mu-On's per-lane modulation wiring:
//   • the generic ".prop" proportion-space scale rule (mu-core depthScaleFor) drives a
//     full-depth mod across the whole 0..1 proportion (scale 1.0, not the 100 default),
//   • the per-lane MuOnModDest provider resolves dropdown ids ↔ destination strings and
//     rejects another lane's destinations,
//   • a lane's modulators serialise/deserialise round-trip, dropping foreign destinations.

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Sequencer/VoiceSlot.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Modulation/ModulationAssignment.h"
#include "Modulation/ModulatorSerialise.h"
#include "Modulation/MuOnModDest.h"

using namespace mu_on;

class MuOnModulationTest : public juce::UnitTest
{
public:
    MuOnModulationTest() : juce::UnitTest("mu-on Modulation", "Modulation") {}

    void runTest() override
    {
        beginTest(".prop destination uses proportion-space scale 1.0");
        {
            // One ControlSequence outputting a constant +100 (full positive).
            std::vector<ControlSequence> seqs(1);
            seqs[0].id   = "cs0";
            seqs[0].mode = ControlSequence::Mode::Stepped;
            seqs[0].stepValues.assign(16, 100.0f);

            ModulationMatrix m;
            ModulationAssignment a;
            a.id = "a1"; a.sourceId = "cs0_output"; a.destinationId = "k.tune.prop"; a.depth = 100.0f;
            expect(m.addAssignment(a));

            std::unordered_map<std::string_view, float> pv;
            pv["k.tune.prop"] = 0.25f;
            m.process(seqs, 0.0, pv);

            // amount = src(100) * depth(100) * scale(1.0) * 0.0001 = 1.0 → 0.25 + 1.0.
            expectWithinAbsoluteError(pv["k.tune.prop"], 1.25f, 0.001f);
        }

        beginTest("per-lane destination tables are lane-scoped");
        {
            // b.cut is the 3rd Bass destination (index 2) and backs the b_cut param.
            int n = 0;
            const ModDestEntry* bassT = destsForLane(Bass, n);
            expect(n >= 3);
            expect(std::string(bassT[2].propId) == "b.cut.prop");
            expect(std::string(bassT[2].apvtsId) == "b_cut");

            // A Kick destination is not valid for the Bass lane, and vice-versa.
            expect(isValidLaneDest(Bass, "b.cut.prop"));
            expect(! isValidLaneDest(Bass, "k.tune.prop"));
            expect(isValidLaneDest(Kick, "k.tune.prop"));
        }

        beginTest("per-lane dest tables are pinned to the engine setParams order (integrity guard)");
        {
            // Each lane table's ROW ORDER is silently coupled to three things at once: the
            // 1-based dropdown id (addItem(alias, i+1)), the resolved out[] index, and the
            // positional argument order of that lane's Engine::setParams() (the GrooveVoices
            // dispatch). An insert/reorder/delete in any one would re-route modulation to the
            // wrong engine param with no other failure — so pin the full ordered id list per
            // lane here. If a dest is added, update BOTH this list and the engine dispatch.
            // Counts equal each engine's modulatable arity: Kick 5, Bass 10 (wave choice
            // omitted), Hat/Snare 2, Rumble 9 (bpm transport excluded).
            auto pinLane = [this](int lane, const std::vector<std::string>& expected)
            {
                int n = 0;
                const ModDestEntry* t = destsForLane(lane, n);
                expectEquals(n, (int) expected.size(),
                             "lane " + juce::String(lane) + " dest count");
                for (int i = 0; i < n && i < (int) expected.size(); ++i)
                    expect(std::string(t[i].propId) == expected[(size_t) i],
                           "lane " + juce::String(lane) + " dest[" + juce::String(i)
                               + "] == " + juce::String(expected[(size_t) i]));
            };
            pinLane(Kick,   { "k.tune.prop", "k.ptch.prop", "k.pdec.prop", "k.adec.prop", "k.drive.prop" });
            pinLane(Bass,   { "b.tune.prop", "b.sub.prop", "b.cut.prop", "b.res.prop", "b.env.prop",
                              "b.edec.prop", "b.atk.prop", "b.dec.prop", "b.sus.prop", "b.drive.prop" });
            pinLane(Hat,    { "h.tune.prop", "h.dec.prop" });
            pinLane(Snare,  { "s.tune.prop", "s.dec.prop" });
            pinLane(Rumble, { "r.drive.prop", "r.d1.prop", "r.d2.prop", "r.d3.prop", "r.size.prop",
                              "r.revmix.prop", "r.revlp.prop", "r.cut.prop", "r.res.prop" });
        }

        beginTest("modulator serialise round-trip drops foreign-lane destinations");
        {
            VoiceSlot src;
            ModulationAssignment good;  good.id = "g"; good.sourceId = "cs0_output"; good.destinationId = "s.dec.prop";  good.depth = 50.0f;
            ModulationAssignment alien; alien.id = "x"; alien.sourceId = "cs1_output"; alien.destinationId = "k.tune.prop"; alien.depth = 30.0f;
            expect(src.modulationMatrix.addAssignment(good));
            expect(src.modulationMatrix.addAssignment(alien));

            auto tree = mu_pp::serialiseModulators(src);

            VoiceSlot dst;
            mu_pp::deserialiseModulators(tree, dst, {},
                                         [](const std::string& id) { return isValidLaneDest(Snare, id); });

            // Only the Snare-valid destination survives.
            expectEquals((int) dst.modulationMatrix.getAssignments().size(), 1);
            expect(dst.modulationMatrix.getAssignments().front().destinationId == std::string("s.dec.prop"));
        }
    }
};

static MuOnModulationTest muOnModulationTest;
