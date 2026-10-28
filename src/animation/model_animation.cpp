#include "imgui.h"
#include "model/model_preview.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void ModelPreview::SelectAnimation(size_t index)
{
    if (!shown.package || !animationSkeleton || index >= animationChoices.size())
        throw std::runtime_error("Select a compatible animation first.");
    auto clip = RZHAnim::Load(*animationChoices[index].package, *animationChoices[index].resource);
    auto placement = AnimationPreviewPlacement(clip, *animationSkeleton);
    if (animationSkin && bindGeometry)
    {
        auto world = animationSkeleton->WorldPose(clip.Sample(*animationSkeleton, 0));
        for (auto& matrix : world)
            matrix = placement * matrix;
        auto firstPose = *bindGeometry;
        animationSkin->Apply(*animationSkeleton, world, *bindGeometry, firstPose);
        placement.m[3][1] -= firstPose.Bounds().mMin.y;
    }
    animationClip = std::move(clip);
    animationPlacement = placement;
    selectedAnimation = int(index);
    animationTime = 0;
    animationPlaying = false;
    ApplyAnimation();
    Log("Animation " + animationClip->name + ": " + std::to_string(animationClip->tracks.size()) +
        " tracks mapped by bone CRC. Actor displacement and morph channels are not emulated.");
}
void ModelPreview::ApplyAnimation()
{
    if (!animationClip || !animationSkeleton)
        return;
    const auto pose = animationClip->Sample(*animationSkeleton, animationTime);
    auto world = animationSkeleton->WorldPose(pose);
    if (animationGrounded)
        for (auto& matrix : world)
            matrix = animationPlacement * matrix;
    UpdateBonePose(world);
    if (!isHierarchy && animationSkin && bindGeometry)
        animationSkin->Apply(*animationSkeleton, world, *bindGeometry, geometry);
    renderDirty = true;
}
void ModelPreview::SetAnimationGrounded(bool enabled)
{
    animationGrounded = enabled;
    ApplyAnimation();
}
void ModelPreview::SetAnimationTime(float time)
{
    if (!animationClip || !std::isfinite(time))
        throw std::runtime_error("No animation or invalid sample time.");
    animationTime = std::clamp(time, 0.f, animationClip->info.duration);
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
            if (animationChoices[i].clip.name.find("Idle") != std::string::npos)
            {
                choice = i;
                break;
            }
        SelectAnimation(choice);
    }
    if (play && animationClip && animationTime >= animationClip->info.duration)
        SetAnimationTime(0);
    animationPlaying = play && animationClip && animationClip->info.duration > 0;
}
void ModelPreview::AdvanceAnimation(float seconds)
{
    if (!animationPlaying || !animationClip)
        return;
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Invalid animation time step.");
    float next = animationTime + seconds;
    if (next >= animationClip->info.duration)
    {
        if (animationLoop)
            next = std::fmod(next, animationClip->info.duration);
        else
        {
            next = animationClip->info.duration;
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
    animationPlacement = BUMatrix{};
    if (bindGeometry)
        geometry.meshes = bindGeometry->meshes;
    if (animationSkeleton)
    {
        std::vector<CMTransforms> bind;
        for (const auto& bone : animationSkeleton->bones)
            bind.push_back(bone.bind);
        UpdateBonePose(animationSkeleton->WorldPose(bind));
    }
    renderDirty = true;
}
void ModelPreview::DrawAnimationControls()
{
    if (animationRigs.empty())
        return;
    if (ImGui::Button("Animation..."))
        ImGui::OpenPopup("Animation playback");
    if (!ImGui::BeginPopup("Animation playback"))
        return;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    const char* rig =
        selectedRig < 0 ? "Choose shared rig..." : animationRigs[size_t(selectedRig)].name.c_str();
    if (ImGui::BeginCombo("Rig", rig))
    {
        for (size_t i = 0; i < animationRigs.size(); ++i)
        {
            ImGui::PushID(int(i));
            const std::string label = i < rigSources.size() && rigSources[i] != shown.package
                                          ? animationRigs[i].name + " (" + RigPackageName() + ")"
                                          : animationRigs[i].name;
            if (ImGui::Selectable(label.c_str(), selectedRig == int(i)))
            {
                try
                {
                    SelectAnimationRig(i);
                }
                catch (const std::exception& ex)
                {
                    Log("Animation rig: " + std::string(ex.what()));
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Choose the shared character rig for costumes without a rig reference. "
                          "Matching palette sizes alone cannot verify bone order.");
    if (animationChoices.empty())
    {
        ImGui::TextDisabled(selectedRig < 0 ? "Choose the model's rig to find clips."
                                            : "No matching clips in this package.");
        ImGui::EndPopup();
        return;
    }
    auto label = [](const std::string& name) { return name.substr(0, name.find('#')); };
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
    const std::string current =
        selectedAnimation < 0 ? "Bind pose" : label(animationChoices[size_t(selectedAnimation)].clip.name);
    if (ImGui::BeginCombo("Clip", current.c_str()))
    {
        if (ImGui::Selectable("Bind pose", selectedAnimation < 0))
            ResetAnimation();
        for (size_t i = 0; i < animationChoices.size(); ++i)
        {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(label(animationChoices[i].clip.name).c_str(), selectedAnimation == int(i)))
            {
                try
                {
                    SelectAnimation(i);
                }
                catch (const std::exception& ex)
                {
                    Log("Animation: " + std::string(ex.what()));
                }
            }
            ImGui::SetItemTooltip("%s", animationChoices[i].clip.name.c_str());
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
        if (!isHierarchy)
        {
            bool grounded = animationGrounded;
            if (ImGui::Checkbox("Align to grid", &grounded))
                SetAnimationGrounded(grounded);
            ImGui::SetItemTooltip(
                "Remove the initial scene position and rotation, then ground the first pose. "
                "Later movement is preserved; disable to see original coordinates.");
        }
        ImGui::BeginDisabled(!animationClip);
        float time = animationTime;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
        if (ImGui::SliderFloat("Seconds", &time, 0.f, animationClip ? animationClip->info.duration : 0.f,
                               "%.2f"))
            SetAnimationTime(time);
        ImGui::EndDisabled();
        if (animationClip)
            ImGui::TextDisabled("%zu tracks / %zu bones", animationClip->tracks.size(),
                                animationSkeleton->bones.size());
    }
    catch (const std::exception& ex)
    {
        animationPlaying = false;
        Log("Animation: " + std::string(ex.what()));
    }
    ImGui::EndPopup();
}
