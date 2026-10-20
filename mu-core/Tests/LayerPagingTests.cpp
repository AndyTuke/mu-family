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
            expectEquals(mixer.getNumChannelPages(), 1);
            expectEquals(mixer.getNumPages(), 1 + (mu_limits::kMaxBuses > 0 ? 1 : 0), "the Buses page follows the channel pages where there are buses");
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
        const int busPages = mu_limits::kMaxBuses > 0 ? 1 : 0;
        expectEquals(mixer.getNumChannelPages(), kPages);
        expectEquals(mixer.getNumPages(), kPages + busPages);
        expect(! mixer.showAdjacentPage(-1));
        expect(mixer.showAdjacentPage(1));
        expectEquals(mixer.getShownPage(), 1);
        mixer.showPage(kPages - 1);
        expect(! mixer.showAdjacentPage(1), "PageDown stops at the last channel page: the Buses page is reached by click");
        mixer.showPage(77);
        expectEquals(mixer.getShownPage(), kPages + busPages - 1);
        if (busPages > 0)
        {
            expect(mixer.isBusPage(mixer.getShownPage()), "the last page is the Buses page");
            expect(! mixer.showAdjacentPage(-1), "no paging off the Buses page");
            mixer.showPage(0);
            expect(! mixer.isBusPage(mixer.getShownPage()));
        }

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

// The Buses page (design-ui.md 5.4): with group buses the mixer's last page replaces the channel strips and the
// returns with one unit per bus (strip + insert column) and keeps the Master where it was.
struct MixerBusPageTests : juce::UnitTest
{
    MixerBusPageTests() : juce::UnitTest("MixerOverlay Buses page: one strip and insert column per bus, the Master stays", "mu-core") {}

    static int visibleStrips(MixerOverlay& mixer, std::vector<juce::Rectangle<int>>* bounds = nullptr)
    {
        int n = 0;
        for (auto* c : mixer.getChildren())
            if (dynamic_cast<MixerChannel*>(c) != nullptr && c->isVisible())
            {
                ++n;
                if (bounds != nullptr) bounds->push_back(c->getBounds());
            }
        return n;
    }

    void runTest() override
    {
        if (mu_limits::kMaxBuses == 0)
        {
            beginTest("(the Buses page exists in a build with group buses)");
            MixerOverlay* none = nullptr;
            expect(none == nullptr);
            return;
        }

        beginTest("the Buses page shows the bus strips and the Master; the channel pages show the channels, returns and Master");
        auto box = std::make_unique<PagedHost>(4); auto& p = *box;
        MixerOverlay mixer(p, p.mixerEngine);
        mixer.setBounds(0, 0, 1088, 800);

        expectEquals(visibleStrips(mixer), kPerPage + 3 + 1, "channel page: a page of channel strips, 3 returns, the Master");
        mixer.showPage(mixer.getNumPages() - 1);
        expect(mixer.isBusPage(mixer.getShownPage()));
        std::vector<juce::Rectangle<int>> bounds;
        expectEquals(visibleStrips(mixer, &bounds), mu_limits::kMaxBuses + 1, "Buses page: one strip per bus and the Master");

        // Nothing overlaps and everything lies inside the overlay; the strips are at least a channel strip wide.
        for (size_t i = 0; i < bounds.size(); ++i)
        {
            expect(mixer.getLocalBounds().contains(bounds[i]), "strip " + juce::String((int) i) + " inside the overlay");
            for (size_t j = i + 1; j < bounds.size(); ++j)
                expect(! bounds[i].intersects(bounds[j]), "strips " + juce::String((int) i) + " and " + juce::String((int) j) + " do not overlap");
        }
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
            expect(bounds[i].getWidth() >= MuLookAndFeel::kMixerChanW, "a bus strip is at least a channel strip wide");

        mixer.showPage(0);
        expectEquals(visibleStrips(mixer), kPerPage + 3 + 1, "back on a channel page");
    }
};
static MixerBusPageTests sMixerBusPageTests;
