#include "model/model_preview.h"
#include "platform/platform.h"
#include "render/gl.h"
#include "texture/texture.h"
#include "ui/widgets.h"
#include "BaseUtils/BUCRC.h"
#include "BaseUtils/BUPng.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr float kMaxFrameWidth = 1920.0f;
    constexpr float kMaxFrameHeight = 1080.0f;
    constexpr float kMeshPalette[][3] = {
        {0.75f, 0.78f, 0.82f}, {0.85f, 0.55f, 0.45f}, {0.45f, 0.70f, 0.55f},
        {0.55f, 0.60f, 0.85f}, {0.80f, 0.70f, 0.40f}, {0.70f, 0.50f, 0.70f},
    };
}

ModelPreview::~ModelPreview()
{
    Clear();
}

void ModelPreview::Update(const PackageSession& session)
{
    const SelectionKey key = SelectionKey::Of(session);
    if (key == shown)
        return;
    const bool sameModel = key.SameChunk(shown);
    const float previousDistance = camera.distance;
    const int previousMaterial = selectedMaterial;
    Clear();
    shown = key;
    if (!key.package || !key.chunk)
        return;

    try
    {
        const auto resource = session.SelectedResource();
        if (!resource)
            return;
        const uint32_t type = resource->GetResourceType();
        if (type != kCMResourceType_Geometry && type != kCMResourceType_Hierarchy)
            return;
        active = true;
        name = resource->GetName();
        if (type == kCMResourceType_Hierarchy)
        {
            LoadHierarchy(*key.package, *resource, key.chunk);
            if (sameModel)
                camera.distance = previousDistance;
            Log("Hierarchy " + name + ": " + std::to_string(boneCount) + " bones.");
        }
        else
        {
            Load(*key.package, *resource);
            if (sameModel)
            {
                camera.distance = previousDistance;
                selectedMaterial = std::clamp(previousMaterial, 0, std::max(0, int(materials.size()) - 1));
            }
            Log("Model " + name + ": " + std::to_string(geometry.meshes.size()) + " meshes, " +
                std::to_string(vertexCount) + " vertices, " + std::to_string(triangleCount) +
                " triangles (static preview).");
        }
    }
    catch (const std::exception& ex)
    {
        geometry = RZGeometry();
        materials.clear();
        vertexCount = triangleCount = 0;
        bonePositions.clear();
        boneParents.clear();
        boneCount = 0;
        isHierarchy = false;
        active = true;
        error = ex.what();
        Log("Preview " + name + ": " + error);
    }
}

void ModelPreview::Load(const PKPackage& package, const CMChunkResourceHeader& resource)
{
    geometry = RZGeometry::Load(package, resource);
    size_t bytes = 0;
    for (const RZGeometry::Mesh& mesh : geometry.meshes)
    {
        bytes += mesh.ByteSize();
        vertexCount += mesh.VertexCount();
        triangleCount += mesh.TriangleCount();
    }
    if (bytes > kGeometryBudget)
        throw std::runtime_error("Model exceeds the 128 MiB preview budget.");

    materials.resize(geometry.materials.size());
    for (size_t i = 0; i < materials.size(); ++i)
        materials[i].packageTexture = RZTexture::FindHeader(package, geometry.materials[i].textureCRC,
            resource.GetLanguageMask(), resource.GetQualityLevel());
    camera.Frame(geometry.Bounds());
}

