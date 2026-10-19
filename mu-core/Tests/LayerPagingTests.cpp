// Pages (docs/mu-tronic/design-ui.md 3 and 5): a host with more layers than mu_limits::kLayersPerPage shows the sidebar
// and the mixer's strips a page at a time. The pages are derived from the layer count, so a host of eight or fewer
// layers (every product but mu-Tronic) has one page and no page switch. The paged cases run in the 16-layer build of
// this suite (mu-core-tests-16); in the default build they check that one page changes nothing.

#include "LayerContractFixtures.h"
#include "UI/LayerSidebar.h"
#include "UI/MixerOverlay.h"

using namespace mu_contract_test;

namespace
{
    constexpr int kPerPage = mu_limits::kLayersPerPage;
    constexpr bool kPaged = mu_limits::kMaxLayers > kPerPage;
    constexpr int kPages = (mu_limits::kMaxLayers + kPerPage - 1) / kPerPage;

    // A host with `live` layers on which the sidebar and mixer are built.
    struct PagedHost : FakeLayerHost
    {
        explicit PagedHost(int live) : FakeLayerHost(live) {}
    };
}

struct SidebarPagingTests : juce::UnitTest
{
    SidebarPagingTests() : juce::UnitTest("LayerSidebar pages: derived from the layer count, the selection moves with them", "mu-core") {}

    void runTest() override
    {
        beginTest("a host of up to one page of layers has one page, no page switch and nowhere to page to");
        {
            auto box = std::make_unique<PagedHost>(4); auto& p = *box;
            LayerSidebar sidebar(p, "Layer");
            sidebar.setBounds(0, 0, 82, 700);
            sidebar.refreshItems();
            expectEquals(sidebar.getNumPages(), 1);
            expect(! sidebar.getPageSwitch().isVisible());
            expect(! sidebar.showAdjacentPage(1));
            expect(! sidebar.showAdjacentPage(-1));
            sidebar.setSelectedIndex(3);
            expectEquals(sidebar.getShownPage(), 0);
        }

        if (! kPaged)
        {
            beginTest("(the paged cases run in the 16-layer build)");
            return;
        }

        beginTest("the page switch shows when there is more than a page, one segment per page");
        auto box = std::make_unique<PagedHost>(mu_limits::kMaxLayers); auto& p = *box;
        LayerSidebar sidebar(p, "Layer");
        sidebar.setBounds(0, 0, 82, 700);
        std::vector<int> selected;
        sidebar.onLayerSelected = [&](int i) { selected.push_back(i); };
        sidebar.refreshItems();
        expectEquals(sidebar.getNumPages(), kPages);
        expect(sidebar.getPageSwitch().isVisible());
        expectEquals(dynamic_cast<SegmentControl&>(sidebar.getPageSwitch()).getNumSegments(), kPages);
        expectEquals(sidebar.getShownPage(), 0);

        beginTest("switching page selects the layer at the same position on it, and tells the product");
        sidebar.setSelectedIndex(3);
        selected.clear();
        sidebar.showPage(1);
        expectEquals(sidebar.getShownPage(), 1);
        expectEquals(sidebar.getSelectedIndex(), kPerPage + 3);
        expect(selected.size() == 1 && selected[0] == kPerPage + 3);
        expectEquals(dynamic_cast<SegmentControl&>(sidebar.getPageSwitch()).getSelectedIndex(), 1);
        sidebar.showPage(0);
        expectEquals(sidebar.getSelectedIndex(), 3);

        beginTest("PageUp / PageDown move one page without wrapping");
        expect(! sidebar.showAdjacentPage(-1), "page 1 has no previous page");
        expect(sidebar.showAdjacentPage(1));
        expectEquals(sidebar.getShownPage(), 1);
        for (int i = 1; i < kPages; ++i) sidebar.showAdjacentPage(1);
        expect(! sidebar.showAdjacentPage(1), "the last page has no next page");
        expectEquals(sidebar.getShownPage(), kPages - 1);
        expect(sidebar.showAdjacentPage(-1));

        beginTest("a selection on another page flips the page to it, in both directions");
        sidebar.showPage(0);
        sidebar.setSelectedIndex(kPerPage + 2);
        expectEquals(sidebar.getShownPage(), 1);
        sidebar.setSelectedIndex(1);
        expectEquals(sidebar.getShownPage(), 0);

        beginTest("a page number out of range is clamped");
        sidebar.showPage(99);
        expectEquals(sidebar.getShownPage(), kPages - 1);
        sidebar.showPage(-4);
        expectEquals(sidebar.getShownPage(), 0);
    }
};
static SidebarPagingTests sSidebarPagingTests;

struct MixerPagingTests : juce::UnitTest
{
    MixerPagingTests() : juce::UnitTest("MixerOverlay pages: strips a page at a time, returns and master always", "mu-core") {}

    void runTest() override
    {
        beginTest("one page of layers: no page selector, nowhere to page to");
        {
            auto box = std::make_unique<PagedHost>(4); auto& p = *box;
            MixerOverlay mixer(p, p.mixerEngine);
            mixer.setBounds(0, 0, 1088, 800);
            expectEquals(mixer.getNumPages(), 1);
            expect(! mixer.showAdjacentPage(1));
            expectEquals(mixer.getShownPage(), 0);
        }

        if (! kPaged)
        {
            beginTest("(the paged cases run in the 16-layer build)");
            return;
        }

        beginTest("pages follow the layer count; PageUp / PageDown do not wrap");
        auto box = std::make_unique<PagedHost>(mu_limits::kMaxLayers); auto& p = *box;
        MixerOverlay mixer(p, p.mixerEngine);
        mixer.setBounds(0, 0, 1088, 800);
        expectEquals(mixer.getNumPages(), kPages);
        expect(! mixer.showAdjacentPage(-1));
        expect(mixer.showAdjacentPage(1));
        expectEquals(mixer.getShownPage(), 1);
        mixer.showPage(kPages - 1);
        expect(! mixer.showAdjacentPage(1));
        mixer.showPage(77);
        expectEquals(mixer.getShownPage(), kPages - 1);

        beginTest("the mixer opens on the page the host names");
        int sidebarPage = 1;
        mixer.pageOnOpen = [&] { return sidebarPage; };
        mixer.showPage(0);
        mixer.setVisible(false);
        mixer.setVisible(true);
        expectEquals(mixer.getShownPage(), 1);
        sidebarPage = 0;
        mixer.setVisible(false);
        mixer.setVisible(true);
        expectEquals(mixer.getShownPage(), 0);
    }
};
static MixerPagingTests sMixerPagingTests;
