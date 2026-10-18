#include "hud/hud_preview.h"
#include "render/gl.h"
#include "ui/widgets.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
    constexpr uint32_t kHudWindow = 0x0641;
    constexpr uint32_t kHudWindowHeader = 0x0642;
    constexpr uint32_t kHud2DWindow = 0x0644;
    constexpr uint32_t kResourceTypeHud = 0x80;
    constexpr uint32_t kImage2D = 0x00000001;

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

    const uint8_t* ChunkPayload(const CMChunk& chunk, const std::vector<uint8_t>& packageBytes, size_t& outSize)
    {
        const size_t headerSize = chunk.HeaderSize();
        if (chunk.offset + headerSize + chunk.length <= packageBytes.size())
        {
            outSize = chunk.length;
            return packageBytes.data() + chunk.offset + headerSize;
        }
        if (chunk.offset + chunk.length <= packageBytes.size() && chunk.length > headerSize)
        {
            outSize = chunk.length - headerSize;
            return packageBytes.data() + chunk.offset + headerSize;
        }
        if (chunk.offset + chunk.length <= packageBytes.size())
        {
            outSize = chunk.length;
            return packageBytes.data() + chunk.offset;
        }
        outSize = 0;
        return nullptr;
    }

    void ParseWindow(const CMChunk& windowNode, const std::vector<uint8_t>& packageBytes, bool littleEndian,
        std::vector<HudPreview::Window2D>& out)
    {
        const CMChunk* header = nullptr;
        const CMChunk* twoD = nullptr;
        for (const CMChunk& child : windowNode.children)
        {
            if (child.GetMaskedID() == kHudWindowHeader)
                header = &child;
            else if (child.GetMaskedID() == kHud2DWindow)
                twoD = &child;
        }
        if (!header || !twoD)
            return;

        size_t payloadSize = 0;
        const uint8_t* payload = ChunkPayload(*header, packageBytes, payloadSize);
        if (!payload || payloadSize < 136)
            return;

        const uint32_t windowParts = ReadU32(payload + 0, littleEndian);
        if (!(windowParts & kImage2D))
            return;

        HudPreview::Window2D w;
        w.x = ReadF32(payload + 12, littleEndian);
        w.y = ReadF32(payload + 16, littleEndian);
        w.width = ReadF32(payload + 20, littleEndian);
        w.height = ReadF32(payload + 24, littleEndian);
        for (int i = 0; i < 16; ++i)
            w.cornerColors[i] = ReadF32(payload + 52 + i * 4, littleEndian);
        w.rotation = ReadF32(payload + 116, littleEndian);
        w.pivotX = ReadF32(payload + 120, littleEndian);
        w.pivotY = ReadF32(payload + 124, littleEndian);

        size_t twoSize = 0;
        const uint8_t* twoPayload = ChunkPayload(*twoD, packageBytes, twoSize);
        if (!twoPayload || twoSize < 80)
            return;

        w.textureId = ReadU32(twoPayload + 0, littleEndian);
        for (int i = 0; i < 8; ++i)
            w.uv[i] = ReadF32(twoPayload + 4 + i * 4, littleEndian);
        for (int i = 0; i < 8; ++i)
            w.maskUv[i] = ReadF32(twoPayload + 36 + i * 4, littleEndian);
        w.textureId3 = ReadU32(twoPayload + 68, littleEndian);
        w.maskTextureId = ReadU32(twoPayload + 72, littleEndian);
        w.flags = ReadU32(twoPayload + 76, littleEndian);
        out.push_back(w);
    }

    void CollectWindows(const CMChunk& node, const std::vector<uint8_t>& packageBytes, bool littleEndian,
        std::vector<HudPreview::Window2D>& out)
    {
        if (node.GetMaskedID() == kHudWindow)
            ParseWindow(node, packageBytes, littleEndian, out);
        for (const CMChunk& child : node.children)
            CollectWindows(child, packageBytes, littleEndian, out);
    }
}

HudPreview::~HudPreview()
{
    Clear();
}

void HudPreview::Clear()
{
    for (Window2D& w : windows)
    {
        if (w.glTexture)
            glDeleteTextures(1, &w.glTexture);
        w.glTexture = 0;
    }
    windows.clear();
    active = false;
    name.clear();
    error.clear();
    shown = {};
}

void HudPreview::UploadTexture(Window2D& window, const RZTexture& texture)
{
    if (window.glTexture)
        glDeleteTextures(1, &window.glTexture);
    window.glTexture = 0;
    const auto rgba = texture.DecodeLevel0();
    window.texWidth = texture.desc.width;
    window.texHeight = texture.desc.height;
    window.glTexture = CreateRgbaTexture(rgba.data(), GLsizei(window.texWidth), GLsizei(window.texHeight),
        GL_CLAMP_TO_EDGE);
}

void HudPreview::Load(const PKPackage& package, const CMChunkResourceHeader& resource, const CMChunk* selected)
{
    const std::vector<uint8_t> packageBytes = package.Serialize();
    const bool littleEndian = package.isLittleEndian;
    windows.clear();

    if (selected)
        CollectWindows(*selected, packageBytes, littleEndian, windows);

    if (windows.empty())
    {
        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* resourceRoot = FindResourceByCrc(root, resource.GetCRC()))
            {
                CollectWindows(*resourceRoot, packageBytes, littleEndian, windows);
                if (!windows.empty())
                    break;
            }
        }
    }

    if (windows.empty())
    {
        for (const CMChunk& root : package.rootChunks)
            CollectWindows(root, packageBytes, littleEndian, windows);
    }

    for (Window2D& w : windows)
    {
        if (!w.textureId)
            continue;
        try
        {
            const auto header = RZTexture::FindHeader(package, w.textureId, resource.GetLanguageMask(),
                resource.GetQualityLevel());
            if (header)
            {
                const RZTexture tex = RZTexture::Load(package, *header);
                w.textureName = header->GetName();
                UploadTexture(w, tex);
            }
        }
        catch (const std::exception&)
        {
        }
    }
}

