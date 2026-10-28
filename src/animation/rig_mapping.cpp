#include "model/model_preview.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace
{
void VisitMainChunks(const PKPackage& package, const std::function<void(const CMChunk&)>& visit)
{
    std::function<void(const CMChunk&)> walk = [&](const CMChunk& chunk) {
        if (chunk.GetMaskedID() == Gen_PostLoadData)
            return;
        visit(chunk);
        for (const auto& child : chunk.children)
            walk(child);
    };
    for (const auto& root : package.rootChunks)
        walk(root);
}

bool SameVariant(const CMChunkResourceHeader& candidate, const CMChunkResourceHeader& model)
{
    return candidate.GetQualityLevel() == model.GetQualityLevel() &&
           (candidate.GetLanguageMask() == model.GetLanguageMask() ||
            (candidate.GetLanguageMask() & model.GetLanguageMask()));
}

int RigScore(const CMChunkResourceHeader& candidate, const CMChunkResourceHeader& model,
             const std::vector<uint32_t>& linkedRigs)
{
    int match = 0;
    if (std::find(linkedRigs.begin(), linkedRigs.end(), candidate.GetCRC()) != linkedRigs.end())
        match = 3;
    else if (candidate.GetCRC() == model.GetCRC())
        match = 2;
    else if (candidate.GetName() == model.GetName())
        match = 1;
    return match ? match * 2 + (candidate.GetLanguageMask() == model.GetLanguageMask()) : -1;
}
} // namespace

void ModelPreview::InitializeAnimations(const PKPackage& package, const CMChunkResourceHeader& model)
{
    try
    {
        if (!isHierarchy)
        {
            animationSkin = RZGeometrySkin::Load(package, model, geometry);
            skinSummary = SummarizeSkin(*animationSkin);
        }

        std::vector<uint32_t> linkedRigs;
        VisitMainChunks(package, [&](const CMChunk& chunk) {
            if (chunk.GetMaskedID() != GO_Type_Info || chunk.version != 18 || chunk.data.size() < 92)
                return;
            size_t offset = 0;
            if (chunk.Read<uint32_t>(offset) != 0x102)
                return;
            offset = 84;
            const auto hierarchyCRC = chunk.Read<uint32_t>(offset);
            if (chunk.Read<uint32_t>(offset) == model.GetCRC())
                linkedRigs.push_back(hierarchyCRC);
        });

        int bestScore = -1, bestRig = -1;
        bool ambiguous = false;
        VisitMainChunks(package, [&](const CMChunk& chunk) {
            if (chunk.GetMaskedID() != GenSub_Resource)
                return;
            const auto* header = chunk.FindChild(GenSub_ResourceHeader);
            if (!header)
                return;
            const CMChunkResourceHeader candidate(*header);
            if (candidate.GetResourceType() != kCMResourceType_Hierarchy || !SameVariant(candidate, model) ||
                (isHierarchy && candidate.GetCRC() != model.GetCRC()))
                return;
            try
            {
                auto skeleton = RZHierarchy::Load(chunk, package.isLittleEndian);
                if (animationSkin)
                    animationSkin->Validate(skeleton);
                animationRigs.push_back(std::move(skeleton));
            }
            catch (const std::exception&)
            {
                return; // Unsupported hierarchy or palette; not a valid manual choice.
            }
            const int score = RigScore(candidate, model, linkedRigs);
            if (score > bestScore)
            {
                bestScore = score;
                bestRig = int(animationRigs.size() - 1);
                ambiguous = false;
            }
            else if (score >= 0 && score == bestScore)
                ambiguous = true;
        });
        if (bestRig >= 0 && !ambiguous)
            SelectAnimationRig(size_t(bestRig));
        else if (!animationRigs.empty())
            Log("Animation: choose a shared rig in Animation...; no unique model/rig reference was found.");
        else
            Log("Animation: no supported rig in this package.");
    }
    catch (const std::exception& ex)
    {
        Log("Animations unavailable: " + std::string(ex.what()));
    }
}

void ModelPreview::SelectAnimationRig(size_t index)
{
    if (!shown.package || index >= animationRigs.size())
        throw std::runtime_error("Select a rig from the current package.");
    const auto& package = *shown.package;
    const auto& skeleton = animationRigs[index];
    if (animationSkin)
        animationSkin->Validate(skeleton);

    std::vector<AnimationChoice> choices;
    size_t skipped = 0, additive = 0;
    VisitMainChunks(package, [&](const CMChunk& chunk) {
        if (chunk.GetMaskedID() != GenSub_Resource)
            return;
        const auto* header = chunk.FindChild(GenSub_ResourceHeader);
        if (!header || CMChunkResourceHeader(*header).GetResourceType() != kCMResourceType_HAnim)
            return;
        try
        {
            auto clip = RZHAnim::Load(package, chunk, RZHAnim::LoadMode::Metadata);
            const auto map = clip.Map(skeleton);
            if (map.empty() || !std::all_of(map.begin(), map.end(), [](int bone) { return bone >= 0; }))
                return;
            if (clip.IsAdditive())
                ++additive;
            else
                choices.push_back({std::move(clip), &chunk});
        }
        catch (const std::exception&)
        {
            ++skipped;
        }
    });

    animationSkeleton = skeleton;
    boneNames.clear();
    for (const auto& bone : skeleton.bones)
        boneNames.push_back(bone.name);
    ResetAnimation();
    if (!isHierarchy && !bindGeometry)
        bindGeometry = geometry;
    selectedRig = int(index);
    animationChoices = std::move(choices);
    Log("Animation rig " + skeleton.name + ": " + std::to_string(animationChoices.size()) +
        " bone-compatible clips for " + std::to_string(skeleton.bones.size()) + " bones.");
    if (additive)
        Log("Skipped " + std::to_string(additive) + " additive clips that require a base animation.");
    if (skipped)
        Log("Skipped " + std::to_string(skipped) + " animation resources with invalid metadata.");
}
