#pragma once

#include <juce_core/juce_core.h>
#include <cstring>
#include <map>
#include <string>

// A preset file's metadata — its root tag plus the root element's attributes — read without
// parsing the body. Every preset list (transport dropdown, preset browser, category lists,
// per-slot filters) reads a folder through this, so listing a folder costs one short scan per
// file instead of a full XML parse (a mu-Clid preset with embedded samples is megabytes of
// base64).
//
// Both family file shapes are understood: the shared full-preset wrapper keeps `category` /
// `description` as root attributes; mu-Clid's presets keep `presetCategory` /
// `presetDescription` / `presetEmbedSamples` as root properties (which serialise as root
// attributes too). `category` and `description` here are whichever the file carries.
namespace mu_pp
{

struct PresetMeta
{
    juce::String rootTag;                            // empty when the file is missing / not XML
    juce::String category, description;              // empty when the file has none
    bool         embedSamples = false;
    std::map<juce::String, juce::String> attributes; // the root's attributes (huge values skipped)

    bool isValid() const noexcept { return rootTag.isNotEmpty(); }
    juce::String attribute(const juce::String& name) const
    {
        const auto it = attributes.find(name);
        return it != attributes.end() ? it->second : juce::String();
    }
};

namespace detail
{
    // Attribute values longer than this (embedded sample data) are skipped, not kept.
    constexpr size_t kMaxMetaValueBytes = 4096;

    inline bool isXmlSpace(int c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    // Reads a stream in blocks; long quoted values are skipped with memchr, not byte by byte.
    class BlockReader
    {
    public:
        explicit BlockReader(juce::InputStream& source) : in(source) {}

        int next()
        {
            if (pos == len && ! refill()) return -1;
            return (int) (unsigned char) buf[pos++];
        }

        // Consume up to and past the closing `quote`, appending to `value` while it stays within
        // `limit` bytes (past that `value` is cleared and `kept` false). False at end of stream.
        bool readQuoted(char quote, std::string& value, size_t limit, bool& kept)
        {
            kept = true;
            for (;;)
            {
                if (pos == len && ! refill()) return false;
                const char* start = buf + pos;
                const auto* hit = static_cast<const char*>(std::memchr(start, quote, (size_t) (len - pos)));
                const size_t n = hit != nullptr ? (size_t) (hit - start) : (size_t) (len - pos);
                if (kept)
                {
                    if (value.size() + n > limit) { kept = false; value.clear(); }
                    else value.append(start, n);
                }
                pos += (int) n;
                if (hit != nullptr) { ++pos; return true; }
            }
        }

    private:
        bool refill()
        {
            len = in.read(buf, (int) sizeof(buf));
            pos = 0;
            if (len > 0) return true;
            len = 0;
            return false;
        }

        juce::InputStream& in;
        char buf[8192];
        int  pos = 0, len = 0;
    };

    // Scan `in` up to the end of the root element's opening tag and return that tag rebuilt as an
    // empty element ("<Tag a=\"v\" .../>") holding only the attributes worth keeping, so JUCE's
    // parser can decode it (entities and all). Empty when no well-formed root tag is found.
    inline std::string scanRootTag(BlockReader& in)
    {
        auto next = [&in]() { return in.next(); };
        int c = 0;

        // Phase 1: skip the prolog (BOM, <?xml ?> declaration, comments) to the root's '<'.
        for (;;)
        {
            do c = next(); while (c != -1 && c != '<');
            if (c == -1) return {};
            c = next();
            if (c == '?')
            {
                for (int prev = 0; (c = next()) != -1 && ! (prev == '?' && c == '>'); prev = c) {}
                continue;
            }
            if (c == '!')
            {
                const int a = next(), b = (a == '>' ? '>' : next());
                if (a == '-' && b == '-')
                    for (int p1 = 0, p2 = 0; (c = next()) != -1 && ! (p1 == '-' && p2 == '-' && c == '>'); p1 = p2, p2 = c) {}
                else
                    for (c = b; c != -1 && c != '>'; c = next()) {}
                continue;
            }
            break;
        }

        // Phase 2: the root's tag name.
        std::string out = "<";
        for (; c != -1 && ! isXmlSpace(c) && c != '>' && c != '/'; c = next())
            out += (char) c;
        if (out.size() < 2) return {};

        // Phase 3: each attribute up to the tag's end, keeping the short ones.
        for (;;)
        {
            while (isXmlSpace(c)) c = next();
            if (c == -1) return {};
            if (c == '>' || c == '/') break;

            std::string name;
            for (; c != -1 && c != '=' && ! isXmlSpace(c) && c != '>' && c != '/'; c = next())
                name += (char) c;
            while (isXmlSpace(c)) c = next();
            if (c != '=' || name.empty()) return {};
            do c = next(); while (isXmlSpace(c));
            if (c != '"' && c != '\'') return {};

            const char quote = (char) c;
            std::string value;
            bool kept = true;
            if (! in.readQuoted(quote, value, kMaxMetaValueBytes, kept)) return {};
            if (kept)
                out += " " + name + "=" + quote + value + quote;
            c = next();
        }
        return out + "/>";
    }
}

// `file`'s metadata (invalid when the file is missing or has no readable root tag). Reads only
// up to the end of the root's opening tag, skipping long values in blocks; uncached, since that
// costs about what checking a cache entry's date and size would.
inline PresetMeta readPresetMeta(const juce::File& file)
{
    PresetMeta meta;
    juce::FileInputStream fis(file);
    if (! fis.openedOk()) return meta;
    detail::BlockReader in(fis);

    const auto tag = detail::scanRootTag(in);
    if (tag.empty()) return meta;
    const auto xml = juce::parseXML(juce::String::fromUTF8(tag.data(), (int) tag.size()));
    if (xml == nullptr) return meta;

    meta.rootTag = xml->getTagName();
    for (int i = 0; i < xml->getNumAttributes(); ++i)
        meta.attributes[xml->getAttributeName(i)] = xml->getAttributeValue(i);
    meta.category     = xml->hasAttribute("category")    ? xml->getStringAttribute("category")
                                                         : xml->getStringAttribute("presetCategory");
    meta.description  = xml->hasAttribute("description") ? xml->getStringAttribute("description")
                                                         : xml->getStringAttribute("presetDescription");
    meta.embedSamples = xml->getIntAttribute("presetEmbedSamples", 0) != 0;
    return meta;
}

} // namespace mu_pp