namespace
{
    uint32_t ReadU32(const uint8_t* p, bool littleEndian)
    {
        if (littleEndian)
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    float ReadF32(const uint8_t* p, bool littleEndian)
    {
        const uint32_t bits = ReadU32(p, littleEndian);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    Mat4 ReadMat4(const uint8_t* p, bool littleEndian)
    {
        Mat4 m;
        for (int i = 0; i < 16; ++i)
            m.m[i] = ReadF32(p + i * 4, littleEndian);
        return m;
    }

    constexpr uint32_t kHierarchyBones = 0x0385;
    constexpr size_t kNameLookupEntry = 8;
    constexpr size_t kBoneRecord = 148;
    constexpr uint32_t kMaxBones = 512;

    const CMChunk* FindChunkRecursive(const CMChunk& node, uint32_t maskedId)
    {
        if (node.GetMaskedID() == maskedId)
            return &node;
        for (const CMChunk& child : node.children)
        {
            if (const CMChunk* found = FindChunkRecursive(child, maskedId))
                return found;
        }
        return nullptr;
    }

    const CMChunk* FindResourceByCrc(const CMChunk& node, uint32_t crc)
    {
        if (node.GetMaskedID() == GenSub_Resource)
        {
            const CMChunk* header = node.FindChild(GenSub_ResourceHeader);
            if (header)
            {
                try
                {
                    if (CMChunkResourceHeader(*header).GetCRC() == crc)
                        return &node;
                }
                catch (const std::exception&)
                {
                }
            }
        }
        for (const CMChunk& child : node.children)
        {
            if (const CMChunk* found = FindResourceByCrc(child, crc))
                return found;
        }
        return nullptr;
    }

    const CMChunk* FindBonesChunk(const PKPackage& package, const CMChunkResourceHeader& resource,
        const CMChunk* selected)
    {
        if (selected)
        {
            if (selected->GetMaskedID() == kHierarchyBones)
                return selected;
            if (const CMChunk* found = FindChunkRecursive(*selected, kHierarchyBones))
                return found;
        }

        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* resourceRoot = FindResourceByCrc(root, resource.GetCRC()))
            {
                if (const CMChunk* found = FindChunkRecursive(*resourceRoot, kHierarchyBones))
                    return found;
            }
        }

        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* found = FindChunkRecursive(root, kHierarchyBones))
                return found;
        }
        return nullptr;
    }
}

