#include "model/skin_view.h"
#include "model/model_preview.h"
#include "render/gl.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

void WeightRampColor(float weight, float rgb[3])
{
    static constexpr float kStops[5][3] = {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    const float t = std::clamp(weight, 0.0f, 1.0f) * 4.0f;
    const int i = std::min(3, int(t));
    const float f = t - float(i);
    for (int c = 0; c < 3; ++c)
        rgb[c] = kStops[i][c] + (kStops[i + 1][c] - kStops[i][c]) * f;
}

void BoneColor(uint32_t bone, float rgb[3])
{
    const float hue = std::fmod(float(bone) * 0.618034f, 1.0f) * 6.0f;
    const float s = 0.65f, v = 0.95f;
    const float f = hue - std::floor(hue);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const float table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    std::copy_n(table[int(hue) % 6], 3, rgb);
}

std::vector<std::vector<float>> SkinVertexColors(const RZGeometrySkin& skin, SkinShading shading, int bone)
{
    std::vector<std::vector<float>> colors;
    for (const auto& mesh : skin.meshes)
    {
        std::vector<float> out(mesh.size() * 3, 0.0f);
        for (size_t v = 0; v < mesh.size(); ++v)
        {
            float* rgb = &out[v * 3];
            if (shading == SkinShading::BoneWeight)
            {
                float weight = 0;
                for (size_t i = 0; i < 4; ++i)
                    if (int(mesh[v].bones[i]) == bone)
                        weight += mesh[v].weights[i];
                WeightRampColor(weight, rgb);
                continue;
            }
            for (size_t i = 0; i < 4; ++i)
            {
                if (!(mesh[v].weights[i] > 0))
                    continue;
                float color[3];
                BoneColor(mesh[v].bones[i], color);
                for (int c = 0; c < 3; ++c)
                    rgb[c] += color[c] * mesh[v].weights[i];
            }
        }
        colors.push_back(std::move(out));
    }
    return colors;
}

SkinSummary SummarizeSkin(const RZGeometrySkin& skin)
{
    SkinSummary summary;
    summary.boneVertices.resize(skin.requiredBoneCount);
    for (const auto& mesh : skin.meshes)
        for (const auto& influence : mesh)
        {
            ++summary.vertices;
            uint32_t count = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                if (!(influence.weights[i] > 0))
                    continue;
                ++count;
                if (influence.bones[i] >= summary.boneVertices.size())
                    summary.boneVertices.resize(influence.bones[i] + 1);
                ++summary.boneVertices[influence.bones[i]];
            }
            summary.weighted += count > 0;
            summary.maxInfluences = std::max(summary.maxInfluences, count);
        }
    return summary;
}

void ModelPreview::SetSkinShading(SkinShading shading)
{
    if (shading != SkinShading::Material && !animationSkin)
        throw std::runtime_error("This model has no skin weights.");
    skinShading = shading;
    if (shading == SkinShading::BoneWeight && selectedBone < 0 && skinSummary)
    {
        const auto& counts = skinSummary->boneVertices;
        selectedBone = int(std::max_element(counts.begin(), counts.end()) - counts.begin());
    }
    vertexColorsDirty = true;
    renderDirty = true;
}

void ModelPreview::SelectBone(int bone)
{
    if (bone >= int(SkinBoneCount()) && bone >= int(bonePositions.size()))
        throw std::runtime_error("Bone index is outside the rig.");
    selectedBone = std::max(-1, bone);
    vertexColorsDirty = true;
    renderDirty = true;
}

void ModelPreview::SetShowSkeleton(bool show)
{
    showSkeleton = show;
    renderDirty = true;
}

size_t ModelPreview::SkinBoneCount() const
{
    if (animationSkeleton)
        return animationSkeleton->bones.size();
    return animationSkin ? animationSkin->requiredBoneCount : 0;
}

std::string ModelPreview::BoneLabel(size_t bone) const
{
    if (bone < boneNames.size() && !boneNames[bone].empty())
        return boneNames[bone];
    return "bone " + std::to_string(bone);
}

void ModelPreview::UpdateBonePose(const std::vector<BUMatrix>& world)
{
    bonePositions.clear();
    boneParents.clear();
    for (size_t i = 0; i < world.size(); ++i)
    {
        bonePositions.emplace_back(world[i].m[3][0], world[i].m[3][1], world[i].m[3][2]);
        boneParents.push_back(animationSkeleton && i < animationSkeleton->bones.size()
                                  ? animationSkeleton->bones[i].parent
                                  : UINT32_MAX);
    }
}

