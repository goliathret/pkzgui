#pragma once
#include "Package/PKPackage.h"
#include <string>
#include <vector>

std::string PackageChunkLabel(const CMChunk& chunk);

class PackageSearch
{
public:
    struct Entry
    {
        const CMChunk* chunk;
        std::string label;
        std::string path;
        std::string text;
    };

    void Build(const PKPackage* package);
    void Filter(const std::string& query);
    size_t Count() const { return matches.size(); }
    const Entry& Result(size_t index) const { return entries.at(matches.at(index)); }

private:
    void IndexChunk(const CMChunk& chunk, const std::string& path);

    std::vector<Entry> entries;
    std::vector<size_t> matches;
};
