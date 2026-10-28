#pragma once
#include "model/model_renderer.h"
#include "model/skin_view.h"
#include "animation/animation_placement.h"
#include "Resource/RZGeometrySkin.h"
#include "package/package_session.h"
#include "Resource/RZGeometry.h"
#include "Resource/RZEnvironment.h"
#include <cstdint>
#include <memory>
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
    void SelectEnvironmentPart(size_t part);
    void SelectMaterial(size_t material);
    size_t EnvironmentPartCount() const { return environment ? environment->parts.size() : 0; }
    const std::string& EnvironmentPartName(size_t part) const { return environment->parts.at(part).name; }

    void LoadRetexture(size_t material, const std::string& path);
    void UsePackageTexture(size_t material);
    void UseRetexture(size_t material);
    void ClearRetexture(size_t material);
    void ApplyRetexture(PackageSession& session, size_t material);
    void ApplyMaterialFlags(PackageSession& session, size_t material, uint32_t flags);
    void ExportGeometryObj(const std::string& path) const;
    void ExportGeometryGlb(const std::string& path, bool includeAnimation = true) const;
    size_t AnimationRigCount() const { return animationRigs.size(); }
    const std::string& AnimationRigName(size_t index) const { return animationRigs.at(index).name; }
    void SelectAnimationRig(size_t index);
    void UseRigPackage(std::unique_ptr<PKPackage> package, const std::string& path);
    void LoadRigPackage(const std::string& path);
    std::string RigPackageName() const;
    void SetAnimationGrounded(bool enabled);
    size_t AnimationCount() const { return animationChoices.size(); }
    const std::string& AnimationName(size_t index) const { return animationChoices.at(index).clip.name; }
    void SelectAnimation(size_t index);
    void SetAnimationTime(float time);
    void SetAnimationPlaying(bool play);
    void ResetAnimation();
    void AdvanceAnimation(float seconds);
    float AnimationTime() const { return animationTime; }
    bool AnimationPlaying() const { return animationPlaying; }
    void SetSkinShading(SkinShading shading);
    void SelectBone(int bone);
    void SetShowSkeleton(bool show);
    std::string GlbExportSummary() const;

    bool IsActive() const { return active; }
    const std::string& Name() const { return name; }
    const std::string& Error() const { return error; }
    const RZGeometry& Geometry() const { return geometry; }
    const std::optional<CMChunkResourceHeader>& PackageTexture(size_t material) const;
    size_t VertexCount() const { return vertexCount; }
    size_t TriangleCount() const { return triangleCount; }

private:
    static constexpr size_t kGeometryBudget = 128 * 1024 * 1024;
    static constexpr const char* kSkinPopup = "Skin view";

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
    void LoadHierarchy(const PKPackage& package, const CMChunkResourceHeader& resource);
    void HandleCameraInput(const ImVec2& origin, bool blocked);
    void FrameCamera(bool resetRotation = false);
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
    void InitializeAnimations(const PKPackage& package, const CMChunkResourceHeader& resource);
    void ApplyAnimation();
    void DrawAnimationControls();
    void DrawSkinControls();
    void UpdateBonePose(const std::vector<BUMatrix>& world);
    int BoneAt(const ImVec2& origin, const ImVec2& size, const ImVec2& point) const;
    std::string BoneLabel(size_t bone) const;
    size_t SkinBoneCount() const;

    SelectionKey shown;
    bool active = false;
    std::string name;
    std::string error;
    RZGeometry geometry;
    std::optional<RZEnvironment> environment;
    size_t environmentPart = 0;
    char environmentFilter[128] = {};
    std::vector<MaterialState> materials;
    size_t vertexCount = 0;
    size_t triangleCount = 0;

    bool isHierarchy = false;
    std::vector<BUVector3> bonePositions;
    std::vector<uint32_t> boneParents;
    std::vector<std::string> boneNames;
    size_t boneCount = 0;
    struct AnimationChoice
    {
        RZHAnim clip;
        const CMChunk* resource;
        const PKPackage* package;
    };
    std::vector<RZHierarchy> animationRigs;
    std::vector<const PKPackage*> rigSources;
    std::unique_ptr<PKPackage> rigPackage;
    std::string rigPackagePath;
    int selectedRig = -1;
    BUMatrix animationPlacement;
    bool animationGrounded = true;
    std::optional<RZHierarchy> animationSkeleton;
    std::optional<RZGeometrySkin> animationSkin;
    std::optional<RZGeometry> bindGeometry;
    std::vector<AnimationChoice> animationChoices;
    std::optional<RZHAnim> animationClip;
    float animationTime = 0;
    int selectedAnimation = -1;
    bool animationPlaying = false, animationLoop = true;
    std::optional<SkinSummary> skinSummary;
    SkinShading skinShading = SkinShading::Material;
    int selectedBone = -1;
    bool showSkeleton = false;
    bool vertexColorsDirty = true;
    char boneFilter[64] = {};

    ModelRenderer renderer;
    OrbitCamera camera;
    BUVector2 cameraViewport{1, 1};
    int cameraDragButton = -1;
    bool cameraPanning = false;
    bool frameOnDraw = true;
    int selectedMaterial = 0;
    float uvDivisor = 1024.0f;
    bool textured = true; // SHOW TEXTURES BY DEFAULT!! this was driving me crazy :(
    bool twoSided = true;
    bool overlayCollapsed = false;
    bool renderDirty = true;
    std::vector<std::string> logMessages;
};

void ExportMaterialShaders(const CMChunkMaterialInfo& material, const std::string& path);
