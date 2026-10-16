#pragma once
#include "Sequencer/Layer.h"

struct Layer;            // forward declarations of mu-core global types are not definitions
class VoiceEngine;

namespace mu_tant
{
    template <class T> struct Holder { T value {}; };
    enum class Mode : int { A, B };
    class Pattern final : public Holder<int>
    {
    public:
        void render() { struct LocalHelper { int n; } h { 1 }; (void) h; }
        const char* name() const { return "struct NotAType {"; }
    };
    namespace detail { union Bits { float f; int i; }; }
}
