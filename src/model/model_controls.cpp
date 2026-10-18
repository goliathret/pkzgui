#include "model/model_preview.h"
#include "platform/platform.h"
#include "texture/texture.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr float kOverlayPadding = 8.0f;
    constexpr const char* kViewControlsPopup = "Model view controls";
    constexpr const char* kAdjustmentsPopup = "Model adjustments";
    constexpr const char* kPngFilter = "PNG image";

    std::string JsonString(const std::string& text)
    {
        static const char* hex = "0123456789abcdef";
        std::string result = "\"";
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\')
            {
                result += '\\';
                result += char(c);
            }
            else if (c < 32)
            {
                result += "\\u00";
                result += hex[c >> 4];
                result += hex[c & 15];
            }
            else
                result += char(c);
        }
        return result + '"';
    }

    const char* Separator(size_t index)
    {
        return index ? "," : "";
    }

    void WriteSamplers(std::ostringstream& json, const CMChunkMaterialInfo& material)
    {
        json << ",\n  \"samplers\": [";
        for (size_t i = 0; i < material.samplers.size(); ++i)
        {
            const auto& sampler = material.samplers[i];
            json << Separator(i) << "\n    {\"slot\":" << i << ",\"crc\":" << sampler.crc
                << ",\"addressMode\":" << unsigned(sampler.addressMode)
                << ",\"sourcePath\":" << JsonString(sampler.sourcePath) << '}';
        }
        json << "\n  ]";
    }

    void WriteShader(std::ostringstream& json, std::vector<uint8_t>& binary, const char* stage,
        const CMChunkMaterialInfo& material, const CMChunkMaterialInfo::ShaderRecord& record)
    {
        const auto container = material.ShaderContainer(record);
        json << ",\"" << stage << "\":{\"offset\":" << binary.size() << ",\"size\":" << container.size() << '}';
        binary.insert(binary.end(), container.begin(), container.end());
    }

    void WritePasses(std::ostringstream& json, const CMChunkMaterialInfo& material, std::vector<uint8_t>& binary)
    {
        json << ",\n  \"passes\": [";
        for (size_t i = 0; i < material.passes.size(); ++i)
        {
            const auto& pass = material.passes[i];
            json << Separator(i) << "\n    {\"slot\":" << pass.slot << ",\"set\":" << pass.set
                << ",\"format\":" << pass.format;
            WriteShader(json, binary, "vertex", material, pass.vertex);
            WriteShader(json, binary, "pixel", material, pass.pixel);
            json << '}';
        }
        json << "\n  ]";
    }

    void WriteDeclarations(std::ostringstream& json, const CMChunkMaterialInfo& material)
    {
        json << ",\n  \"declarations\": [";
        for (size_t i = 0; i < material.declarations.size(); ++i)
        {
            const auto& declaration = material.declarations[i];
            json << Separator(i) << "\n    {\"format\":" << declaration.format << ",\"slot\":" << declaration.slot
                << ",\"set\":" << declaration.set << ",\"attributes\":[";
            for (size_t j = 0; j < declaration.elements.size(); ++j)
            {
                const auto& element = declaration.elements[j];
                json << Separator(j) << "{\"stream\":" << element.stream << ",\"offset\":" << element.offset
                    << ",\"type\":" << element.type << ",\"usage\":" << unsigned(element.usage)
                    << ",\"index\":" << unsigned(element.index) << '}';
            }
            json << "]}";
        }
        json << "\n  ]";
    }
}

bool ModelPreview::DrawOverlay(const ImVec2& origin, const ImVec2& area)
{
    if (area.x < 64 || area.y < 48)
        return false;

    const float fontSize = ImGui::GetFontSize();
    const bool compact = area.x < fontSize * 16 || area.y < ImGui::GetFrameHeightWithSpacing() * 4;
    const bool collapsed = overlayCollapsed && !compact;
    const int rows = compact || collapsed ? 1 : (materials.empty() ? 3 : 4);
    const ImVec2 size(std::min(area.x - kOverlayPadding * 2, fontSize * (collapsed ? 10 : 18)),
        std::min(area.y - kOverlayPadding * 2, (ImGui::GetFrameHeight() + 3) * rows + kOverlayPadding * 2));
    const ImVec2 position(origin.x + area.x - size.x - kOverlayPadding, origin.y + area.y - size.y - kOverlayPadding);

    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    background.w = 0.95f;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kOverlayPadding, kOverlayPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 3));
    ImGui::BeginChild("Model HUD", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

    if (compact)
    {
        if (ImGui::Button("View...", ImVec2(-1, 0)))
            ImGui::OpenPopup(kViewControlsPopup);
        ImGui::SetNextWindowSizeConstraints(ImVec2(fontSize * 18, 0), ImGui::GetMainViewport()->WorkSize);
        if (ImGui::BeginPopup(kViewControlsPopup))
        {
            DrawOverlayControls(false, true);
            ImGui::EndPopup();
        }
    }
    else
        DrawOverlayControls(collapsed, false);

    const bool capturesInput =
        ImGui::IsMouseHoveringRect(position, ImVec2(position.x + size.x, position.y + size.y)) ||
        ImGui::IsPopupOpen(kAdjustmentsPopup) || ImGui::IsPopupOpen(kViewControlsPopup);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return capturesInput;
}

