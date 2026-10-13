// SurfaceMapping: the default generated from a description, user overrides, and their saved form.

#include "TestDescriptions.h"
#include "Surface/SurfaceMapping.h"

class SurfaceMappingTests : public juce::UnitTest
{
public:
    SurfaceMappingTests() : juce::UnitTest("Surface mapping", "MuControl") {}

    void runTest() override
    {
        using namespace mu_control;
        using namespace mu_control::test;
        using T = mu_core::ControlActionType;

        beginTest("default mapping, Launchpad-shaped: top = items, side = layers bottom to top, grid row = layer");
        {
            const SurfaceMapping m(launchpadStyle());
            const auto top = m.actionFor({ ControlKind::Top, 2, 0 });
            expect(top && top->type == T::PerformanceItem && top->index == 2);

            const auto bottomSide = m.actionFor({ ControlKind::Side, 0, 7 });
            expect(bottomSide && bottomSide->type == T::SelectLayer && bottomSide->layer == 0, "the bottom button is layer 1");
            const auto topSide = m.actionFor({ ControlKind::Side, 0, 0 });
            expect(topSide && topSide->layer == 7);

            const auto pad = m.actionFor({ ControlKind::Pad, 5, 7 });
            expect(pad && pad->type == T::LaunchClip && pad->layer == 0 && pad->index == 5, "bottom row, column 6 = layer 1 clip 6");
            const auto padTop = m.actionFor({ ControlKind::Pad, 0, 0 });
            expect(padTop && padTop->layer == 7 && padTop->index == 0);

            expect(! m.actionFor({ ControlKind::Special, 0, 0 }), "no logo action in version 1");
            expect(! m.hasOverrides());
            expectEquals((int) m.bindings().size(), 64 + 8 + 8);
        }

        beginTest("default mapping, mock grid: a 4 x 4 surface with no side buttons");
        {
            const SurfaceMapping m(mockGrid());
            const auto pad = m.actionFor({ ControlKind::Pad, 3, 3 });
            expect(pad && pad->type == T::LaunchClip && pad->layer == 0 && pad->index == 3);
            const auto top = m.actionFor({ ControlKind::Top, 3, 0 });
            expect(top && top->type == T::PerformanceItem && top->index == 3);
            expect(! m.actionFor({ ControlKind::Side, 0, 0 }), "there is no side column to map");
            expect(! m.actionFor({ ControlKind::Pad, 4, 0 }), "a control off the surface has no action");
            expectEquals((int) m.bindings().size(), 16 + 4);
        }

        beginTest("an override replaces the default, unbinds, and is refused off the surface");
        {
            SurfaceMapping m(launchpadStyle());
            mu_core::ControlAction a; a.type = T::TransportToggle;
            expect(m.setOverride({ ControlKind::Special, 0, 0 }, a));
            const auto logo = m.actionFor({ ControlKind::Special, 0, 0 });
            expect(logo && logo->type == T::TransportToggle);

            expect(m.setOverride({ ControlKind::Top, 0, 0 }, mu_core::ControlAction {}));   // type None
            expect(! m.actionFor({ ControlKind::Top, 0, 0 }), "None unbinds the default");

            expect(! m.setOverride({ ControlKind::Pad, 9, 9 }, a), "no such control");
            m.clearOverride({ ControlKind::Top, 0, 0 });
            expect(m.actionFor({ ControlKind::Top, 0, 0 }).has_value(), "clearing returns the default");
        }

        beginTest("overrides survive an XML round trip, including every action field");
        {
            SurfaceMapping m(launchpadStyle());
            mu_core::ControlAction a;
            a.type = T::Parameter; a.paramId = "mstr_lvl"; a.value = 0.25f; a.quantise = mu_core::Quantise::Bar;
            m.setOverride({ ControlKind::Pad, 1, 2 }, a);
            mu_core::ControlAction b; b.type = T::MuteLayer; b.layer = 3;
            m.setOverride({ ControlKind::Side, 0, 4 }, b);
            m.setOverride({ ControlKind::Top, 7, 0 }, mu_core::ControlAction {});   // an unbind is saved too

            SurfaceMapping r(launchpadStyle());
            expectEquals(r.restoreFromXml(m.toXml()), 3);
            const auto pa = r.actionFor({ ControlKind::Pad, 1, 2 });
            expect(pa && pa->type == T::Parameter && pa->paramId == "mstr_lvl" && pa->quantise == mu_core::Quantise::Bar);
            expectWithinAbsoluteError(pa ? pa->value : -1.0f, 0.25f, 0.0001f);
            const auto sb = r.actionFor({ ControlKind::Side, 0, 4 });
            expect(sb && sb->type == T::MuteLayer && sb->layer == 3);
            expect(! r.actionFor({ ControlKind::Top, 7, 0 }), "the unbind came back");
            expect(r.actionFor({ ControlKind::Top, 6, 0 }).has_value(), "untouched controls keep their default");
            expectEquals(r.toXml(), m.toXml(), "saving the restored mapping gives the same text");
        }

        beginTest("a mapping with no overrides saves only a version and the surface id");
        {
            const auto t = SurfaceMapping(mockGrid()).toValueTree();
            expectEquals((int) t.getProperty("version"), SurfaceMapping::kVersion);
            expectEquals(t.getProperty("surface").toString(), juce::String("test-mock-grid"));
            expectEquals(t.getNumChildren(), 0);
        }

        beginTest("an unknown action name is dropped and the other entries still load");
        {
            SurfaceMapping src(launchpadStyle());
            mu_core::ControlAction a; a.type = T::MuteLayer;
            src.setOverride({ ControlKind::Pad, 0, 0 }, a);
            src.setOverride({ ControlKind::Pad, 1, 0 }, a);
            auto tree = src.toValueTree();
            tree.getChild(0).setProperty("action", "somethingNew", nullptr);

            SurfaceMapping m(launchpadStyle());
            expectEquals(m.restoreFrom(tree), 1);
            const auto first = m.actionFor({ ControlKind::Pad, 0, 0 });
            expect(first && first->type == T::LaunchClip, "the unknown entry left the default alone");
            const auto second = m.actionFor({ ControlKind::Pad, 1, 0 });
            expect(second && second->type == T::MuteLayer);
        }

        beginTest("overrides for another surface, a newer or missing version, bad text and off-surface controls are ignored");
        {
            SurfaceMapping src(launchpadStyle());
            mu_core::ControlAction a; a.type = T::MuteLayer;
            src.setOverride({ ControlKind::Pad, 0, 0 }, a);

            SurfaceMapping other(mockGrid());
            expectEquals(other.restoreFromXml(src.toXml()), 0);
            expect(! other.hasOverrides(), "another surface id");

            SurfaceMapping m(launchpadStyle());
            auto newer = src.toValueTree();
            newer.setProperty("version", SurfaceMapping::kVersion + 1, nullptr);
            expectEquals(m.restoreFrom(newer), 0);
            auto unversioned = src.toValueTree();
            unversioned.removeProperty("version", nullptr);
            expectEquals(m.restoreFrom(unversioned), 0);

            expectEquals(m.restoreFromXml("not xml at all"), 0);
            expectEquals(m.restoreFromXml("<Other/>"), 0);

            auto stray = src.toValueTree();
            stray.getChild(0).setProperty("x", 99, nullptr);
            expectEquals(m.restoreFrom(stray), 0, "the off-surface entry is dropped");

            expectEquals(m.restoreFrom(src.toValueTree()), 1);
            expectEquals(m.restoreFromXml(SurfaceMapping(launchpadStyle()).toXml()), 0);
            expect(! m.hasOverrides(), "restoring replaces the earlier overrides");
        }
    }
};

static SurfaceMappingTests surfaceMappingTests;
