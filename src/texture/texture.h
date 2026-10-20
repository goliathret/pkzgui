#pragma once
#include "package/package_session.h"
#include "Resource/RZTexture.h"
#include "BaseUtils/BUPng.h"
#include <string>

constexpr uint32_t kMaxTextureSize = 4096;

CMChunkResourceHeader SelectedTextureHeader(const PackageSession& session);
RZTexture SelectedTexture(const PackageSession& session);
BUPng::Image DecodeTextureImage(const std::vector<uint8_t>& file);
void ExtractTexture(const PKPackage& package, const CMChunkResourceHeader& header, const std::string& path);
void ReplaceTexture(PackageSession& session, const CMChunkResourceHeader& header, const std::string& path,
    uint32_t width, uint32_t height);

class TexturePreview
{
public:
    TexturePreview() = default;
    ~TexturePreview();
    TexturePreview(const TexturePreview&) = delete;
    TexturePreview& operator=(const TexturePreview&) = delete;

    void Update(const PackageSession& session);
    void Clear();
    void Draw() const;
    void DrawFields() const;

    bool IsActive() const { return active; }

private:
    void Upload(const RZTexture& texture);

    SelectionKey shown;
    bool active = false;
    std::string name;
    std::string error;
    RZTexture::Descriptor descriptor;
    unsigned int texture = 0;
};