void ModelPreview::LoadHierarchy(const PKPackage& package, const CMChunkResourceHeader& resource,
    const CMChunk* selected)
{
    isHierarchy = true;
    const CMChunk* bonesChunk = FindBonesChunk(package, resource, selected);
    if (!bonesChunk)
        throw std::runtime_error("Hierarchy_Bones chunk not found.");

    const std::vector<uint8_t> packageBytes = package.Serialize();
    const size_t headerSize = bonesChunk->HeaderSize();
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (bonesChunk->offset + headerSize + bonesChunk->length <= packageBytes.size())
    {
        payload = packageBytes.data() + bonesChunk->offset + headerSize;
        payloadSize = bonesChunk->length;
    }
    else if (bonesChunk->offset + bonesChunk->length <= packageBytes.size() && bonesChunk->length > headerSize)
    {
        payload = packageBytes.data() + bonesChunk->offset + headerSize;
        payloadSize = bonesChunk->length - headerSize;
    }
    else if (bonesChunk->offset + bonesChunk->length <= packageBytes.size())
    {
        payload = packageBytes.data() + bonesChunk->offset;
        payloadSize = bonesChunk->length;
    }
    if (!payload || payloadSize < 12)
        throw std::runtime_error("Hierarchy_Bones chunk too small.");

    const bool littleEndian = package.isLittleEndian;
    const uint32_t count = ReadU32(payload, littleEndian);
    const size_t needed = 12 + size_t(count) * kNameLookupEntry + size_t(count) * kBoneRecord;
    if (count == 0 || count > kMaxBones || needed > payloadSize)
    {
        const uint32_t countBe = ReadU32(payload, false);
        const size_t neededBe = 12 + size_t(countBe) * kNameLookupEntry + size_t(countBe) * kBoneRecord;
        if (countBe > 0 && countBe <= kMaxBones && neededBe <= payloadSize)
        {
            const uint8_t* records = payload + 12 + size_t(countBe) * kNameLookupEntry;
            std::vector<Mat4> localBind(countBe);
            std::vector<uint32_t> parents(countBe);
            for (uint32_t i = 0; i < countBe; ++i)
            {
                const uint8_t* rec = records + size_t(i) * kBoneRecord;
                localBind[i] = ReadMat4(rec, false);
                parents[i] = ReadU32(rec + 128, false);
            }
            std::vector<Mat4> world(countBe);
            std::vector<bool> computed(countBe, false);
            auto computeWorld = [&](auto&& self, uint32_t index) -> const Mat4& {
                if (computed[index])
                    return world[index];
                const uint32_t parent = parents[index];
                if (parent >= countBe)
                    world[index] = localBind[index];
                else
                    world[index] = self(self, parent) * localBind[index];
                computed[index] = true;
                return world[index];
                };
            bonePositions.resize(countBe);
            boneParents = parents;
            BUAABB bounds;
            bool hasBounds = false;
            for (uint32_t i = 0; i < countBe; ++i)
            {
                const Mat4& w = computeWorld(computeWorld, i);
                bonePositions[i] = BUVector3(w.m[12], w.m[13], w.m[14]);
                if (!hasBounds)
                {
                    bounds.mMin = bounds.mMax = bonePositions[i];
                    hasBounds = true;
                }
                else
                {
                    bounds.mMin.x = std::min(bounds.mMin.x, bonePositions[i].x);
                    bounds.mMin.y = std::min(bounds.mMin.y, bonePositions[i].y);
                    bounds.mMin.z = std::min(bounds.mMin.z, bonePositions[i].z);
                    bounds.mMax.x = std::max(bounds.mMax.x, bonePositions[i].x);
                    bounds.mMax.y = std::max(bounds.mMax.y, bonePositions[i].y);
                    bounds.mMax.z = std::max(bounds.mMax.z, bonePositions[i].z);
                }
            }
            boneCount = countBe;
            camera.Frame(bounds);
            return;
        }
        throw std::runtime_error("Hierarchy_Bones data truncated (count=" + std::to_string(count) +
            ", payload=" + std::to_string(payloadSize) + ").");
    }

    const uint8_t* records = payload + 12 + size_t(count) * kNameLookupEntry;
    std::vector<Mat4> localBind(count);
    std::vector<uint32_t> parents(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* rec = records + size_t(i) * kBoneRecord;
        localBind[i] = ReadMat4(rec, littleEndian);
        parents[i] = ReadU32(rec + 128, littleEndian);
    }

    std::vector<Mat4> world(count);
    std::vector<bool> computed(count, false);
    auto computeWorld = [&](auto&& self, uint32_t index) -> const Mat4& {
        if (computed[index])
            return world[index];
        const uint32_t parent = parents[index];
        if (parent >= count)
            world[index] = localBind[index];
        else
            world[index] = self(self, parent) * localBind[index];
        computed[index] = true;
        return world[index];
        };

    bonePositions.resize(count);
    boneParents = parents;
    BUAABB bounds;
    bool hasBounds = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const Mat4& w = computeWorld(computeWorld, i);
        bonePositions[i] = BUVector3(w.m[12], w.m[13], w.m[14]);
        if (!hasBounds)
        {
            bounds.mMin = bounds.mMax = bonePositions[i];
            hasBounds = true;
        }
        else
        {
            bounds.mMin.x = std::min(bounds.mMin.x, bonePositions[i].x);
            bounds.mMin.y = std::min(bounds.mMin.y, bonePositions[i].y);
            bounds.mMin.z = std::min(bounds.mMin.z, bonePositions[i].z);
            bounds.mMax.x = std::max(bounds.mMax.x, bonePositions[i].x);
            bounds.mMax.y = std::max(bounds.mMax.y, bonePositions[i].y);
            bounds.mMax.z = std::max(bounds.mMax.z, bonePositions[i].z);
        }
    }
    boneCount = count;
    camera.Frame(bounds);
}

