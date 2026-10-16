#include "model/model_preview.h"
#include "platform/platform.h"
#include "texture/texture.h"
#include "ui/widgets.h"
#include "BaseUtils/BUCRC.h"
#include "BaseUtils/BUPng.h"
#include "imgui.h"
#include <algorithm>
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
        if (!resource || resource->GetResourceType() != kCMResourceType_Geometry)
            return;
        active = true;
        name = resource->GetName();
        Load(*key.package, *resource);
        if (sameModel)
        {
            camera.distance = previousDistance;
            selectedMaterial = std::clamp(previousMaterial, 0, std::max(0, int(materials.size()) - 1));
        }
        Log("Model " + name + ": " + std::to_string(geometry.meshes.size()) + " meshes, " + std::to_string(vertexCount) +
            " vertices, " + std::to_string(triangleCount) + " triangles (static preview).");
    }
    catch (const std::exception& ex)
    {
        geometry = RZGeometry();
        materials.clear();
        vertexCount = triangleCount = 0;
        active = true;
        error = ex.what();
        Log("Model " + name + ": " + error);
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
