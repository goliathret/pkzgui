#include "animation/animation_placement.h"
#include <cmath>
#include <cstdio>
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
} // namespace
int main()
{
    try
    {
        PreviewPlacement();
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }
}