void ModelPreview::Clear()
{
    for (MaterialState& material : materials)
    {
        renderer.DeleteTexture(material.texture);
        renderer.DeleteTexture(material.retexture);
    }
    renderer.Release();
    materials.clear();
    geometry = RZGeometry();
    bonePositions.clear();
    boneParents.clear();
    boneCount = 0;
    isHierarchy = false;
    shown = {};
    active = false;
    name.clear();
    error.clear();
    vertexCount = triangleCount = 0;
    selectedMaterial = 0;
    renderDirty = true;
}

void ModelPreview::Draw()
{
    ImGui::TextWrapped("%s", name.c_str());
    if (!error.empty())
    {
        ImGui::TextDisabled("Preview unavailable. See Debug log.");
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 32 || available.y < 32)
        return;

    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const float pixelWidth = available.x * scale.x;
    const float pixelHeight = available.y * scale.y;
    const float fit = std::min({1.0f, kMaxFrameWidth / pixelWidth, kMaxFrameHeight / pixelHeight});
    const int frameWidth = std::max(1, static_cast<int>(pixelWidth * fit));
    const int frameHeight = std::max(1, static_cast<int>(pixelHeight * fit));

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    const bool overlayHasInput = DrawOverlay(origin, available);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##modelview", available);
    HandleCameraInput(overlayHasInput);

    try
    {
        if (renderDirty || !renderer.HasFrame(frameWidth, frameHeight))
        {
            RenderFrame(frameWidth, frameHeight);
            renderDirty = false;
        }
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        Log("Render error: " + error);
        drawList->ChannelsMerge();
        return;
    }

    drawList->ChannelsSetCurrent(0);
    drawList->AddImage(static_cast<ImTextureID>(renderer.FrameTexture()), origin,
        ImVec2(origin.x + available.x, origin.y + available.y), ImVec2(0, 1), ImVec2(1, 0));
    drawList->ChannelsMerge();
}

void ModelPreview::DrawFields() const
{
    if (!active || !error.empty())
        return;
    if (isHierarchy)
    {
        FieldRow("Hierarchy", "string", name);
        FieldRow("bones", "u32", std::to_string(boneCount));
        return;
    }
    FieldRow("Model", "string", name);
    FieldRow("meshes", "u32", std::to_string(geometry.meshes.size()));
    FieldRow("vertices", "u32", std::to_string(vertexCount));
    FieldRow("triangles", "u32", std::to_string(triangleCount));
}

std::vector<std::string> ModelPreview::TakeLogMessages()
{
    return std::exchange(logMessages, {});
}

void ModelPreview::SetTextured(bool enabled)
{
    textured = enabled;
    renderDirty = true;
}

void ModelPreview::LoadRetexture(size_t index, const std::string& path)
{
    if (index >= materials.size())
        throw std::runtime_error("Select a material first.");
    const auto png = ReadFile(std::filesystem::u8path(path));
    const BUPng::Image image = BUPng::Decode(png);
    MaterialState& material = materials[index];
    ModelRenderer::Texture texture = renderer.UploadTexture(image.rgba, image.width, image.height);
    renderer.DeleteTexture(material.retexture);
    material.retexture = texture;
    material.retexturePath = path;
    material.retextureCRC = BUCRC().GenerateRaw(png.data(), png.size());
    material.useRetexture = true;
    Log("PNG preview for " + geometry.materials[index].name + ": " + path + " (package unchanged).");
    SetTextured(true);
}

void ModelPreview::UsePackageTexture(size_t index)
{
    if (index >= materials.size())
        return;
    materials[index].useRetexture = false;
    Log("Using package texture for " + geometry.materials[index].name + ".");
    SetTextured(true);
}

void ModelPreview::UseRetexture(size_t index)
{
    if (index >= materials.size() || !materials[index].retexture.id)
        throw std::runtime_error("Load a retexture PNG first.");
    materials[index].useRetexture = true;
    Log("Using retexture for " + geometry.materials[index].name + ".");
    SetTextured(true);
}

