#pragma once
#include "model/model_renderer.h"
#include "package/package_session.h"
#include "Resource/RZGeometry.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImVec2;

class ModelPreview
{
public:
    ModelPreview() = default;
    ~ModelPreview();
    ModelPreview(const ModelPreview&) = delete;
    ModelPreview& operator=(const ModelPreview&) = delete;

    void Update(const PackageSession& session);
    void Clear();
    void Draw();
    void DrawActions(PackageSession& session, GLFWwindow* window);
    void DrawFields() const;
    std::vector<std::string> TakeLogMessages();

    void SetTextured(bool enabled);
    void LoadRetexture(size_t material, const std::string& path);
    void UsePackageTexture(size_t material);
    void UseRetexture(size_t material);
    void ClearRetexture(size_t material);
    void ApplyRetexture(PackageSession& session, size_t material);
    void ApplyMaterialFlags(PackageSession& session, size_t material, uint32_t flags);
    void ExportGeometryObj(const std::string& path) const;

    bool IsActive() const { return active; }
    const std::string& Name() const { return name; }
    const std::string& Error() const { return error; }
    const RZGeometry& Geometry() const { return geometry; }
    const std::optional<CMChunkResourceHeader>& PackageTexture(size_t material) const;
    size_t VertexCount() const { return vertexCount; }
    size_t TriangleCount() const { return triangleCount; }

private:
    static constexpr size_t kGeometryBudget = 128 * 1024 * 1024;

    struct MaterialState
    {
        std::optional<CMChunkResourceHeader> packageTexture;
        ModelRenderer::Texture texture;
        bool textureRequested = false;
        ModelRenderer::Texture retexture;
        std::string retexturePath;
        uint32_t retextureCRC = 0;
        bool useRetexture = false;
        bool alphaCutout = false;
    };

    void Load(const PKPackage& package, const CMChunkResourceHeader& resource);
    void LoadHierarchy(const PKPackage& package, const CMChunkResourceHeader& resource, const CMChunk* selected);
    void HandleCameraInput(bool blocked);
    void RenderFrame(int width, int height);
    std::vector<ModelRenderer::MeshStyle> MeshStyles();
    unsigned int MaterialTexture(size_t index);

    bool DrawOverlay(const ImVec2& origin, const ImVec2& size);
    void DrawOverlayControls(bool collapsed, bool insidePopup);
    void DrawMaterialCombo(const char* id);
    void DrawTextureSummary(size_t index);
    void DrawMaterialFlags(PackageSession& session, size_t index);
    void DrawRetextureActions(PackageSession& session, GLFWwindow* window, size_t index);
    void DrawGeometryIO(PackageSession& session, GLFWwindow* window);
    void DrawShaderExport(GLFWwindow* window, size_t index);
    void Log(std::string message);

    SelectionKey shown;
    bool active = false;
    std::string name;
    std::string error;
    RZGeometry geometry;
    std::vector<MaterialState> materials;
    size_t vertexCount = 0;
    size_t triangleCount = 0;

    bool isHierarchy = false;
    std::vector<BUVector3> bonePositions;
    std::vector<uint32_t> boneParents;
    std::vector<std::string> boneNames;
    size_t boneCount = 0;

    ModelRenderer renderer;
    OrbitCamera camera;
    int selectedMaterial = 0;
    float uvDivisor = 1024.0f;
    bool textured = true; // SHOW TEXTURES BY DEFAULT!! this was driving me crazy :(
    bool twoSided = true;
    bool overlayCollapsed = false;
    bool renderDirty = true;
    std::vector<std::string> logMessages;
};

void ExportMaterialShaders(const CMChunkMaterialInfo& material, const std::string& path);