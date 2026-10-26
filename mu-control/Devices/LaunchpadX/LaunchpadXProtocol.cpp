#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include "Devices/LaunchpadX/LaunchpadXPalette.h"
#include <algorithm>

namespace mu_control::launchpadx
{
namespace
{
constexpr uint8_t kSysExStart = 0xF0, kSysExEnd = 0xF7;
constexpr int     kLogoCc     = 99;   // the Novation logo: a Special button in Programmer mode (a press reports CC 99, value 127 / 0)

// F0 00 20 29 02 0C is the Novation Launchpad X SysEx header.
std::vector<uint8_t> withHeader(std::initializer_list<uint8_t> body)
{
    std::vector<uint8_t> m { kSysExStart, 0x00, 0x20, 0x29, 0x02, 0x0C };
    m.insert(m.end(), body);
    m.push_back(kSysExEnd);
    return m;
}

// The 7-bit value the lighting message takes for an 8-bit channel (rounded).
uint8_t to7bit(uint32_t channel8) { return (uint8_t) ((channel8 * 127u + 127u) / 255u); }

// Bytes a short message carries after the status byte, or -1 for a status that is not a short channel message.
int dataBytesOf(uint8_t status)
{
    if (status < 0x80 || status >= 0xF0) return -1;
    const uint8_t type = (uint8_t) (status & 0xF0);
    return (type == 0xC0 || type == 0xD0) ? 1 : 2;
}
}

std::optional<int> indexOf(const SurfaceControlId& id)
{
    switch (id.kind)
    {
        case ControlKind::Pad:
            if (id.x < 0 || id.x > 7 || id.y < 0 || id.y > 7) return std::nullopt;
            return 10 * (8 - id.y) + id.x + 1;
        case ControlKind::Top:
            if (id.x < 0 || id.x > 7 || id.y != 0) return std::nullopt;
            return 91 + id.x;
        case ControlKind::Side:
            if (id.y < 0 || id.y > 7 || id.x != 0) return std::nullopt;
            return 10 * (8 - id.y) + 9;
        case ControlKind::Special:   // the logo
            if (id.x != 0 || id.y != 0) return std::nullopt;
            return kLogoCc;
    }
    return std::nullopt;
}

std::optional<SurfaceControlId> controlOf(int index, bool isCc)
{
    const int row = index / 10, col = index % 10;
    if (! isCc)
    {
        // Pads are notes 11..88 with a column of 1..8.
        if (row < 1 || row > 8 || col < 1 || col > 8) return std::nullopt;
        return SurfaceControlId { ControlKind::Pad, col - 1, 8 - row };
    }
    if (index == kLogoCc) return SurfaceControlId { ControlKind::Special, 0, 0 };
    if (row == 9 && col >= 1 && col <= 8) return SurfaceControlId { ControlKind::Top, col - 1, 0 };
    if (col == 9 && row >= 1 && row <= 8) return SurfaceControlId { ControlKind::Side, 0, 8 - row };
    return std::nullopt;
}

std::vector<uint8_t> programmerMode(bool on)   { return withHeader({ 0x0E, (uint8_t) (on ? 1 : 0) }); }
std::vector<uint8_t> aftertouchOff()           { return withHeader({ 0x0B, 0x02, 0x01 }); }
std::vector<uint8_t> readLayout()              { return withHeader({ 0x00 }); }
std::vector<uint8_t> deviceInquiry()           { return { kSysExStart, 0x7E, 0x7F, 0x06, 0x01, kSysExEnd }; }

bool isInquiryReply(const uint8_t* data, size_t size)
{
    // F0 7E <channel> 06 02 <manufacturer 00 20 29> <family 03 01> <member, version...> F7
    static constexpr uint8_t id[] = { 0x06, 0x02, 0x00, 0x20, 0x29, 0x03, 0x01 };
    if (data == nullptr || size < 3 + sizeof(id) + 1 || data[0] != kSysExStart || data[1] != 0x7E || data[size - 1] != kSysExEnd)
        return false;
    for (size_t i = 0; i < sizeof(id); ++i)
        if (data[3 + i] != id[i]) return false;
    return true;
}

std::vector<uint8_t> encodeInput(const SurfaceControlId& id, bool down)
{
    const auto index = indexOf(id);
    if (! index) return {};
    const auto n = (uint8_t) *index;
    if (id.kind == ControlKind::Pad) return { 0x90, n, (uint8_t) (down ? 100 : 0) };
    return { 0xB0, n, (uint8_t) (down ? 127 : 0) };
}

std::vector<SurfaceEvent> decodeInput(const uint8_t* data, size_t size)
{
    std::vector<SurfaceEvent> events;
    size_t i = 0;
    while (data != nullptr && i < size)
    {
        const uint8_t status = data[i];
        if (status == kSysExStart)
        {
            while (i < size && data[i] != kSysExEnd) ++i;   // skip the whole SysEx
            ++i;
            continue;
        }
        const int n = dataBytesOf(status);
        if (n < 0) { ++i; continue; }                       // stray data or real-time byte
        if (i + 1 + (size_t) n > size) break;               // truncated message
        const uint8_t type = (uint8_t) (status & 0xF0);
        const uint8_t d1 = data[i + 1];
        const uint8_t d2 = n == 2 ? data[i + 2] : (uint8_t) 0;
        i += 1 + (size_t) n;

        if (type == 0x90 || type == 0x80)
        {
            if (const auto id = controlOf(d1, false))
                events.push_back({ *id, (type == 0x90 && d2 > 0) ? SurfaceEventType::Press : SurfaceEventType::Release, 0.0f });
        }
        else if (type == 0xB0)
        {
            if (const auto id = controlOf(d1, true))
                events.push_back({ *id, d2 > 0 ? SurfaceEventType::Press : SurfaceEventType::Release, 0.0f });
        }
    }
    return events;
}

std::vector<std::vector<uint8_t>> lightingMessages(const std::vector<PadLight>& lights, bool rgb)
{
    std::vector<std::vector<uint8_t>> messages;
    for (size_t first = 0; first < lights.size(); first += (size_t) kLightingBatchMax)
    {
        // F0 00 20 29 02 0C 03 <spec>... F7, one spec per light.
        std::vector<uint8_t> m { kSysExStart, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03 };
        const size_t last = std::min(lights.size(), first + (size_t) kLightingBatchMax);
        for (size_t k = first; k < last; ++k)
        {
            const auto& l = lights[k];
            const auto idx = (uint8_t) l.index;
            if (rgb)
            {
                m.insert(m.end(), { (uint8_t) 0x03, idx, to7bit((l.rgb >> 16) & 0xFFu), to7bit((l.rgb >> 8) & 0xFFu), to7bit(l.rgb & 0xFFu) });
                continue;
            }
            const uint8_t pal = nearestPaletteIndex(l.rgb);
            if (l.rgb == 0 || l.mode == LedMode::Static) m.insert(m.end(), { (uint8_t) 0x00, idx, pal });
            else if (l.mode == LedMode::Flash)           m.insert(m.end(), { (uint8_t) 0x01, idx, (uint8_t) 0x00, pal });   // colour B off, colour A lit
            else                                         m.insert(m.end(), { (uint8_t) 0x02, idx, pal });
        }
        m.push_back(kSysExEnd);
        messages.push_back(std::move(m));
    }
    return messages;
}

} // namespace mu_control::launchpadx
