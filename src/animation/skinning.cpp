#include "animation/animation.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace animation
{
namespace
{
uint32_t U32(const CMChunk &c, size_t at)
{
    return c.Read<uint32_t>(at);
}
uint32_t Level(const CMChunk &c)
{
    const auto *info = c.FindChild(Geo_GeomLODInfo);
    if (!info || info->data.size() < 16)
        throw std::runtime_error("Skin LOD metadata is missing.");
    return U32(*info, 12);
}
BUVector3 TransformPoint(const Mat4 &m, const BUVector3 &v, bool direction)
{
    return BUVector3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + (direction ? 0 : m.m[12]),
                     m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + (direction ? 0 : m.m[13]),
                     m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + (direction ? 0 : m.m[14]));
}
Mat4 NormalMatrix(const Mat4 &m)
{
    const BUVector3 x(m.m[0], m.m[1], m.m[2]), y(m.m[4], m.m[5], m.m[6]), z(m.m[8], m.m[9], m.m[10]);
    const float determinant = x.Dot(y.Cross(z));
    if (std::abs(determinant) < 1e-10f)
        return m;
    const BUVector3 columns[] = {y.Cross(z) * (1 / determinant), z.Cross(x) * (1 / determinant),
                                 x.Cross(y) * (1 / determinant)};
    Mat4 result;
    for (size_t i = 0; i < 3; ++i)
    {
        result.m[i * 4] = columns[i].x;
        result.m[i * 4 + 1] = columns[i].y;
        result.m[i * 4 + 2] = columns[i].z;
    }
    return result;
}
} // namespace
Skin Skin::Load(const PKPackage &package, const CMChunkResourceHeader &header, const RZGeometry &geometry,
                const Skeleton &skeleton)
{
    const auto *resource = RZGeometry::FindResource(package, header);
    const auto *source = resource ? resource->FindChild(Geo_Geometry) : nullptr;
    const auto *post = package.FindRootAtOffset(header.GetDataOffset());
    if (!source || !post)
        throw std::runtime_error("Skin geometry buffers are missing.");
    const auto *postHeader = post->FindChild(GenSub_ResourceHeader);
    if (!postHeader || !CMChunkResourceHeader(*postHeader).IsSameResource(header))
        throw std::runtime_error("Skin buffers do not match the model resource.");
    const CMChunk *descriptors = nullptr;
    const CMChunk *buffers = nullptr;
    uint32_t lowest = UINT32_MAX;
    for (const auto &c : source->children)
        if (c.GetMaskedID() == Geo_GeomLOD && c.FindChild(Geo_GeomMesh))
        {
            const uint32_t level = Level(c);
            if (level < lowest)
            {
                lowest = level;
                descriptors = &c;
            }
        }
    for (const auto &c : post->children)
        if (c.GetMaskedID() == Geo_GeomLOD && Level(c) == lowest)
        {
            if (buffers)
                throw std::runtime_error("Ambiguous skin LOD buffers.");
            buffers = &c;
        }
    const auto *vertices = buffers ? buffers->FindChild(GenSub_VertexList) : nullptr;
    if (!descriptors || !vertices)
        throw std::runtime_error("Skin vertex data is missing.");
    Skin result;
    size_t meshIndex = 0;
    for (const auto &d : descriptors->children)
    {
        if (d.GetMaskedID() != Geo_GeomMesh)
            continue;
        if (d.version != 8 || d.data.size() < 48 || meshIndex >= geometry.meshes.size())
            throw std::runtime_error("Unsupported skin mesh descriptor.");
        const uint32_t material = U32(d, 0), offset = U32(d, 4), stride = U32(d, 8), format = U32(d, 12),
                       count = U32(d, 16), paletteSize = U32(d, 28);
        if (count != geometry.meshes[meshIndex].VertexCount() || material >= geometry.materials.size() ||
            !paletteSize || paletteSize > 256 || paletteSize > (d.data.size() - 48) / 4 || !stride ||
            offset > vertices->data.size() || count > (vertices->data.size() - offset) / stride)
            throw std::runtime_error("Skin vertex or palette data is invalid.");
        const auto *decl = geometry.materials[material].FindDeclaration(format);
        const CMChunkMaterialInfo::VertexElement *indices = nullptr;
        const CMChunkMaterialInfo::VertexElement *weights = nullptr;
        if (decl)
            for (const auto &e : decl->elements)
                if (e.stream == 0 && e.index == 0)
                {
                    if (e.usage == 2)
                        indices = &e;
                    if (e.usage == 1)
                        weights = &e;
                }
        if (!indices || !weights || indices->GetFormat() != 6 || weights->GetFormat() != 6 ||
            indices->offset > stride || 4 > stride - indices->offset || weights->offset > stride ||
            4 > stride - weights->offset)
            throw std::runtime_error("This mesh does not have supported 8-bit skin indices and weights.");
        std::vector<uint32_t> palette;
        for (uint32_t i = 0; i < paletteSize; ++i)
        {
            const uint32_t bone = U32(d, 48 + i * 4);
            if (bone >= skeleton.bones.size())
                throw std::runtime_error("Skin palette references a missing skeleton bone.");
            palette.push_back(bone);
        }
        std::vector<Influence> skin(count);
        for (uint32_t v = 0; v < count; ++v)
        {
            const size_t base = offset + size_t(v) * stride;
            const uint32_t packedIndices = U32(*vertices, base + indices->offset),
                           packedWeights = U32(*vertices, base + weights->offset);
            float sum = 0;
            for (size_t i = 0; i < 4; ++i)
            {
                const uint32_t index = (packedIndices >> (8 * i)) & 255,
                               weight = (packedWeights >> (8 * i)) & 255;
                if (!weight)
                    continue;
                if (index >= palette.size())
                    throw std::runtime_error("Vertex skin index is outside the bone palette.");
                skin[v].bones[i] = palette[index];
                skin[v].weights[i] = float(weight) / 255;
                sum += skin[v].weights[i];
            }
            if (sum < .001f)
                throw std::runtime_error("Skinned vertex has no bone weights.");
            for (float &w : skin[v].weights)
                w /= sum;
        }
        result.meshes.push_back(std::move(skin));
        ++meshIndex;
    }
    if (result.meshes.size() != geometry.meshes.size())
        throw std::runtime_error("Skin mesh count mismatch.");
    return result;
}
void Skin::Apply(const Skeleton &skeleton, const std::vector<Mat4> &world, const RZGeometry &original,
                 RZGeometry &output) const
{
    if (world.size() != skeleton.bones.size() || meshes.size() != original.meshes.size() ||
        meshes.size() != output.meshes.size())
        throw std::runtime_error("Skin pose does not match the mesh.");
    std::vector<Mat4> matrices, normals;
    for (size_t i = 0; i < world.size(); ++i)
    {
        matrices.push_back(world[i] * skeleton.bones[i].inverseBind);
        normals.push_back(NormalMatrix(matrices.back()));
    }
    for (size_t m = 0; m < meshes.size(); ++m)
    {
        const auto &source = original.meshes[m];
        auto &target = output.meshes[m];
        if (meshes[m].size() != source.VertexCount() || source.normals.size() != source.positions.size() ||
            target.positions.size() != source.positions.size() ||
            target.normals.size() != source.normals.size())
            throw std::runtime_error("Skin vertex count mismatch.");
        for (size_t v = 0; v < meshes[m].size(); ++v)
        {
            const BUVector3 point(source.positions[v * 3], source.positions[v * 3 + 1],
                                  source.positions[v * 3 + 2]);
            const BUVector3 normal(source.normals[v * 3], source.normals[v * 3 + 1],
                                   source.normals[v * 3 + 2]);
            BUVector3 p(0, 0, 0), n(0, 0, 0);
            for (size_t i = 0; i < 4; ++i)
                if (meshes[m][v].weights[i] > 0)
                {
                    const auto &matrix = matrices.at(meshes[m][v].bones[i]);
                    p = p + TransformPoint(matrix, point, false) * meshes[m][v].weights[i];
                    n = n + TransformPoint(normals.at(meshes[m][v].bones[i]), normal, true) *
                                meshes[m][v].weights[i];
                }
            if (n.Length() > 1e-8f)
                n.Normalize();
            target.positions[v * 3] = p.x;
            target.positions[v * 3 + 1] = p.y;
            target.positions[v * 3 + 2] = p.z;
            target.normals[v * 3] = n.x;
            target.normals[v * 3 + 1] = n.y;
            target.normals[v * 3 + 2] = n.z;
        }
    }
}
} // namespace animation