void HudPreview::Update(const PackageSession& session)
{
    const SelectionKey key = SelectionKey::Of(session);
    if (key == shown)
        return;
    Clear();
    shown = key;
    if (!key.package || !key.chunk)
        return;
    try
    {
        const auto resource = session.SelectedResource();
        if (!resource || resource->GetResourceType() != kResourceTypeHud)
            return;
        active = true;
        name = resource->GetName();
        Load(*key.package, *resource, key.chunk);
        if (windows.empty())
            error = "No 2D HUD windows found.";
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        active = true;
    }
}

void HudPreview::Draw()
{
    if (!active)
        return;
    if (!error.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", error.c_str());
        return;
    }
    if (windows.empty())
    {
        ImGui::TextDisabled("No 2D windows in this HUD resource.");
        return;
    }

    float minX = windows[0].x, minY = windows[0].y, maxX = windows[0].x + windows[0].width,
        maxY = windows[0].y + windows[0].height;
    for (const Window2D& w : windows)
    {
        minX = std::min(minX, w.x);
        minY = std::min(minY, w.y);
        maxX = std::max(maxX, w.x + w.width);
        maxY = std::max(maxY, w.y + w.height);
    }
    const float contentW = std::max(1.0f, maxX - minX);
    const float contentH = std::max(1.0f, maxY - minY);

    constexpr float kAspect = 16.0f / 9.0f;
    float frameW = contentW;
    float frameH = contentH;
    if (frameW / frameH > kAspect)
        frameH = frameW / kAspect;
    else
        frameW = frameH * kAspect;
    const float frameMinX = minX - (frameW - contentW) * 0.5f;
    const float frameMinY = minY - (frameH - contentH) * 0.5f;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = std::min(avail.x / frameW, avail.y / frameH) * 0.95f;
    const float drawW = frameW * scale;
    const float drawH = frameH * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float offsetX = origin.x + (avail.x - drawW) * 0.5f;
    const float offsetY = origin.y + (avail.y - drawH) * 0.5f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(offsetX, offsetY), ImVec2(offsetX + drawW, offsetY + drawH),
        IM_COL32(30, 30, 35, 255));

    for (const Window2D& w : windows)
    {
        if (!w.glTexture)
            continue;
        const float sx = offsetX + (w.x - frameMinX) * scale;
        const float sy = offsetY + (w.y - frameMinY) * scale;
        const float sw = w.width * scale;
        const float sh = w.height * scale;

        float uvs[8] = {w.uv[0], w.uv[1], w.uv[2], w.uv[3], w.uv[4], w.uv[5], w.uv[6], w.uv[7]};
        float uvMin = uvs[0], uvMax = uvs[0];
        for (int i = 1; i < 8; ++i)
        {
            uvMin = std::min(uvMin, uvs[i]);
            uvMax = std::max(uvMax, uvs[i]);
        }
        if (uvMax > 1.5f && w.texWidth > 0 && w.texHeight > 0)
        {
            for (int i = 0; i < 4; ++i)
            {
                uvs[i * 2] /= float(w.texWidth);
                uvs[i * 2 + 1] /= float(w.texHeight);
            }
        }

        const ImVec2 uv0(uvs[0], uvs[1]);
        const ImVec2 uv1(uvs[2], uvs[3]);
        const ImVec2 uv2(uvs[4], uvs[5]);
        const ImVec2 uv3(uvs[6], uvs[7]);

        ImVec2 p0(sx, sy);
        ImVec2 p1(sx + sw, sy);
        ImVec2 p2(sx + sw, sy + sh);
        ImVec2 p3(sx, sy + sh);

        if (std::abs(w.rotation) > 0.0001f)
        {
            const float cx = sx + w.pivotX * scale;
            const float cy = sy + w.pivotY * scale;
            const float rad = w.rotation;
            const float c = std::cos(rad);
            const float s = std::sin(rad);
            auto rot = [&](ImVec2 p) {
                const float dx = p.x - cx;
                const float dy = p.y - cy;
                return ImVec2(cx + dx * c - dy * s, cy + dx * s + dy * c);
                };
            p0 = rot(p0);
            p1 = rot(p1);
            p2 = rot(p2);
            p3 = rot(p3);
        }

        drawList->AddImageQuad(static_cast<ImTextureID>(intptr_t(w.glTexture)), p0, p1, p2, p3, uv0, uv1, uv2, uv3);
    }

    ImGui::Dummy(ImVec2(avail.x, avail.y));
}

void HudPreview::DrawFields() const
{
    if (!active)
        return;
    if (!error.empty())
        FieldRow("error", "", error);
    FieldRow("windows", "", std::to_string(windows.size()));
    for (size_t i = 0; i < windows.size(); ++i)
    {
        const Window2D& w = windows[i];
        FieldRow(("w" + std::to_string(i) + ".pos").c_str(), "f32",
            std::to_string(w.x) + ", " + std::to_string(w.y));
        FieldRow(("w" + std::to_string(i) + ".size").c_str(), "f32",
            std::to_string(w.width) + " x " + std::to_string(w.height));
        FieldRow(("w" + std::to_string(i) + ".texture").c_str(), "",
            w.textureName.empty() ? HexString(w.textureId) : w.textureName);
        if (w.glTexture)
            FieldRow(("w" + std::to_string(i) + ".texSize").c_str(), "",
                std::to_string(w.texWidth) + " x " + std::to_string(w.texHeight));
    }
}