#include "animation/animation_placement.h"
#include "animation/rig_match.h"
#include "model/gltf_export.h"
#include "model/skin_view.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
bool Near(float a, float b)
{
    return std::abs(a - b) < .002f;
}
template <class Function> bool Rejects(Function function)
{
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}
RZHierarchy Rig()
{
    RZHierarchy rig;
    rig.bones.resize(3);
    rig.bones[0].crc = 0x20;
    rig.bones[1].crc = 0x10;
    rig.bones[1].parent = 0;
    rig.bones[2].crc = 0x30;
    rig.bones[2].parent = 1;
    rig.bones[2].bind.translation = {0, 0, 2, 0};
    return rig;
}
void PreviewPlacement()
{
    CMTransforms transform;
    transform.translation = {-1.688f, -1.612f, -.290f, 0};
    transform.rotation = {0, 0, std::sin(.4f), std::cos(.4f)};
    transform.scale = {2, 3, 4, 0};
    const auto identity = transform.InverseMatrix() * transform.Matrix();
    for (int i = 0; i < 16; ++i)
        Check(Near(identity.m[i / 4][i % 4], i % 5 == 0 ? 1.f : 0.f), "Root transform inverse is incorrect");
    transform.scale[1] = 0;
    Check(Rejects([&] { transform.InverseMatrix(); }), "Singular root alignment was accepted");

    auto rig = Rig();
    rig.bones.resize(2);
    rig.bones[0].parent = 1;
    rig.bones[1].parent = UINT32_MAX; // The actor root need not be the first bone.
    rig.bones[0].bind.translation = {0, 1, 0, 0};
    RZHAnim clip;
    clip.info.duration = 1;
    clip.tracks.resize(1);
    auto& track = clip.tracks[0];
    track.boneCRC = rig.bones[1].crc;
    RZHAnim::Segment translation;
    translation.start = 0;
    translation.end = 1;
    translation.interpolate = true;
    translation.from = {-1.688f, -1.612f, -.290f, 0};
    translation.to = {-1.688f, -.612f, -.290f, 0};
    track.translation.push_back(translation);
    RZHAnim::Segment rotation;
    rotation.start = 0;
    rotation.end = 1;
    rotation.interpolate = true;
    rotation.from = {0, 0, std::sin(.4f), std::cos(.4f)};
    rotation.to = {0, 0, std::sin(.6f), std::cos(.6f)};
    track.rotation.push_back(rotation);
    const auto placement = AnimationPreviewPlacement(clip, rig);
    const auto raw = rig.WorldPose(clip.Sample(rig, 0));
    const auto alignedRoot = placement * raw[1];
    for (int i = 0; i < 16; ++i)
        Check(Near(alignedRoot.m[i / 4][i % 4], i % 5 == 0 ? 1.f : 0.f),
              "Initial scene rotation or translation leaked into the preview");
    const auto alignedChild = placement * raw[0];
    Check(Near(alignedChild.m[3][0], 0) && Near(alignedChild.m[3][1], 1),
          "Root alignment changed the local skeletal pose");
    const auto later = placement * rig.WorldPose(clip.Sample(rig, 1))[1];
    Check(Near(later.m[3][0], std::sin(.8f)) && Near(later.m[3][1], std::cos(.8f)) &&
              Near(later.m[0][0], std::cos(.4f)),
          "Preview alignment removed subsequent root movement or turning");
    Check(Near(raw[1].m[3][1], -1.612f), "Alignment changed the original animation coordinates");
    rig.bones[0].parent = UINT32_MAX;
    const auto independent = AnimationPreviewPlacement(clip, rig);
    Check(Near(independent.m[3][0], 0) && Near(independent.m[3][1], 0) && Near(independent.m[0][0], 1),
          "An arbitrary root was chosen for independent hierarchies");
}
void RigMatching()
{
    Check(RigBaseName("SpiderManAmazing[Hi]") == "spidermanamazing", "Rig names keep their tags");
    Check(RigPrefixLength("SpiderManAmazing[Hi]", "SpiderManAmazingProdigy[Hi]") == 16,
          "A costume does not match its hero rig");
    Check(RigPrefixLength("SpiderManAmazing[Hi]", "SpiderManAmazing[Hi]") == 0, "An exact name counted as a prefix");
    Check(RigPrefixLength("SpiderMan2099_Ghost[Hi]", "SpiderManAmazingProdigy[Hi]") == 0,
          "An unrelated rig matched a costume");
}
void SkinView()
{
    float rgb[3];
    WeightRampColor(0, rgb);
    Check(Near(rgb[0], 0) && Near(rgb[1], 0) && Near(rgb[2], 1), "Zero weight is not blue");
    WeightRampColor(.5f, rgb);
    Check(Near(rgb[0], 0) && Near(rgb[1], 1) && Near(rgb[2], 0), "Half weight is not green");
    WeightRampColor(1, rgb);
    Check(Near(rgb[0], 1) && Near(rgb[1], 0) && Near(rgb[2], 0), "Full weight is not red");

    RZGeometrySkin::Influence shared, rigid;
    shared.bones = {1, 2, 0, 0};
    shared.weights = {.75f, .25f, 0, 0};
    rigid.weights = {1, 0, 0, 0};
    RZGeometrySkin skin;
    skin.requiredBoneCount = 3;
    skin.meshes = {{shared, rigid, rigid}};

    const auto weights = SkinVertexColors(skin, SkinShading::BoneWeight, 1);
    WeightRampColor(.75f, rgb);
    Check(weights.size() == 1 && weights[0].size() == 9 && Near(weights[0][0], rgb[0]) &&
              Near(weights[0][1], rgb[1]) && Near(weights[0][2], rgb[2]) && Near(weights[0][5], 1),
          "Bone weight colours do not follow the selected bone");
    float first[3], second[3];
    BoneColor(1, first);
    BoneColor(2, second);
    const auto blended = SkinVertexColors(skin, SkinShading::BoneColors, -1);
    Check(Near(blended[0][0], first[0] * .75f + second[0] * .25f) &&
              Near(blended[0][2], first[2] * .75f + second[2] * .25f),
          "Bone colours are not blended by weight");
    const auto summary = SummarizeSkin(skin);
    Check(summary.vertices == 3 && summary.weighted == 3 && summary.maxInfluences == 2 &&
              summary.boneVertices == std::vector<size_t>{2, 1, 1},
          "Skin summary miscounted vertices or influences");

    RZGeometry geometry;
    geometry.name = "skinned";
    geometry.meshes.resize(1);
    geometry.meshes[0].positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    geometry.meshes[0].normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    geometry.meshes[0].indices = {0, 1, 2};
    const auto rig = Rig();
    const auto exportJson = [&](const RZHAnim* clip) {
        const auto directory = std::filesystem::temp_directory_path() / "pkzgui_skin_export_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        const auto previous = std::filesystem::current_path();
        std::filesystem::current_path(directory);
        try
        {
            ExportGeometryGlb((directory / "skinned.glb").string(), geometry, {}, &rig, &skin, clip);
        }
        catch (...)
        {
            std::filesystem::current_path(previous);
            throw;
        }
        std::filesystem::current_path(previous);
        size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            files += entry.is_regular_file();
        std::ifstream file(directory / "skinned.glb", std::ios::binary);
        const std::string glb((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        std::filesystem::remove_all(directory);
        Check(files == 1, "glTF export wrote files besides the .glb");
        Check(glb.size() > 20 && glb.compare(0, 4, "glTF") == 0, "glTF export is not a GLB");
        const uint32_t length = uint8_t(glb[12]) | uint8_t(glb[13]) << 8 | uint8_t(glb[14]) << 16 |
                                uint32_t(uint8_t(glb[15])) << 24;
        return glb.substr(20, length);
    };

    const std::string json = exportJson(nullptr);
    Check(json.find("\"JOINTS_0\"") != std::string::npos && json.find("\"WEIGHTS_0\"") != std::string::npos &&
              json.find("\"skins\"") != std::string::npos && json.find("\"inverseBindMatrices\":") != std::string::npos,
          "Skinned glTF export lost its joints, weights or skin");
    size_t targets = 0;
    for (size_t at = json.find("\"target\":"); at != std::string::npos; at = json.find("\"target\":", at + 1))
        ++targets;
    Check(targets == 5, "Only vertex attribute and index buffer views may declare a target");

    RZHAnim clip;
    clip.name = "wave";
    clip.info.duration = 1;
    clip.tracks.resize(1);
    clip.tracks[0].boneCRC = rig.bones[2].crc;
    RZHAnim::Segment rotation;
    rotation.start = 0;
    rotation.end = 1;
    rotation.interpolate = true;
    rotation.from = {0, 0, 0, 1};
    rotation.to = {0, 0, std::sin(.5f), std::cos(.5f)};
    clip.tracks[0].rotation.push_back(rotation);
    const std::string animated = exportJson(&clip);
    std::vector<std::string> accessors;
    const size_t accessorsAt = animated.find("\"accessors\":[");
    const size_t accessorsEnd = animated.find("}]", accessorsAt);
    for (size_t open = animated.find('{', accessorsAt); open < accessorsEnd; open = animated.find('{', open + 1))
        accessors.push_back(animated.substr(open, animated.find('}', open) - open));
    size_t inputs = 0;
    for (size_t at = animated.find("\"input\":"); at != std::string::npos; at = animated.find("\"input\":", at + 1))
    {
        const size_t index = std::stoul(animated.substr(at + 8));
        Check(index < accessors.size() && accessors[index].find("\"type\":\"SCALAR\"") != std::string::npos &&
                  accessors[index].find("\"min\":") != std::string::npos,
              "An animation sampler input is not the keyframe time accessor");
        ++inputs;
    }
    Check(inputs == rig.bones.size() * 3, "Animated export lost bone channels");
}
} // namespace
int main()
{
    try
    {
        PreviewPlacement();
        RigMatching();
        SkinView();
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }
}
