#pragma once
#include "BaseUtils/BUAABB.h"
#include "Resource/RZGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

struct OrbitCamera
{
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 3.0f;
    BUVector3 target;

    void Frame(const BUAABB& bounds)
    {
        if (bounds.mMin.x > bounds.mMax.x)
            return;
        target = (bounds.mMin + bounds.mMax) * 0.5f;
        const float radius = (bounds.mMax - bounds.mMin).Length() * 0.5f;
        distance = std::max(radius * 2.2f, 0.5f);
    }

    void Orbit(float deltaX, float deltaY)
    {
        yaw += deltaX * 0.01f;
        pitch = std::clamp(pitch + deltaY * 0.01f, -1.4f, 1.4f);
    }

    void Zoom(float wheel)
    {
        distance = std::clamp(distance * (wheel > 0 ? 0.9f : 1.11f), 0.2f, 1000000.0f);
    }

    BUVector3 Eye() const
    {
        const BUVector3 direction(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        return target + direction * distance;
    }
};

class ModelRenderer
{
public:
    static constexpr size_t kTextureBudget = 128 * 1024 * 1024;
    static constexpr uint32_t kMaxTextureSize = 4096;

    struct Texture
    {
        unsigned int id = 0;
        size_t bytes = 0;
    };

    struct MeshStyle
    {
        unsigned int texture = 0;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float uvDivisor = 1.0f;
        bool alphaCutout = false;
    };

    ModelRenderer() = default;
    ~ModelRenderer();
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    void Render(const std::vector<RZGeometry::Mesh>& meshes, const std::vector<MeshStyle>& styles,
        const OrbitCamera& camera, bool twoSided, int width, int height);
    Texture UploadTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool clampToEdge = false);
    void DeleteTexture(Texture& texture);
    void Release();

    bool HasFrame(int width, int height) const { return framebuffer && frameWidth == width && frameHeight == height; }
    unsigned int FrameTexture() const { return colorTexture; }

private:
    struct GpuMesh
    {
        unsigned int vertexArray = 0;
        unsigned int positions = 0;
        unsigned int normals = 0;
        unsigned int texcoords = 0;
        unsigned int indices = 0;
    };

    void EnsureProgram();
    void EnsureFramebuffer(int width, int height);
    void EnsureWhiteTexture();
    void UploadMeshes(const std::vector<RZGeometry::Mesh>& meshes);
    void ReleaseFramebuffer();
    int Uniform(const char* name) const;

    std::vector<GpuMesh> gpuMeshes;
    unsigned int program = 0;
    unsigned int framebuffer = 0;
    unsigned int colorTexture = 0;
    unsigned int depthBuffer = 0;
    unsigned int whiteTexture = 0;
    int frameWidth = 0;
    int frameHeight = 0;
    size_t textureBytes = 0;

    unsigned int msaaFramebuffer = 0;
    unsigned int msaaColorBuffer = 0;
    unsigned int msaaDepthBuffer = 0;
    int msaaSamples = 4;
};