int ModelPreview::BoneAt(const ImVec2& origin, const ImVec2& size, const ImVec2& point) const
{
    const BUVector3 eye = camera.Eye();
    const Mat4 view = Mat4::LookAt(eye, camera.target, camera.Up());
    const Mat4 projection = Mat4::Perspective(OrbitCamera::kFieldOfView, size.x / std::max(size.y, 1.0f),
                                              OrbitCamera::kNearPlane, camera.distance * 20.0f + 10.0f);
    const Mat4 mvp = projection * view;
    int best = -1;
    float bestDistance = 12.0f;
    for (size_t i = 0; i < bonePositions.size(); ++i)
    {
        const BUVector3& p = bonePositions[i];
        const float x = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
        const float y = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
        const float w = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
        if (w <= 0.0f)
            continue;
        const float dx = point.x - (origin.x + (x / w * 0.5f + 0.5f) * size.x);
        const float dy = point.y - (origin.y + (1.0f - (y / w * 0.5f + 0.5f)) * size.y);
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = int(i);
        }
    }
    return best;
}

std::string ModelPreview::GlbExportSummary() const
{
    if (!animationSkin)
        return "Binary glTF 2.0, static mesh (this model has no skin).";
    if (!animationSkeleton)
        return "Binary glTF 2.0, static mesh: choose the model's rig under Animation... to include bones and weights.";
    std::string text = "Binary glTF 2.0 with the " + animationSkeleton->name + " skeleton (" +
                       std::to_string(animationSkeleton->bones.size()) + " joints) and vertex weights";
    if (animationClip)
        text += ", plus the clip " + animationClip->name.substr(0, animationClip->name.find('#'));
    return text + ".";
}

void ModelPreview::DrawSkinControls()
{
    if (!animationSkin)
        return;
    if (!animationRigs.empty())
        ImGui::SameLine();
    if (ImGui::Button("Skin..."))
        ImGui::OpenPopup(kSkinPopup);
    ImGui::SetItemTooltip("Bone weights and the skeleton over the mesh.");
    if (!ImGui::BeginPopup(kSkinPopup))
        return;

    if (ImGui::Checkbox("Show skeleton", &showSkeleton))
        renderDirty = true;
    ImGui::SetItemTooltip("Bones follow the playing clip. Click a joint in the view to select its bone.");
    if (!animationSkeleton)
        ImGui::TextDisabled("Choose the model's rig under Animation... to see and name its bones.");

    int shading = int(skinShading);
    bool changed = ImGui::RadioButton("Material", &shading, int(SkinShading::Material));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Bone weight", &shading, int(SkinShading::BoneWeight));
    ImGui::SetItemTooltip("The selected bone's weight on every vertex: blue 0, green 0.5, red 1.");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Bone colours", &shading, int(SkinShading::BoneColors));
    ImGui::SetItemTooltip("Every bone in its own colour, blended by weight.");
    if (changed)
        SetSkinShading(SkinShading(shading));

    const auto vertexCountOf = [&](size_t bone) {
        return skinSummary && bone < skinSummary->boneVertices.size() ? skinSummary->boneVertices[bone] : size_t(0);
    };
    const std::string current = selectedBone < 0 ? std::string("None")
                                                 : BoneLabel(size_t(selectedBone)) + " (" +
                                                       std::to_string(vertexCountOf(size_t(selectedBone))) + ")";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
    if (ImGui::BeginCombo("Bone", current.c_str(), ImGuiComboFlags_HeightLarge))
    {
        ImGui::InputTextWithHint("##Bone filter", "Find a bone...", boneFilter, sizeof(boneFilter));
        const std::string filter(boneFilter);
        if (ImGui::Selectable("None", selectedBone < 0))
            SelectBone(-1);
        for (size_t bone = 0; bone < SkinBoneCount(); ++bone)
        {
            const std::string label = BoneLabel(bone);
            const auto match = std::search(label.begin(), label.end(), filter.begin(), filter.end(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
            if (!filter.empty() && match == label.end())
                continue;
            const size_t count = vertexCountOf(bone);
            ImGui::PushID(int(bone));
            if (!count)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable((label + "  (" + std::to_string(count) + ")").c_str(), selectedBone == int(bone)))
                SelectBone(int(bone));
            if (!count)
                ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Vertices weighted to each bone in brackets.");
    if (skinSummary)
        ImGui::Text("%zu of %zu vertices weighted, up to %u bones each", skinSummary->weighted, skinSummary->vertices,
                    skinSummary->maxInfluences);
    ImGui::TextWrapped("%s", GlbExportSummary().c_str());
    ImGui::EndPopup();
}
