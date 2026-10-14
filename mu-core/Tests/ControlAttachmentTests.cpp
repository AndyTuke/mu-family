// mu_core::ControlAttachment: the factory slot is empty until mu-control fills it, and the handle's
// listeners hear status changes (also through a const reference).

#include "Control/ControlAttachment.h"
#include "Control/LiveState.h"

namespace
{
using namespace mu_core;

struct FakeAttachment : ControlAttachment
{
    ControlStatus status = ControlStatus::Found;
    ControlStatus getStatus() const override { return status; }
    juce::String  getDeviceLabel() const override { return "Fake"; }
    void release() override { status = ControlStatus::Released; notifyStatusChanged(); }
    void connect() override { status = ControlStatus::InUse; notifyStatusChanged(); }
    void closeNow() override { status = ControlStatus::Released; }
};

struct CountingListener : ControlAttachment::Listener
{
    int calls = 0;
    void controlStatusChanged() override { ++calls; }
    void controlAttachmentDestroyed() override { ++destroyed; }
    int destroyed = 0;
};

struct NullSink : ControlSink { bool perform(const ControlAction&) override { return false; } };
}

class ControlAttachmentTests : public juce::UnitTest
{
public:
    ControlAttachmentTests() : juce::UnitTest("Control attachment seam", "ControlAttachment") {}

    void runTest() override
    {
        beginTest("the factory slot is empty by default and is one object");
        {
            expect(! controlAttachmentFactory(), "nothing registers a manager unless mu-control is linked");
            expect(&controlAttachmentFactory() == &controlAttachmentFactory());
        }

        beginTest("a registered factory builds an attachment from the sink, view, look and folder");
        {
            bool called = false;
            controlAttachmentFactory() = [&](ControlSink&, const LiveView&, const SurfaceLook& look, const juce::File&)
            {
                called = true;
                expectEquals(look.stepFlashMs, 150);
                return std::unique_ptr<ControlAttachment>(new FakeAttachment());
            };
            NullSink sink;
            LiveState view;
            auto a = controlAttachmentFactory()(sink, view, SurfaceLook{}, juce::File());
            expect(called && a != nullptr);
            controlAttachmentFactory() = nullptr;   // leave the slot as found
            expect(! controlAttachmentFactory());
        }

        beginTest("status listeners are told on release and connect, and not after removal");
        {
            FakeAttachment a;
            const ControlAttachment& readOnly = a;
            CountingListener l;
            readOnly.addListener(&l);
            a.release();
            expect(a.getStatus() == ControlStatus::Released);
            a.connect();
            expectEquals(l.calls, 2);
            readOnly.removeListener(&l);
            a.release();
            expectEquals(l.calls, 2);
        }

        beginTest("listeners are told when the attachment is destroyed, so a watcher never holds a stale pointer");
        {
            CountingListener l;
            {
                FakeAttachment a;
                a.addListener(&l);
            }
            expectEquals(l.destroyed, 1);
        }
    }
};

static ControlAttachmentTests controlAttachmentTests;
