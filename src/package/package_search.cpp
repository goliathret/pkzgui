#include "package/package_search.h"
#include "Resource/ResourceHeader.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace
{
    std::string Lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return text;
    }

    std::string Hex(uint32_t value)
    {
        std::ostringstream text;
        text << "0x" << std::hex << value;
        return text.str();
    }

    std::optional<CMChunkResourceHeader> ReadResourceHeader(const CMChunk& chunk)
    {
        const auto* header = chunk.FindChild(GenSub_ResourceHeader);
        if (!header)
            return std::nullopt;
        try
        {
            return CMChunkResourceHeader(*header);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }
} // namespace

std::string PackageChunkLabel(const CMChunk& chunk)
{
    if (chunk.GetMaskedID() == GenSub_Resource)
    {
        const auto resource = ReadResourceHeader(chunk);
        if (resource && !resource->GetName().empty())
            return resource->GetName();
    }
    return ToString(chunk.GetIDToEnum()) + " [" + Hex(chunk.GetMaskedID()) + "] v" +
           std::to_string(chunk.version) + (chunk.GetHasChildren() ? " folder " : " leaf ") +
           std::to_string(chunk.length);
}

void PackageSearch::Build(const PKPackage* package)
{
    entries.clear();
    matches.clear();
    if (!package)
        return;
    for (const auto& root : package->rootChunks)
        IndexChunk(root, {});
}

void PackageSearch::IndexChunk(const CMChunk& chunk, const std::string& path)
{
    const auto label = PackageChunkLabel(chunk);
    std::string text = label + " " + ToString(chunk.GetIDToEnum()) + " " + Hex(chunk.GetMaskedID());
    if (const auto resource = ReadResourceHeader(chunk))
    {
        text +=
            " " + resource->GetName() + " " + resource->GetResourceTypeName() + " " + Hex(resource->GetCRC());
    }
    entries.push_back({&chunk, label, path, Lowercase(std::move(text))});

    const auto childPath = path.empty() ? label : path + " / " + label;
    for (const auto& child : chunk.children)
        IndexChunk(child, childPath);
}

void PackageSearch::Filter(const std::string& query)
{
    matches.clear();
    std::istringstream input(Lowercase(query));
    std::vector<std::string> terms;
    for (std::string term; input >> term;)
        terms.push_back(std::move(term));
    if (terms.empty())
        return;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& text = entries[i].text;
        const bool matchesAll = std::all_of(terms.begin(), terms.end(), [&](const std::string& term) {
            return text.find(term) != std::string::npos;
        });
        if (matchesAll)
            matches.push_back(i);
    }
}
