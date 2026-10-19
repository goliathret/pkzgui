#include "model/model_preview.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

void ModelPreview::InitializeAnimations(const PKPackage &package, const CMChunkResourceHeader &resource)
{
    try
    {
        std::vector<uint32_t> linkedRigs;
        std::function<void(const CMChunk &)> findLinks = [&](const CMChunk &c) {
            if (c.GetMaskedID() == Gen_PostLoadData)
                return;
            if (c.GetMaskedID() == GO_Type_Info && c.version == 18 && c.data.size() >= 92)
            {
                size_t at = 0;
                const auto kind = c.Read<uint32_t>(at);
                at = 84;
                const auto rig = c.Read<uint32_t>(at);
                const auto model = c.Read<uint32_t>(at);
                if (kind == 0x102 && model == resource.GetCRC())
                    linkedRigs.push_back(rig);
            }
            for (const auto &child : c.children)
                findLinks(child);
        };
        for (const auto &root : package.rootChunks)
            findLinks(root);

        int best = -1, selected = -1;
        bool ambiguous = false;
        std::function<void(const CMChunk &)> findHierarchy = [&](const CMChunk &c) {
            if (c.GetMaskedID() == Gen_PostLoadData)
                return;
            if (c.GetMaskedID() == GenSub_Resource)
                if (const auto *h = c.FindChild(GenSub_ResourceHeader))
                {
                    CMChunkResourceHeader candidate(*h);
                    const auto language = candidate.GetLanguageMask();
                    const auto wanted = resource.GetLanguageMask();
                    if (candidate.GetResourceType() == kCMResourceType_Hierarchy &&
                        candidate.GetQualityLevel() == resource.GetQualityLevel() &&
                        (!isHierarchy || candidate.GetCRC() == resource.GetCRC()) &&
                        (language == wanted || (language & wanted)))
                    {
                        try
                        {
                            auto skeleton = animation::Skeleton::Load(c, package.isLittleEndian);
                            if (!isHierarchy)
                                animation::Skin::Load(package, resource, geometry, skeleton);
                            rigResources.push_back(&c);
                            rigNames.push_back(candidate.GetName());
                            const bool linked = std::find(linkedRigs.begin(), linkedRigs.end(),
                                                          candidate.GetCRC()) != linkedRigs.end();
                            const int match = linked ? 3 : candidate.GetCRC() == resource.GetCRC() ? 2 :
                                              candidate.GetName() == resource.GetName() ? 1 : 0;
                            const int score = match ? match * 2 + (language == wanted) : -1;
                            if (score > best)
                            {
                                best = score;
                                selected = int(rigResources.size() - 1);
                                ambiguous = false;
                            }
                            else if (score >= 0 && score == best)
                                ambiguous = true;
                        }
                        catch (const std::exception &)
                        {
                            // Rigid meshes and incompatible palettes cannot use this rig.
                        }
                    }
                }
            for (const auto &child : c.children)
                findHierarchy(child);
        };
        for (const auto &root : package.rootChunks)
            findHierarchy(root);
        if (selected >= 0 && !ambiguous)
            SelectAnimationRig(size_t(selected));
        else if (!rigResources.empty())
            Log("Animation: choose a shared rig in Animation...; no unique model/rig reference was found.");
        else if (!isHierarchy)
            Log("Animation: no supported skin and rig pair in this package.");
    }
    catch (const std::exception &ex)
    {
        Log("Animations unavailable: " + std::string(ex.what()));
    }
}

void ModelPreview::SelectAnimationRig(size_t index)
{
    if (!shown.package || index >= rigResources.size())
        throw std::runtime_error("Select a rig from the current package.");
    const auto &package = *shown.package;
    auto skeleton = animation::Skeleton::Load(*rigResources[index], package.isLittleEndian);
    std::optional<animation::Skin> skin;
    if (!isHierarchy)
    {
        const auto header = CMChunkResourceHeader::FindOwner(package, *shown.chunk);
        if (!header)
            throw std::runtime_error("Model resource is missing.");
        skin = animation::Skin::Load(package, *header, bindGeometry ? *bindGeometry : geometry, skeleton);
    }
    ResetAnimation();
    animationSkeleton = std::move(skeleton);
    animationSkin = std::move(skin);
    if (!isHierarchy && !bindGeometry)
        bindGeometry = geometry;
    selectedRig = int(index);
    animationChoices.clear();
    animationResources.clear();
    try
    {
        size_t skipped = 0;
        std::function<void(const CMChunk &)> findClips = [&](const CMChunk &c) {
            if (c.GetMaskedID() == Gen_PostLoadData)
                return;
            if (c.GetMaskedID() == GenSub_Resource)
                if (const auto *h = c.FindChild(GenSub_ResourceHeader))
                    if (CMChunkResourceHeader(*h).GetResourceType() == kCMResourceType_HAnim)
                    {
                        try
                        {
                            auto clip = animation::Clip::Load(package, c, false);
                            const auto map = clip.Map(*animationSkeleton);
                            if (!map.empty() &&
                                std::all_of(map.begin(), map.end(), [](int bone) { return bone >= 0; }))
                            {
                                animationChoices.push_back(std::move(clip));
                                animationResources.push_back(&c);
                            }
                        }
                        catch (const std::exception &)
                        {
                            ++skipped;
                        }
                    }
            for (const auto &child : c.children)
                findClips(child);
        };
        for (const auto &root : package.rootChunks)
            findClips(root);
        Log("Animation rig " + animationSkeleton->name + ": " + std::to_string(animationChoices.size()) +
            " bone-compatible clips for " +
            std::to_string(animationSkeleton->bones.size()) +
            " bones (CRC mapping; unmapped bones keep the bind pose).");
        if (skipped)
            Log("Skipped " + std::to_string(skipped) + " animation resources with invalid metadata.");
    }
    catch (const std::exception &ex)
    {
        animationChoices.clear();
        animationResources.clear();
        Log("Animations unavailable: " + std::string(ex.what()));
    }
}