void ModelPreview::DrawOverlayControls(bool collapsed, bool insidePopup)
{
    if (ImGui::ArrowButton("##Collapse model HUD", collapsed ? ImGuiDir_Right : ImGuiDir_Down))
    {
        overlayCollapsed = !overlayCollapsed;
        if (insidePopup)
            ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip(collapsed ? "Expand model controls" : "Condense model controls");
    ImGui::SameLine();
    if (!collapsed)
        ImGui::TextUnformatted("Model controls");
    if (ImGui::Checkbox("Textures", &textured))
        renderDirty = true;
    if (collapsed)
        return;

    if (ImGui::Checkbox("Two-sided", &twoSided))
        renderDirty = true;
    ImGui::SameLine();
    if (ImGui::Button("Frame model"))
    {
        camera.Frame(geometry.Bounds());
        renderDirty = true;
    }
    if (materials.empty())
        return;

    if (ImGui::Button("Adjustments..."))
        ImGui::OpenPopup(kAdjustmentsPopup);
    if (!ImGui::BeginPopup(kAdjustmentsPopup))
        return;
    DrawMaterialCombo("##Adjustment material");
    if (ImGui::Checkbox("Cutout alpha", &materials[selectedMaterial].alphaCutout))
        renderDirty = true;
    ImGui::SetItemTooltip("Use the selected material's texture alpha as a cutout mask.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::DragFloat("UV divisor", &uvDivisor, 1, 1, 65536, "%.0f", ImGuiSliderFlags_AlwaysClamp))
        renderDirty = true;
    ImGui::SetItemTooltip("Integer UV coordinates only. OfficeSuit uses 1024; animated UV transforms are not emulated.");
    if (ImGui::Checkbox("Draw Mats w/ No Tex", &renderer.drawNoTex))
        renderDirty = true;
    ImGui::EndPopup();
}

void ModelPreview::DrawMaterialCombo(const char* id)
{
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, geometry.materials[selectedMaterial].name.c_str()))
    {
        for (size_t i = 0; i < geometry.materials.size(); ++i)
            if (ImGui::Selectable(geometry.materials[i].name.c_str(), selectedMaterial == int(i)))
                selectedMaterial = int(i);
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Material");
}

void ModelPreview::DrawActions(PackageSession& session, GLFWwindow* window)
{
    ImGui::TextUnformatted("Model exports / textures");
    ImGui::Separator();
    if (materials.empty() || !error.empty())
    {
        ImGui::TextDisabled("No model materials available.");
        return;
    }

    DrawMaterialCombo("##Export material");
    const size_t index = size_t(selectedMaterial);
    DrawTextureSummary(index);
    try
    {
        DrawMaterialFlags(session, index);
        DrawRetextureActions(session, window, index);
        ImGui::Separator();
        DrawShaderExport(window, index);
    }
    catch (const std::exception& ex)
    {
        Log("Model texture/export error: " + std::string(ex.what()));
    }
}

void ModelPreview::DrawTextureSummary(size_t index)
{
    const CMChunkMaterialInfo& info = geometry.materials[index];
    const MaterialState& material = materials[index];
    if (material.packageTexture)
    {
        const std::string textureName = material.packageTexture->GetName();
        ImGui::TextUnformatted(textureName.c_str());
        ImGui::SetItemTooltip("Package texture: %s", textureName.c_str());
    }
    else
    {
        ImGui::TextDisabled("No package texture");
        ImGui::SetItemTooltip(info.textureCRC
            ? "The referenced texture is not in this package. A PNG can still be previewed."
            : "This material has no base-color texture.");
    }
    if (!material.retexturePath.empty())
    {
        ImGui::Text("Retexture: %s", FileName(material.retexturePath).c_str());
        ImGui::SetItemTooltip("%s", material.retexturePath.c_str());
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const bool sameLine = ImGui::GetContentRegionAvail().x >=
        ImGui::CalcTextSize("Package textureRetexture").x + ImGui::GetFrameHeight() * 2 + style.ItemSpacing.x * 3;
    if (ImGui::RadioButton("Package texture", !material.useRetexture))
        UsePackageTexture(index);
    if (sameLine)
        ImGui::SameLine();
    ImGui::BeginDisabled(!material.retexture.id);
    const bool useRetexture = ImGui::RadioButton("Retexture", material.useRetexture);
    ImGui::EndDisabled();
    if (useRetexture)
        UseRetexture(index);
}

void ModelPreview::DrawMaterialFlags(PackageSession& session, size_t index)
{
    if (index >= geometry.materials.size())
        return;

    // MaterialFlags bits from EOT's X360 schema (Geo_MaterialInfo.materialFlags)
    constexpr uint32_t kCellShading = 0x00080000u;
    constexpr uint32_t kRimLighting = 0x08000000u;

    uint32_t flags = geometry.materials[index].flags;
    bool cellShading = (flags & kCellShading) != 0;
    bool rimLighting = (flags & kRimLighting) != 0;

    ImGui::Separator();
    ImGui::TextUnformatted("Material flags");
    bool changed = false;
    if (ImGui::Checkbox("Cell shading", &cellShading))
        changed = true;
    ImGui::SetItemTooltip("MaterialFlags::CELLSHADING (0x00080000). Saved into the package on change.");
    if (ImGui::Checkbox("Rim lighting", &rimLighting))
        changed = true;
    ImGui::SetItemTooltip("MaterialFlags::RIM_LIGHTING (0x08000000). Saved into the package on change.");

    if (!changed)
        return;

    if (cellShading)
        flags |= kCellShading;
    else
        flags &= ~kCellShading;
    if (rimLighting)
        flags |= kRimLighting;
    else
        flags &= ~kRimLighting;

    ApplyMaterialFlags(session, index, flags);
}

void ModelPreview::DrawRetextureActions(PackageSession& session, GLFWwindow* window, size_t index)
{
    const ImVec2 fullWidth(-1, 0);
    const MaterialState& material = materials[index];

    ImGui::BeginDisabled(!material.packageTexture);
    const bool extract = ImGui::Button("Extract package PNG...", fullWidth);
    ImGui::EndDisabled();
    if (extract)
    {
        const auto& header = *material.packageTexture;
        const auto path = PickSaveFile(window, kPngFilter, "png", SanitizeFileName(header.GetName()) + ".png");
        if (!path.empty())
        {
            ExtractTexture(*session.package, header, path);
            Log("Package texture extracted: " + path);
        }
    }

    if (ImGui::Button("Load retexture PNG...", fullWidth))
    {
        const auto path = PickOpenFile(window, kPngFilter, "png");
        if (!path.empty())
            LoadRetexture(index, path);
    }

    ImGui::BeginDisabled(material.retexturePath.empty());
    const float halfWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    const bool reload = ImGui::Button("Reload PNG", ImVec2(halfWidth, 0));
    ImGui::SameLine();
    const bool clear = ImGui::Button("Clear", fullWidth);
    ImGui::EndDisabled();
    if (reload)
        LoadRetexture(index, std::string(material.retexturePath));
    if (clear)
        ClearRetexture(index);

    ImGui::BeginDisabled(!material.packageTexture || material.retexturePath.empty());
    const bool apply = ImGui::Button("Apply retexture to package", fullWidth);
    ImGui::SetItemTooltip("Replaces every use of this texture. Save As writes the edited package.");
    ImGui::EndDisabled();
    if (apply)
        ApplyRetexture(session, index);
}

void ModelPreview::DrawShaderExport(GLFWwindow* window, size_t index)
{
    const CMChunkMaterialInfo& info = geometry.materials[index];
    ImGui::BeginDisabled(info.passes.empty());
    const bool exportShaders = ImGui::Button("Export material shaders...", ImVec2(-1, 0));
    ImGui::EndDisabled();
    if (!exportShaders)
        return;
    const auto path = PickSaveFile(window, "Shader containers", "bin", SanitizeFileName(info.name) + ".bin");
    if (path.empty())
        return;
    ExportMaterialShaders(info, path);
    Log("Shader containers and binding metadata exported for XenosRecomp: " + path);
}

void ExportMaterialShaders(const CMChunkMaterialInfo& material, const std::string& path)
{
    std::vector<uint8_t> binary;
    std::ostringstream json;
    json << "{\n  \"version\": 1,\n  \"material\": " << JsonString(material.name)
        << ",\n  \"materialFlags\": " << material.flags;
    WriteSamplers(json, material);
    WritePasses(json, material, binary);
    WriteDeclarations(json, material);
    json << "\n}\n";

    if (binary.empty())
        throw std::runtime_error("This material has no embedded shader containers.");
    WriteFile(std::filesystem::u8path(path), binary);
    const std::string manifest = json.str();
    WriteFile(std::filesystem::u8path(path + ".json"), std::vector<uint8_t>(manifest.begin(), manifest.end()));
}