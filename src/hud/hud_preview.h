#pragma once
#include "package/package_session.h"
#include "Resource/RZTexture.h"
#include <cstdint>
#include <string>
#include <vector>

class HudPreview
{
public:
    HudPreview() = default;
    ~HudPreview();
    HudPreview(const HudPreview&) = delete;
    HudPreview& operator=(const HudPreview&) = delete;

    void Update(const PackageSession& session);
    void Clear();
    void Draw();
    void DrawFields() const;

    bool IsActive() const { return active; }

    struct Window2D
    {
        float x = 0;
        float y = 0;
        float width = 0;
        float height = 0;
        float rotation = 0;
        float pivotX = 0;
        float pivotY = 0;
        float cornerColors[16]{};
        uint32_t textureId = 0;
        float uv[8]{};
        float maskUv[8]{};
        uint32_t textureId3 = 0;
        uint32_t maskTextureId = 0;
        uint32_t flags = 0;
        unsigned int glTexture = 0;
        uint32_t texWidth = 0;
        uint32_t texHeight = 0;
        std::string textureName;
    };

private:
    void Load(const PKPackage& package, const CMChunkResourceHeader& resource, const CMChunk* selected);
    void UploadTexture(Window2D& window, const RZTexture& texture);

    SelectionKey shown;
    bool active = false;
    std::string name;
    std::string error;
    std::vector<Window2D> windows;
};
