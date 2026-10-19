#pragma once
#include "Resource/RZGeometry.h"
#include "Resource/ResourceHeader.h"
#include "render/gl.h"
#include <array>
#include <string>
#include <vector>

namespace animation
{
using Value = std::array<float, 4>;
struct Transform
{
    Value translation{0, 0, 0, 0};
    Value rotation{0, 0, 0, 1};
    Value scale{1, 1, 1, 0};
    Mat4 Matrix() const;
    Mat4 InverseMatrix() const;
};
struct Bone
{
    std::string name;
    uint32_t crc = 0, parent = UINT32_MAX;
    Transform bind;
    Mat4 localBind, inverseBind;
};
struct Skeleton
{
    std::string name;
    uint32_t crc = 0;
    std::vector<Bone> bones;
    static Skeleton Load(const CMChunk &resource, bool littleEndian);
    std::vector<Mat4> WorldPose(const std::vector<Transform> &local) const;
};
struct Segment
{
    float start = 0, end = 0;
    Value from{}, to{};
    bool interpolate = false;
};
struct Track
{
    uint32_t boneCRC = 0;
    std::vector<Segment> translation, rotation, scale;
};
struct Clip
{
    std::string name;
    uint32_t hierarchyCRC = 0;
    float duration = 0, interval = 0;
    std::vector<Track> tracks;
    static Clip Load(const PKPackage &package, const CMChunk &resource, bool decode = true);
    std::vector<int> Map(const Skeleton &skeleton) const;
    std::vector<Transform> Sample(const Skeleton &skeleton, float time) const;
    Mat4 PreviewPlacement(const Skeleton &skeleton) const;
};
struct Influence
{
    std::array<uint32_t, 4> bones{};
    std::array<float, 4> weights{};
};
struct Skin
{
    std::vector<std::vector<Influence>> meshes;
    static Skin Load(const PKPackage &package, const CMChunkResourceHeader &header,
                     const RZGeometry &geometry, const Skeleton &skeleton);
    void Apply(const Skeleton &skeleton, const std::vector<Mat4> &world, const RZGeometry &original,
               RZGeometry &output) const;
};
const CMChunk *Resource(const PKPackage &package, const CMChunkResourceHeader &header);
const CMChunk *Find(const CMChunk &chunk, uint32_t id);
} // namespace animation
