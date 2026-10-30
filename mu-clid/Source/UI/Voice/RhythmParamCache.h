#pragma once

#include "Control/ParamActual.h"

#include <string_view>
#include <unordered_map>

namespace mu_clid
{

// Parameter pointers of one rhythm, keyed by the id suffix ("ampLvl", "fltCut", ...) the caller passes as a
// string literal. Clear it when the subsection is pointed at another rhythm: the pointers belong to the old ids.
using RhythmParamCache = std::unordered_map<std::string_view, juce::RangedAudioParameter*>;

// Writes `v` (a real value) to rhythm `rhythmIndex`'s "r<N>_<suffix>" parameter. A knob drag fires many events,
// so the pointer is looked up once and kept in `cache`. Does nothing without a rhythm (index < 0).
inline void setRhythmParamCached(juce::AudioProcessorValueTreeState& apvts, int rhythmIndex,
                                 RhythmParamCache& cache, const char* suffix, float v)
{
    if (rhythmIndex < 0)
        return;
    auto it = cache.find(suffix);
    if (it == cache.end())
        it = cache.emplace(suffix, apvts.getParameter("r" + juce::String(rhythmIndex) + "_" + suffix)).first;
    mu_core::setParameterActual(it->second, v);
}

// The same without a cache, for a section that writes a few parameters on discrete clicks.
inline void setRhythmParam(juce::AudioProcessorValueTreeState& apvts, int rhythmIndex, const char* suffix, float v)
{
    if (rhythmIndex < 0)
        return;
    mu_core::setParameterActual(apvts.getParameter("r" + juce::String(rhythmIndex) + "_" + suffix), v);
}

} // namespace mu_clid
