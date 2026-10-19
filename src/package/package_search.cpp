#include "package/package_search.h"
#include "Resource/ResourceHeader.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}
std::string Hex(uint32_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}
} // namespace

std::string PackageChunkLabel(const CMChunk& chunk)
{
    if (chunk.GetMaskedID() == GenSub_Resource)
        if (const auto* header = chunk.FindChild(GenSub_ResourceHeader))
            try
            {
                const auto name = CMChunkResourceHeader(*header).GetName();
                if (!name.empty())
                    return name;
            }
            catch (const std::exception&)
            {
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
    std::function<void(const CMChunk&, const std::string&)> visit = [&](const CMChunk& chunk,
                                                                        const std::string& path) {
        auto label = PackageChunkLabel(chunk);
        std::string text = label + " " + ToString(chunk.GetIDToEnum()) + " " + Hex(chunk.GetMaskedID());
        if (const auto* header = chunk.FindChild(GenSub_ResourceHeader))
            try
            {
                const CMChunkResourceHeader resource(*header);
                text += " " + resource.GetName() + " " + resource.GetResourceTypeName() + " " +
                        Hex(resource.GetCRC());
            }
            catch (const std::exception&)
            {
            }
        entries.push_back({&chunk, label, path, Lower(std::move(text))});
        for (const auto& child : chunk.children)
            visit(child, path.empty() ? label : path + " / " + label);
    };
    for (const auto& root : package->rootChunks)
        visit(root, {});
}

void PackageSearch::Filter(const std::string& query)
{
    matches.clear();
    std::istringstream input(Lower(query));
    std::vector<std::string> terms;
    for (std::string term; input >> term;)
        terms.push_back(std::move(term));
    if (terms.empty())
        return;
    for (size_t i = 0; i < entries.size(); ++i)
        if (std::all_of(terms.begin(), terms.end(),
                        [&](const auto& term) { return entries[i].text.find(term) != std::string::npos; }))
            matches.push_back(i);
}