void ModelPreview::ClearRetexture(size_t index)
{
    if (index >= materials.size())
        return;
    renderer.DeleteTexture(materials[index].retexture);
    materials[index].retexturePath.clear();
    UsePackageTexture(index);
}

void ModelPreview::ApplyRetexture(PackageSession& session, size_t index)
{
    if (SelectionKey::Of(session) != shown || index >= materials.size() || !materials[index].packageTexture)
        throw std::runtime_error("Select a material with a texture in this package first.");
    const MaterialState& material = materials[index];
    if (material.retexturePath.empty())
        throw std::runtime_error("Load a retexture PNG first.");
    const auto png = ReadFile(std::filesystem::u8path(material.retexturePath));
    if (BUCRC().GenerateRaw(png.data(), png.size()) != material.retextureCRC)
        throw std::runtime_error("Retexture changed on disk. Reload PNG to preview it before applying.");

    const CMChunkResourceHeader header = *material.packageTexture;
    const RZTexture texture = RZTexture::Load(*shown.package, header);
    ReplaceTexture(session, header, material.retexturePath, texture.desc.width, texture.desc.height);
    renderDirty = true;
}

const std::optional<CMChunkResourceHeader>& ModelPreview::PackageTexture(size_t material) const
{
    return materials.at(material).packageTexture;
}

void ModelPreview::HandleCameraInput(bool blocked)
{
    if (blocked)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)
            renderDirty = true;
        camera.Orbit(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        camera.Zoom(io.MouseWheel);
        renderDirty = true;
    }
}

void ModelPreview::RenderFrame(int width, int height)
{
    if (isHierarchy)
    {
        renderer.RenderSkeleton(bonePositions, boneParents, camera, width, height);
        return;
    }
    const auto styles = MeshStyles();
    renderer.Render(geometry.meshes, styles, camera, twoSided, width, height);
}

std::vector<ModelRenderer::MeshStyle> ModelPreview::MeshStyles()
{
    std::vector<ModelRenderer::MeshStyle> styles(geometry.meshes.size());
    for (size_t i = 0; i < styles.size(); ++i)
    {
        const RZGeometry::Mesh& mesh = geometry.meshes[i];
        const bool hasMaterial = mesh.material < geometry.materials.size();
        ModelRenderer::MeshStyle& style = styles[i];
        style.texture = textured && mesh.hasUV ? MaterialTexture(mesh.material) : 0;
        style.uvDivisor = mesh.integerUV ? uvDivisor : 1.0f;
        style.alphaCutout = hasMaterial && materials[mesh.material].alphaCutout;
        const float* color = style.texture && hasMaterial ? geometry.materials[mesh.material].color
            : kMeshPalette[mesh.material % std::size(kMeshPalette)];
        std::copy_n(color, 3, style.color);
    }
    return styles;
}

unsigned int ModelPreview::MaterialTexture(size_t index)
{
    if (index >= materials.size())
        return 0;
    MaterialState& material = materials[index];
    if (material.useRetexture && material.retexture.id)
        return material.retexture.id;
    if (material.textureRequested)
        return material.texture.id;
    material.textureRequested = true;
    if (!material.packageTexture || !shown.package)
        return 0;

    const CMChunkMaterialInfo& info = geometry.materials[index];
    try
    {
        const RZTexture texture = RZTexture::Load(*shown.package, *material.packageTexture);
        if (!texture.desc.Is2D() || texture.desc.width > ModelRenderer::kMaxTextureSize ||
            texture.desc.height > ModelRenderer::kMaxTextureSize)
            throw std::runtime_error("Unsupported or oversized material texture.");
        material.texture = renderer.UploadTexture(texture.DecodeLevel0(), texture.desc.width, texture.desc.height,
            info.addressMode == 1);
    }
    catch (const std::exception& ex)
    {
        Log("Texture " + info.name + ": " + ex.what());
    }
    return material.texture.id;
}

void ModelPreview::Log(std::string message)
{
    logMessages.push_back(std::move(message));
}