#include "Surface/SurfaceMapping.h"

namespace mu_control
{

namespace
{
const juce::Identifier kTreeType   { "SurfaceMapping" };
const juce::Identifier kOverride   { "Override" };
const juce::Identifier kVersionId  { "version" };
const juce::Identifier kSurfaceId  { "surface" };

const char* kindName(ControlKind k) noexcept
{
    switch (k) { case ControlKind::Top: return "top"; case ControlKind::Side: return "side";
                 case ControlKind::Special: return "special"; default: break; }
    return "pad";
}

std::optional<ControlKind> kindFromName(const juce::String& n)
{
    if (n == "pad")     return ControlKind::Pad;
    if (n == "top")     return ControlKind::Top;
    if (n == "side")    return ControlKind::Side;
    if (n == "special") return ControlKind::Special;
    return std::nullopt;
}
}

std::vector<SurfaceControlId> controlsOf(const SurfaceDescription& d)
{
    std::vector<SurfaceControlId> out;
    for (int y = 0; y < d.gridHeight; ++y)
        for (int x = 0; x < d.gridWidth; ++x)
            out.push_back({ ControlKind::Pad, x, y });
    for (int i = 0; i < d.buttonCount(ButtonKind::Top); ++i)     out.push_back({ ControlKind::Top, i, 0 });
    for (int i = 0; i < d.buttonCount(ButtonKind::Side); ++i)    out.push_back({ ControlKind::Side, 0, i });
    for (int i = 0; i < d.buttonCount(ButtonKind::Special); ++i) out.push_back({ ControlKind::Special, i, 0 });
    return out;
}

bool hasControl(const SurfaceDescription& d, const SurfaceControlId& id) noexcept
{
    switch (id.kind)
    {
        case ControlKind::Pad:     return id.x >= 0 && id.x < d.gridWidth && id.y >= 0 && id.y < d.gridHeight;
        case ControlKind::Top:     return id.y == 0 && id.x >= 0 && id.x < d.buttonCount(ButtonKind::Top);
        case ControlKind::Side:    return id.x == 0 && id.y >= 0 && id.y < d.buttonCount(ButtonKind::Side);
        case ControlKind::Special: return id.y == 0 && id.x >= 0 && id.x < d.buttonCount(ButtonKind::Special);
    }
    return false;
}

SurfaceMapping::SurfaceMapping(const SurfaceDescription& d) : description(d) {}

std::optional<mu_core::ControlAction> SurfaceMapping::defaultAction(const SurfaceControlId& id) const
{
    if (! hasControl(description, id)) return std::nullopt;

    mu_core::ControlAction a;
    switch (id.kind)
    {
        case ControlKind::Top:   // the top row, left to right: app arrows, preset arrows, clip arrows, mute, play / stop
        {
            using T = mu_core::ControlActionType;
            static constexpr T kTop[] = { T::AppPrev, T::AppNext, T::PerformancePrev, T::PerformanceNext,
                                          T::ClipPrev, T::ClipNext, T::MuteSelectedLayer, T::TransportToggle };
            if (id.x < 0 || id.x >= (int) (sizeof(kTop) / sizeof(kTop[0]))) return std::nullopt;
            a.type = kTop[id.x];
            return a;
        }
        case ControlKind::Side:  // the side column selects layers, top to bottom
            a.type = mu_core::ControlActionType::SelectLayer;
            a.layer = rowToLayer(id.y);
            return a;
        case ControlKind::Pad:   // rows are layers (layer 1 on the top row), columns are clips
            a.type = mu_core::ControlActionType::LaunchClip;
            a.layer = rowToLayer(id.y);
            a.index = id.x;
            return a;
        case ControlKind::Special:
            break;
    }
    return std::nullopt;
}

std::optional<mu_core::ControlAction> SurfaceMapping::actionFor(const SurfaceControlId& id) const
{
    if (! hasControl(description, id)) return std::nullopt;
    const auto it = overrides.find(id);
    if (it != overrides.end())
    {
        if (it->second.type == mu_core::ControlActionType::None) return std::nullopt;   // explicitly unbound
        return it->second;
    }
    return defaultAction(id);
}

std::vector<SurfaceMapping::Binding> SurfaceMapping::bindings() const
{
    std::vector<Binding> out;
    for (const auto& id : controlsOf(description))
        if (const auto a = actionFor(id)) out.push_back({ id, *a });
    return out;
}

bool SurfaceMapping::setOverride(const SurfaceControlId& id, const mu_core::ControlAction& action)
{
    if (! hasControl(description, id)) return false;
    overrides[id] = action;
    return true;
}

void SurfaceMapping::clearOverride(const SurfaceControlId& id) { overrides.erase(id); }

juce::ValueTree SurfaceMapping::toValueTree() const
{
    juce::ValueTree t(kTreeType);
    t.setProperty(kVersionId, kVersion, nullptr);
    t.setProperty(kSurfaceId, description.id, nullptr);
    for (const auto& entry : overrides)
    {
        const auto& id = entry.first;
        const auto& a  = entry.second;
        juce::ValueTree o(kOverride);
        o.setProperty("kind", kindName(id.kind), nullptr);
        o.setProperty("x", id.x, nullptr);
        o.setProperty("y", id.y, nullptr);
        o.setProperty("action", a.type == mu_core::ControlActionType::None ? "none" : mu_core::actionName(a.type), nullptr);
        o.setProperty("layer", a.layer, nullptr);
        o.setProperty("index", a.index, nullptr);
        o.setProperty("paramId", a.paramId, nullptr);
        o.setProperty("value", (double) a.value, nullptr);
        o.setProperty("quantise", mu_core::quantiseName(a.quantise), nullptr);
        t.appendChild(o, nullptr);
    }
    return t;
}

juce::String SurfaceMapping::toXml() const { return toValueTree().toXmlString(); }

int SurfaceMapping::restoreFrom(const juce::ValueTree& tree)
{
    overrides.clear();
    if (! tree.hasType(kTreeType)) return 0;
    const int version = tree.getProperty(kVersionId, 0);
    if (version < 1 || version > kVersion) return 0;
    if (tree.getProperty(kSurfaceId).toString() != description.id) return 0;   // another surface's overrides

    int applied = 0;
    for (const auto& o : tree)
    {
        if (! o.hasType(kOverride)) continue;
        const auto kind = kindFromName(o.getProperty("kind").toString());
        if (! kind) continue;

        // "none" is a deliberate unbind; any other unknown name is dropped.
        const auto name = o.getProperty("action").toString();
        mu_core::ControlAction a;
        a.type = mu_core::actionFromName(name);
        if (a.type == mu_core::ControlActionType::None && name != "none") continue;

        a.layer    = o.getProperty("layer", 0);
        a.index    = o.getProperty("index", -1);
        a.paramId  = o.getProperty("paramId").toString();
        a.value    = (float) (double) o.getProperty("value", 0.0);
        a.quantise = mu_core::quantiseFromName(o.getProperty("quantise").toString());
        if (setOverride({ *kind, (int) o.getProperty("x", 0), (int) o.getProperty("y", 0) }, a)) ++applied;
    }
    return applied;
}

int SurfaceMapping::restoreFromXml(const juce::String& xml)
{
    return restoreFrom(juce::ValueTree::fromXml(xml));   // unparseable text gives an invalid tree: no overrides
}

} // namespace mu_control
