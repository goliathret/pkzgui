#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

inline std::string RigBaseName(const std::string& name)
{
    std::string out;
    int depth = 0;
    for (const char c : name)
    {
        if (c == '[')
            ++depth;
        else if (c == ']')
            depth = std::max(0, depth - 1);
        else if (!depth)
            out += char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline size_t RigPrefixLength(const std::string& rig, const std::string& model)
{
    const std::string base = RigBaseName(rig), target = RigBaseName(model);
    return !base.empty() && base.size() < target.size() && target.compare(0, base.size(), base) == 0 ? base.size()
                                                                                                   : 0;
}
