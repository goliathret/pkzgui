#include "imgui.h"
#include "model/model_preview.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

void ModelPreview::InitializeAnimations(const PKPackage &package, const CMChunkResourceHeader &resource)
{
    try
    {
        const CMChunk *hierarchy = nullptr;
        int best = -1;
        std::function<void(const CMChunk &)> findHierarchy = [&](const CMChunk &c) {
            if (c.GetMaskedID() == Gen_PostLoadData)
                return;
            if (c.GetMaskedID() == GenSub_Resource)
                if (const auto *h = c.FindChild(GenSub_ResourceHeader))
                {
                    CMChunkResourceHeader candidate(*h);
                    if (candidate.GetResourceType() == kCMResourceType_Hierarchy &&
                        candidate.GetCRC() == resource.GetCRC() &&
                        candidate.GetQualityLevel() == resource.GetQualityLevel())
                    {
                        const auto language = candidate.GetLanguageMask();
                        const auto wanted = resource.GetLanguageMask();
                        const int score = language == wanted ? 2 : (language & wanted) ? 1 : -1;
                        if (score > best)
                        {
                            hierarchy = &c;
                            best = score;
                        }
                        else if (score >= 0 && score == best)
                            throw std::runtime_error("Ambiguous hierarchy resource.");
                    }
                }
            for (const auto &child : c.children)
                findHierarchy(child);
        };
        for (const auto &root : package.rootChunks)
            findHierarchy(root);
        if (!hierarchy)
            return;
        animationSkeleton = animation::Skeleton::Load(*hierarchy, package.isLittleEndian);
        if (!isHierarchy)
        {
            animationSkin = animation::Skin::Load(package, resource, geometry, *animationSkeleton);
            bindGeometry = geometry;
        }
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
        Log("Animation mapping: " + std::to_string(animationChoices.size()) + " bone-compatible clips for " +
            std::to_string(animationSkeleton->bones.size()) +
            " bones (CRC mapping; unmapped bones keep the bind pose).");
        if (skipped)
            Log("Skipped " + std::to_string(skipped) + " animation resources with invalid metadata.");
    }
    catch (const std::exception &ex)
    {
        animationSkeleton.reset();
        animationSkin.reset();
        bindGeometry.reset();
        animationChoices.clear();
        animationResources.clear();
        Log("Animations unavailable: " + std::string(ex.what()));
    }
}

void ModelPreview::SelectAnimation(size_t index)
{
    if (!shown.package || !animationSkeleton || index >= animationResources.size())
        throw std::runtime_error("Select a compatible animation first.");
    auto clip = animation::Clip::Load(*shown.package, *animationResources[index]);
    animationClip = std::move(clip);
    selectedAnimation = int(index);
    animationTime = 0;
    animationPlaying = false;
    ApplyAnimation();
    Log("Animation " + animationClip->name + ": " + std::to_string(animationClip->tracks.size()) +
        " tracks mapped by bone CRC. Root motion and morph tracks are not applied.");
}
void ModelPreview::ApplyAnimation()
{
    if (!animationClip || !animationSkeleton)
        return;
    const auto pose = animationClip->Sample(*animationSkeleton, animationTime);
    const auto world = animationSkeleton->WorldPose(pose);
    if (isHierarchy)
    {
        bonePositions.clear();
        boneParents.clear();
        for (size_t i = 0; i < world.size(); ++i)
        {
            bonePositions.emplace_back(world[i].m[12], world[i].m[13], world[i].m[14]);
            boneParents.push_back(animationSkeleton->bones[i].parent);
        }
    }
    else if (animationSkin && bindGeometry)
        animationSkin->Apply(*animationSkeleton, world, *bindGeometry, geometry);
    renderDirty = true;
}
void ModelPreview::SetAnimationTime(float time)
{
    if (!animationClip || !std::isfinite(time))
        throw std::runtime_error("No animation or invalid sample time.");
    animationTime = std::clamp(time, 0.f, animationClip->duration);
    ApplyAnimation();
}
void ModelPreview::SetAnimationPlaying(bool play)
{
    if (play && !animationClip)
    {
        if (animationChoices.empty())
            return;
        size_t choice = 0;
        for (size_t i = 0; i < animationChoices.size(); ++i)
            if (animationChoices[i].name.find("Idle") != std::string::npos)
            {
                choice = i;
                break;
            }
        SelectAnimation(choice);
    }
    if (play && animationClip && animationTime >= animationClip->duration)
        SetAnimationTime(0);
    animationPlaying = play && animationClip && animationClip->duration > 0;
}
void ModelPreview::AdvanceAnimation(float seconds)
{
    if (!animationPlaying || !animationClip)
        return;
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Invalid animation time step.");
    float next = animationTime + seconds;
    if (next >= animationClip->duration)
    {
        if (animationLoop)
            next = std::fmod(next, animationClip->duration);
        else
        {
            next = animationClip->duration;
            animationPlaying = false;
        }
    }
    SetAnimationTime(next);
}
void ModelPreview::ResetAnimation()
{
    animationPlaying = false;
    animationTime = 0;
    selectedAnimation = -1;
    animationClip.reset();
    if (bindGeometry)
        geometry.meshes = bindGeometry->meshes;
    if (isHierarchy && animationSkeleton)
    {
        std::vector<animation::Transform> bind;
        for (const auto &bone : animationSkeleton->bones)
            bind.push_back(bone.bind);
        const auto world = animationSkeleton->WorldPose(bind);
        bonePositions.clear();
        for (const auto &matrix : world)
            bonePositions.emplace_back(matrix.m[12], matrix.m[13], matrix.m[14]);
    }
    renderDirty = true;
}
void ModelPreview::DrawAnimationControls()
{
    if (animationChoices.empty())
        return;
    if (ImGui::Button("Animation..."))
        ImGui::OpenPopup("Animation playback");
    if (!ImGui::BeginPopup("Animation playback"))
        return;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 28);
    const char *current =
        selectedAnimation < 0 ? "Bind pose" : animationChoices[size_t(selectedAnimation)].name.c_str();
    if (ImGui::BeginCombo("##Animation clip", current))
    {
        if (ImGui::Selectable("Bind pose", selectedAnimation < 0))
            ResetAnimation();
        for (size_t i = 0; i < animationChoices.size(); ++i)
        {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(animationChoices[i].name.c_str(), selectedAnimation == int(i)))
            {
                try
                {
                    SelectAnimation(i);
                }
                catch (const std::exception &ex)
                {
                    Log("Animation: " + std::string(ex.what()));
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    try
    {
        if (ImGui::Button(animationPlaying ? "Pause" : "Play"))
            SetAnimationPlaying(!animationPlaying);
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            animationPlaying = false;
            if (animationClip)
                SetAnimationTime(0);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &animationLoop);
        ImGui::BeginDisabled(!animationClip);
        float time = animationTime;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
        if (ImGui::SliderFloat("Seconds", &time, 0.f, animationClip ? animationClip->duration : 0.f, "%.2f"))
            SetAnimationTime(time);
        ImGui::EndDisabled();
        if (animationClip)
            ImGui::TextDisabled("%zu tracks / %zu bones", animationClip->tracks.size(),
                                animationSkeleton->bones.size());
    }
    catch (const std::exception &ex)
    {
        animationPlaying = false;
        Log("Animation: " + std::string(ex.what()));
    }
    ImGui::EndPopup();
}
