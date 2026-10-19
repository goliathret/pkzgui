#include "model/model_preview.h"
#include "platform/platform.h"
#include "render/gl.h"
#include "texture/texture.h"
#include "ui/widgets.h"
#include "BaseUtils/BUCRC.h"
#include "BaseUtils/BUPng.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr float kMaxFrameWidth = 1920.0f;
    constexpr float kMaxFrameHeight = 1080.0f;
    constexpr float kMeshPalette[][3] = {
        {0.75f, 0.78f, 0.82f}, {0.85f, 0.55f, 0.45f}, {0.45f, 0.70f, 0.55f},
        {0.55f, 0.60f, 0.85f}, {0.80f, 0.70f, 0.40f}, {0.70f, 0.50f, 0.70f},
    };

    void WriteBytesBE(uint8_t* p, const void* src, size_t size, bool littleEndian)
    {
        std::memcpy(p, src, size);
        if (!littleEndian)
            CMChunk::ByteSwap(p, size);
    }

    void WriteU16At(std::vector<uint8_t>& buf, size_t at, uint16_t value, bool littleEndian)
    {
        if (at + 2 > buf.size())
            buf.resize(at + 2);
        WriteBytesBE(buf.data() + at, &value, 2, littleEndian);
    }

    void WriteU32At(std::vector<uint8_t>& buf, size_t at, uint32_t value, bool littleEndian)
    {
        if (at + 4 > buf.size())
            buf.resize(at + 4);
        WriteBytesBE(buf.data() + at, &value, 4, littleEndian);
    }

    void WriteF32At(std::vector<uint8_t>& buf, size_t at, float value, bool littleEndian)
    {
        if (at + 4 > buf.size())
            buf.resize(at + 4);
        WriteBytesBE(buf.data() + at, &value, 4, littleEndian);
    }

    uint32_t PackSignedBits(float value, int width, float scale)
    {
        const int maxPos = (1 << (width - 1)) - 1;
        int quantized = int(std::lround(std::clamp(value, -1.0f, 1.0f) * scale));
        quantized = std::clamp(quantized, -maxPos - 1, maxPos);
        if (quantized < 0)
            quantized += 1 << width;
        return uint32_t(quantized) & ((1u << width) - 1u);
    }

    uint32_t PackNormal101111(float nx, float ny, float nz)
    {
        const uint32_t x = PackSignedBits(nx, 11, 1024.0f);
        const uint32_t y = PackSignedBits(ny, 11, 1024.0f);
        const uint32_t z = PackSignedBits(nz, 10, 512.0f);
        return x | (y << 11) | (z << 22);
    }

    struct ObjMesh
    {
        std::string name;
        std::string material;
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<uint32_t> indices;
    };

    struct ObjData
    {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<ObjMesh> meshes;
    };

    int ParseObjIndex(const std::string& token, int count)
    {
        if (token.empty() || count <= 0)
            return -1;
        const size_t slash = token.find('/');
        const std::string head = slash == std::string::npos ? token : token.substr(0, slash);
        if (head.empty())
            return -1;
        int value = std::stoi(head);
        if (value < 0)
            value = count + value + 1;
        if (value < 1 || value > count)
            throw std::runtime_error("OBJ index out of range");
        return value - 1;
    }

    void ParseObjFaceCorner(const std::string& token, int vCount, int vtCount, int vnCount,
        int& vi, int& ti, int& ni)
    {
        vi = ti = ni = -1;
        if (token.empty())
            return;
        size_t a = 0;
        size_t b = token.find('/');
        const std::string vPart = token.substr(0, b);
        vi = ParseObjIndex(vPart, vCount);
        if (b == std::string::npos)
            return;
        a = b + 1;
        b = token.find('/', a);
        const std::string vtPart = token.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!vtPart.empty())
            ti = ParseObjIndex(vtPart, vtCount);
        if (b == std::string::npos)
            return;
        const std::string vnPart = token.substr(b + 1);
        if (!vnPart.empty())
            ni = ParseObjIndex(vnPart, vnCount);
    }

    ObjData ParseObj(const std::string& text)
    {
        ObjData data;
        ObjMesh current;
        current.name = "mesh_0";
        bool meshStarted = false;
        std::string material;

        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t i = 0;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                ++i;
            if (i >= line.size() || line[i] == '#')
                continue;
            std::istringstream ls(line.substr(i));
            std::string tag;
            ls >> tag;
            if (tag == "v")
            {
                float x = 0, y = 0, z = 0;
                ls >> x >> y >> z;
                data.positions.insert(data.positions.end(), {x, y, z});
            }
            else if (tag == "vn")
            {
                float x = 0, y = 0, z = 0;
                ls >> x >> y >> z;
                data.normals.insert(data.normals.end(), {x, y, z});
            }
            else if (tag == "vt")
            {
                float u = 0, t = 0;
                ls >> u >> t;
                data.texcoords.insert(data.texcoords.end(), {u, t});
            }
            else if (tag == "o" || tag == "g")
            {
                if (meshStarted && !current.indices.empty())
                    data.meshes.push_back(std::move(current));
                current = ObjMesh{};
                current.material = material;
                std::string rest;
                std::getline(ls, rest);
                size_t s = 0;
                while (s < rest.size() && std::isspace(static_cast<unsigned char>(rest[s])))
                    ++s;
                current.name = s < rest.size() ? rest.substr(s) : (tag + "_" + std::to_string(data.meshes.size()));
                meshStarted = true;
            }
            else if (tag == "usemtl")
            {
                ls >> material;
                if (!meshStarted)
                {
                    current.material = material;
                }
                else if (!current.indices.empty())
                {
                    data.meshes.push_back(std::move(current));
                    current = ObjMesh{};
                    current.name = "mesh_" + std::to_string(data.meshes.size());
                    current.material = material;
                }
                else
                    current.material = material;
            }
            else if (tag == "f")
            {
                meshStarted = true;
                const int vCount = int(data.positions.size() / 3);
                const int vtCount = int(data.texcoords.size() / 2);
                const int vnCount = int(data.normals.size() / 3);
                std::vector<int> vis, tis, nis;
                std::string corner;
                while (ls >> corner)
                {
                    int vi = -1, ti = -1, ni = -1;
                    ParseObjFaceCorner(corner, vCount, vtCount, vnCount, vi, ti, ni);
                    if (vi < 0)
                        throw std::runtime_error("OBJ face is missing a vertex index");
                    vis.push_back(vi);
                    tis.push_back(ti);
                    nis.push_back(ni);
                }
                if (vis.size() < 3)
                    throw std::runtime_error("OBJ face has fewer than 3 corners");
                for (size_t t = 1; t + 1 < vis.size(); ++t)
                {
                    const int corners[3] = {vis[0], vis[t], vis[t + 1]};
                    const int tc[3] = {tis[0], tis[t], tis[t + 1]};
                    const int nc[3] = {nis[0], nis[t], nis[t + 1]};
                    for (int c = 0; c < 3; ++c)
                    {
                        const uint32_t local = uint32_t(current.positions.size() / 3);
                        current.positions.insert(current.positions.end(), {
                            data.positions[size_t(corners[c]) * 3 + 0],
                            data.positions[size_t(corners[c]) * 3 + 1],
                            data.positions[size_t(corners[c]) * 3 + 2]});
                        if (nc[c] >= 0)
                            current.normals.insert(current.normals.end(), {
                            data.normals[size_t(nc[c]) * 3 + 0],
                            data.normals[size_t(nc[c]) * 3 + 1],
                            data.normals[size_t(nc[c]) * 3 + 2]});
                        else
                            current.normals.insert(current.normals.end(), {0.0f, 1.0f, 0.0f});
                        if (tc[c] >= 0)
                            current.texcoords.insert(current.texcoords.end(), {
                            data.texcoords[size_t(tc[c]) * 2 + 0],
                            data.texcoords[size_t(tc[c]) * 2 + 1]});
                        else
                            current.texcoords.insert(current.texcoords.end(), {0.0f, 0.0f});
                        current.indices.push_back(local);
                    }
                }
            }
        }
        if (meshStarted || !current.indices.empty())
            data.meshes.push_back(std::move(current));
        if (data.meshes.empty())
            throw std::runtime_error("OBJ contains no faces");
        return data;
    }

    struct MeshLayout
    {
        int position = 0;
        int normal = -1;
        int uv = -1;
        uint32_t normalType = CMChunkMaterialInfo::VertexElement::kFormat_10_11_11;
        uint32_t uvType = CMChunkMaterialInfo::VertexElement::kFormat_16_16 |
            CMChunkMaterialInfo::VertexElement::kSigned | CMChunkMaterialInfo::VertexElement::kInteger;
        bool integerUV = false;
    };

    MeshLayout ResolveLayout(const CMChunkMaterialInfo& material, uint32_t vertexFormat, uint32_t stride)
    {
        using VE = CMChunkMaterialInfo::VertexElement;
        MeshLayout layout;
        if (const auto* declaration = material.FindDeclaration(vertexFormat))
        {
            bool hasPosition = false;
            for (const auto& element : declaration->elements)
            {
                if (element.stream != 0)
                    continue;
                if (element.usage == VE::kUsagePosition && element.index == 0)
                {
                    layout.position = element.offset;
                    hasPosition = true;
                }
                if (element.usage == VE::kUsageNormal && element.index == 0)
                {
                    layout.normal = element.offset;
                    layout.normalType = element.type;
                }
                if (element.usage == VE::kUsageTexCoord && element.index == material.uvSet)
                {
                    layout.uv = element.offset;
                    layout.uvType = element.type;
                }
            }
            if (!hasPosition)
                throw std::runtime_error("Material vertex declaration has no position");
        }
        else
        {
            int offset = 12;
            if (vertexFormat & 0x0002)
            {
                layout.normal = offset;
                offset += 4;
            }
            for (uint32_t flag : {0x0004u, 0x0008u, 0x0010u})
                if (vertexFormat & flag)
                    offset += 4;
            if ((vertexFormat >> 5) & 15)
                layout.uv = offset;
        }
        const uint32_t uvFormat = layout.uvType & VE::kFormatMask;
        layout.integerUV = layout.uv >= 0 && uvFormat == VE::kFormat_16_16 &&
            (layout.uvType & VE::kInteger) != 0;
        if (size_t(layout.position) + 12 > stride)
            throw std::runtime_error("Invalid position offset for mesh stride");
        return layout;
    }

    void EncodeVertex(std::vector<uint8_t>& buffer, size_t vertexBase, uint32_t stride, const MeshLayout& layout,
        float px, float py, float pz, float nx, float ny, float nz, float u, float v, bool littleEndian)
    {
        using VE = CMChunkMaterialInfo::VertexElement;
        if (vertexBase + stride > buffer.size())
            buffer.resize(vertexBase + stride, 0);
        WriteF32At(buffer, vertexBase + size_t(layout.position), px, littleEndian);
        WriteF32At(buffer, vertexBase + size_t(layout.position) + 4, py, littleEndian);
        WriteF32At(buffer, vertexBase + size_t(layout.position) + 8, pz, littleEndian);
        if (layout.normal >= 0)
        {
            const uint32_t normalFormat = layout.normalType & VE::kFormatMask;
            if (normalFormat == VE::kFormat_10_11_11)
                WriteU32At(buffer, vertexBase + size_t(layout.normal), PackNormal101111(nx, ny, nz), littleEndian);
            else if (normalFormat == VE::kFormat_32_32_32_FLOAT)
            {
                WriteF32At(buffer, vertexBase + size_t(layout.normal), nx, littleEndian);
                WriteF32At(buffer, vertexBase + size_t(layout.normal) + 4, ny, littleEndian);
                WriteF32At(buffer, vertexBase + size_t(layout.normal) + 8, nz, littleEndian);
            }
        }
        if (layout.uv >= 0)
        {
            const uint32_t uvFormat = layout.uvType & VE::kFormatMask;
            const bool uvSigned = (layout.uvType & VE::kSigned) != 0;
            const bool uvInteger = (layout.uvType & VE::kInteger) != 0;
            if (uvFormat == VE::kFormat_16_16)
            {
                auto pack = [&](float value) -> uint16_t {
                    if (uvInteger)
                    {
                        const int quantized = int(std::lround(value));
                        if (uvSigned)
                            return uint16_t(int16_t(std::clamp(quantized, -32768, 32767)));
                        return uint16_t(std::clamp(quantized, 0, 65535));
                    }
                    if (uvSigned)
                    {
                        const float clamped = std::clamp(value, -1.0f, 1.0f);
                        return uint16_t(int16_t(std::lround(clamped * 32767.0f)));
                    }
                    const float clamped = std::clamp(value, 0.0f, 1.0f);
                    return uint16_t(std::lround(clamped * 65535.0f));
                    };
                WriteU16At(buffer, vertexBase + size_t(layout.uv), pack(u), littleEndian);
                WriteU16At(buffer, vertexBase + size_t(layout.uv) + 2, pack(v), littleEndian);
            }
            else if (uvFormat == VE::kFormat_32_32_FLOAT)
            {
                WriteF32At(buffer, vertexBase + size_t(layout.uv), u, littleEndian);
                WriteF32At(buffer, vertexBase + size_t(layout.uv) + 4, v, littleEndian);
            }
        }
    }

    uint32_t ReadU32Chunk(const CMChunk& chunk, size_t offset)
    {
        size_t at = offset;
        return chunk.Read<uint32_t>(at);
    }

    void WriteU32Chunk(CMChunk& chunk, size_t offset, uint32_t value)
    {
        if (chunk.data.size() < offset + 4)
            throw std::runtime_error("Mesh descriptor is truncated");
        WriteBytesBE(chunk.data.data() + offset, &value, 4, chunk.isLittleEndian);
    }
}

ModelPreview::~ModelPreview()
{
    Clear();
}

void ModelPreview::Update(const PackageSession& session)
{
    const SelectionKey key = SelectionKey::Of(session);
    if (key == shown)
        return;
    const bool sameModel = key.SameChunk(shown);
    const float previousDistance = camera.distance;
    const int previousMaterial = selectedMaterial;
    Clear();
    shown = key;
    if (!key.package || !key.chunk)
        return;

    try
    {
        const auto resource = session.SelectedResource();
        if (!resource)
            return;
        const uint32_t type = resource->GetResourceType();
        if (type != kCMResourceType_Geometry && type != kCMResourceType_Hierarchy)
            return;
        active = true;
        name = resource->GetName();
        if (type == kCMResourceType_Hierarchy)
        {
            LoadHierarchy(*key.package, *resource, key.chunk);
            if (sameModel)
                camera.distance = previousDistance;
            Log("Hierarchy " + name + ": " + std::to_string(boneCount) + " bones.");
        }
        else
        {
            Load(*key.package, *resource);
            if (sameModel)
            {
                camera.distance = previousDistance;
                selectedMaterial = std::clamp(previousMaterial, 0, std::max(0, int(materials.size()) - 1));
            }
            Log("Model " + name + ": " + std::to_string(geometry.meshes.size()) + " meshes, " +
                std::to_string(vertexCount) + " vertices, " + std::to_string(triangleCount) +
                " triangles (static preview).");
        }
        InitializeAnimations(*key.package, *resource);
    }
    catch (const std::exception& ex)
    {
        geometry = RZGeometry();
        materials.clear();
        vertexCount = triangleCount = 0;
        bonePositions.clear();
        boneParents.clear();
        boneNames.clear();
        boneCount = 0;
        isHierarchy = false;
        active = true;
        error = ex.what();
        Log("Preview " + name + ": " + error);
    }
}

void ModelPreview::Load(const PKPackage& package, const CMChunkResourceHeader& resource)
{
    geometry = RZGeometry::Load(package, resource);
    size_t bytes = 0;
    for (const RZGeometry::Mesh& mesh : geometry.meshes)
    {
        bytes += mesh.ByteSize();
        vertexCount += mesh.VertexCount();
        triangleCount += mesh.TriangleCount();
    }
    if (bytes > kGeometryBudget)
        throw std::runtime_error("Model exceeds the 128 MiB preview budget.");

    materials.resize(geometry.materials.size());
    for (size_t i = 0; i < materials.size(); ++i)
        materials[i].packageTexture = RZTexture::FindHeader(package, geometry.materials[i].textureCRC,
            resource.GetLanguageMask(), resource.GetQualityLevel());
    camera.Frame(geometry.Bounds());
}

namespace
{
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

    Mat4 ReadMat4(const uint8_t* p, bool littleEndian)
    {
        Mat4 m;
        for (int i = 0; i < 16; ++i)
            m.m[i] = ReadF32(p + i * 4, littleEndian);
        return m;
    }

    constexpr uint32_t kHierarchyBones = 0x0385;
    constexpr uint32_t kHierarchyBoneNames = 0x038B;
    constexpr size_t kNameLookupEntry = 8;
    constexpr size_t kBoneRecord = 148;
    constexpr uint32_t kMaxBones = 512;
    constexpr size_t kBoneNameSize = 64;

    const CMChunk* FindChunkRecursive(const CMChunk& node, uint32_t maskedId)
    {
        if (node.GetMaskedID() == maskedId)
            return &node;
        for (const CMChunk& child : node.children)
        {
            if (const CMChunk* found = FindChunkRecursive(child, maskedId))
                return found;
        }
        return nullptr;
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

    const CMChunk* FindBonesChunk(const PKPackage& package, const CMChunkResourceHeader& resource,
        const CMChunk* selected)
    {
        if (selected)
        {
            if (selected->GetMaskedID() == kHierarchyBones)
                return selected;
            if (const CMChunk* found = FindChunkRecursive(*selected, kHierarchyBones))
                return found;
        }

        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* resourceRoot = FindResourceByCrc(root, resource.GetCRC()))
            {
                if (const CMChunk* found = FindChunkRecursive(*resourceRoot, kHierarchyBones))
                    return found;
            }
        }

        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* found = FindChunkRecursive(root, kHierarchyBones))
                return found;
        }
        return nullptr;
    }

    const CMChunk* FindNamesChunk(const PKPackage& package, const CMChunkResourceHeader& resource,
        const CMChunk* selected)
    {
        if (selected)
        {
            if (selected->GetMaskedID() == kHierarchyBoneNames)
                return selected;
            if (const CMChunk* found = FindChunkRecursive(*selected, kHierarchyBoneNames))
                return found;
        }
        for (const CMChunk& root : package.rootChunks)
        {
            if (const CMChunk* resourceRoot = FindResourceByCrc(root, resource.GetCRC()))
            {
                if (const CMChunk* found = FindChunkRecursive(*resourceRoot, kHierarchyBoneNames))
                    return found;
            }
        }
        return nullptr;
    }

    void ReadBoneNames(std::vector<std::string>& out, size_t count, const PKPackage& package,
        const CMChunkResourceHeader& resource, const CMChunk* selected, const std::vector<uint8_t>& packageBytes)
    {
        out.assign(count, {});
        const CMChunk* namesChunk = FindNamesChunk(package, resource, selected);
        if (!namesChunk)
            return;
        const size_t headerSize = namesChunk->HeaderSize();
        if (namesChunk->offset + headerSize + namesChunk->length > packageBytes.size())
            return;
        const uint8_t* nameData = packageBytes.data() + namesChunk->offset + headerSize;
        const size_t nameCount = namesChunk->length / kBoneNameSize;
        for (size_t i = 0; i < count && i < nameCount; ++i)
        {
            char buf[kBoneNameSize + 1]{};
            std::memcpy(buf, nameData + i * kBoneNameSize, kBoneNameSize);
            buf[kBoneNameSize] = '\0';
            out[i] = buf;
        }
    }
}

void ModelPreview::LoadHierarchy(const PKPackage& package, const CMChunkResourceHeader& resource,
    const CMChunk* selected)
{
    isHierarchy = true;
    const CMChunk* bonesChunk = FindBonesChunk(package, resource, selected);
    if (!bonesChunk)
        throw std::runtime_error("Hierarchy_Bones chunk not found.");

    const std::vector<uint8_t> packageBytes = package.Serialize();
    const size_t headerSize = bonesChunk->HeaderSize();
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (bonesChunk->offset + headerSize + bonesChunk->length <= packageBytes.size())
    {
        payload = packageBytes.data() + bonesChunk->offset + headerSize;
        payloadSize = bonesChunk->length;
    }
    else if (bonesChunk->offset + bonesChunk->length <= packageBytes.size() && bonesChunk->length > headerSize)
    {
        payload = packageBytes.data() + bonesChunk->offset + headerSize;
        payloadSize = bonesChunk->length - headerSize;
    }
    else if (bonesChunk->offset + bonesChunk->length <= packageBytes.size())
    {
        payload = packageBytes.data() + bonesChunk->offset;
        payloadSize = bonesChunk->length;
    }
    if (!payload || payloadSize < 12)
        throw std::runtime_error("Hierarchy_Bones chunk too small.");

    const bool littleEndian = package.isLittleEndian;
    const uint32_t count = ReadU32(payload, littleEndian);
    const size_t needed = 12 + size_t(count) * kNameLookupEntry + size_t(count) * kBoneRecord;
    if (count == 0 || count > kMaxBones || needed > payloadSize)
    {
        const uint32_t countBe = ReadU32(payload, false);
        const size_t neededBe = 12 + size_t(countBe) * kNameLookupEntry + size_t(countBe) * kBoneRecord;
        if (countBe > 0 && countBe <= kMaxBones && neededBe <= payloadSize)
        {
            const uint8_t* records = payload + 12 + size_t(countBe) * kNameLookupEntry;
            std::vector<Mat4> localBind(countBe);
            std::vector<uint32_t> parents(countBe);
            for (uint32_t i = 0; i < countBe; ++i)
            {
                const uint8_t* rec = records + size_t(i) * kBoneRecord;
                localBind[i] = ReadMat4(rec, false);
                parents[i] = ReadU32(rec + 128, false);
            }
            std::vector<Mat4> world(countBe);
            std::vector<bool> computed(countBe, false);
            auto computeWorld = [&](auto&& self, uint32_t index) -> const Mat4& {
                if (computed[index])
                    return world[index];
                const uint32_t parent = parents[index];
                if (parent >= countBe)
                    world[index] = localBind[index];
                else
                    world[index] = self(self, parent) * localBind[index];
                computed[index] = true;
                return world[index];
                };
            bonePositions.resize(countBe);
            boneParents = parents;
            BUAABB bounds;
            bool hasBounds = false;
            for (uint32_t i = 0; i < countBe; ++i)
            {
                const Mat4& w = computeWorld(computeWorld, i);
                bonePositions[i] = BUVector3(w.m[12], w.m[13], w.m[14]);
                if (!hasBounds)
                {
                    bounds.mMin = bounds.mMax = bonePositions[i];
                    hasBounds = true;
                }
                else
                {
                    bounds.mMin.x = std::min(bounds.mMin.x, bonePositions[i].x);
                    bounds.mMin.y = std::min(bounds.mMin.y, bonePositions[i].y);
                    bounds.mMin.z = std::min(bounds.mMin.z, bonePositions[i].z);
                    bounds.mMax.x = std::max(bounds.mMax.x, bonePositions[i].x);
                    bounds.mMax.y = std::max(bounds.mMax.y, bonePositions[i].y);
                    bounds.mMax.z = std::max(bounds.mMax.z, bonePositions[i].z);
                }
            }
            boneCount = countBe;
            ReadBoneNames(boneNames, countBe, package, resource, selected, packageBytes);
            camera.Frame(bounds);
            return;
        }
        throw std::runtime_error("Hierarchy_Bones data truncated (count=" + std::to_string(count) +
            ", payload=" + std::to_string(payloadSize) + ").");
    }

    const uint8_t* records = payload + 12 + size_t(count) * kNameLookupEntry;
    std::vector<Mat4> localBind(count);
    std::vector<uint32_t> parents(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* rec = records + size_t(i) * kBoneRecord;
        localBind[i] = ReadMat4(rec, littleEndian);
        parents[i] = ReadU32(rec + 128, littleEndian);
    }

    std::vector<Mat4> world(count);
    std::vector<bool> computed(count, false);
    auto computeWorld = [&](auto&& self, uint32_t index) -> const Mat4& {
        if (computed[index])
            return world[index];
        const uint32_t parent = parents[index];
        if (parent >= count)
            world[index] = localBind[index];
        else
            world[index] = self(self, parent) * localBind[index];
        computed[index] = true;
        return world[index];
        };

    bonePositions.resize(count);
    boneParents = parents;
    BUAABB bounds;
    bool hasBounds = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const Mat4& w = computeWorld(computeWorld, i);
        bonePositions[i] = BUVector3(w.m[12], w.m[13], w.m[14]);
        if (!hasBounds)
        {
            bounds.mMin = bounds.mMax = bonePositions[i];
            hasBounds = true;
        }
        else
        {
            bounds.mMin.x = std::min(bounds.mMin.x, bonePositions[i].x);
            bounds.mMin.y = std::min(bounds.mMin.y, bonePositions[i].y);
            bounds.mMin.z = std::min(bounds.mMin.z, bonePositions[i].z);
            bounds.mMax.x = std::max(bounds.mMax.x, bonePositions[i].x);
            bounds.mMax.y = std::max(bounds.mMax.y, bonePositions[i].y);
            bounds.mMax.z = std::max(bounds.mMax.z, bonePositions[i].z);
        }
    }
    boneCount = count;
    ReadBoneNames(boneNames, count, package, resource, selected, packageBytes);
    camera.Frame(bounds);
}

void ModelPreview::Clear()
{
    for (MaterialState& material : materials)
    {
        renderer.DeleteTexture(material.texture);
        renderer.DeleteTexture(material.retexture);
    }
    renderer.Release();
    materials.clear();
    geometry = RZGeometry();
    bonePositions.clear();
    boneParents.clear();
    boneNames.clear();
    boneCount = 0;
    isHierarchy = false;
    animationPlaying = false;
    animationTime = 0;
    selectedAnimation = -1;
    animationClip.reset();
    animationChoices.clear();
    animationResources.clear();
    animationSkeleton.reset();
    animationSkin.reset();
    bindGeometry.reset();
    shown = {};
    active = false;
    name.clear();
    error.clear();
    vertexCount = triangleCount = 0;
    selectedMaterial = 0;
    renderDirty = true;
}

void ModelPreview::Draw()
{
    ImGui::TextWrapped("%s", name.c_str());
    if (!error.empty())
    {
        ImGui::TextDisabled("Preview unavailable. See Debug log.");
        return;
    }
    try { AdvanceAnimation(ImGui::GetIO().DeltaTime); }
    catch (const std::exception& ex)
    {
        animationPlaying = false;
        Log("Animation: " + std::string(ex.what()));
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 32 || available.y < 32)
        return;

    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const float pixelWidth = available.x * scale.x;
    const float pixelHeight = available.y * scale.y;
    const float fit = std::min({1.0f, kMaxFrameWidth / pixelWidth, kMaxFrameHeight / pixelHeight});
    const int frameWidth = std::max(1, static_cast<int>(pixelWidth * fit));
    const int frameHeight = std::max(1, static_cast<int>(pixelHeight * fit));

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    const bool overlayHasInput = DrawOverlay(origin, available);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##modelview", available);
    HandleCameraInput(overlayHasInput);

    try
    {
        if (renderDirty || !renderer.HasFrame(frameWidth, frameHeight))
        {
            RenderFrame(frameWidth, frameHeight);
            renderDirty = false;
        }
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        Log("Render error: " + error);
        drawList->ChannelsMerge();
        return;
    }

    drawList->ChannelsSetCurrent(0);
    drawList->AddImage(static_cast<ImTextureID>(renderer.FrameTexture()), origin,
        ImVec2(origin.x + available.x, origin.y + available.y), ImVec2(0, 1), ImVec2(1, 0));

    if (isHierarchy && !bonePositions.empty() && ImGui::IsItemHovered())
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const BUVector3 eye = camera.Eye();
        const Mat4 view = Mat4::LookAt(eye, camera.target, BUVector3(0.0f, 1.0f, 0.0f));
        const Mat4 projection = Mat4::Perspective(0.785398f, available.x / std::max(available.y, 1.0f), 0.05f,
            camera.distance * 20.0f + 10.0f);
        const Mat4 mvp = projection * view;
        int best = -1;
        float bestDist = 12.0f;
        for (size_t i = 0; i < bonePositions.size(); ++i)
        {
            const BUVector3& p = bonePositions[i];
            const float x = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
            const float y = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
            const float w = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
            if (w <= 0.0f)
                continue;
            const float sx = origin.x + (x / w * 0.5f + 0.5f) * available.x;
            const float sy = origin.y + (1.0f - (y / w * 0.5f + 0.5f)) * available.y;
            const float dx = mouse.x - sx;
            const float dy = mouse.y - sy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0)
        {
            const std::string& boneName =
                best < static_cast<int>(boneNames.size()) && !boneNames[best].empty()
                ? boneNames[best]
                : ("bone " + std::to_string(best));
            ImGui::SetTooltip("%s", boneName.c_str());
        }
    }

    drawList->ChannelsMerge();
}

void ModelPreview::DrawFields() const
{
    if (!active || !error.empty())
        return;
    if (isHierarchy)
    {
        FieldRow("Hierarchy", "string", name);
        FieldRow("bones", "u32", std::to_string(boneCount));
        return;
    }
    FieldRow("Model", "string", name);
    FieldRow("meshes", "u32", std::to_string(geometry.meshes.size()));
    FieldRow("vertices", "u32", std::to_string(vertexCount));
    FieldRow("triangles", "u32", std::to_string(triangleCount));
}

std::vector<std::string> ModelPreview::TakeLogMessages()
{
    return std::exchange(logMessages, {});
}

void ModelPreview::SetTextured(bool enabled)
{
    textured = enabled;
    renderDirty = true;
}

void ModelPreview::LoadRetexture(size_t index, const std::string& path)
{
    if (index >= materials.size())
        throw std::runtime_error("Select a material first.");
    const auto png = ReadFile(std::filesystem::u8path(path));
    const BUPng::Image image = BUPng::Decode(png);
    MaterialState& material = materials[index];
    ModelRenderer::Texture texture = renderer.UploadTexture(image.rgba, image.width, image.height);
    renderer.DeleteTexture(material.retexture);
    material.retexture = texture;
    material.retexturePath = path;
    material.retextureCRC = BUCRC().GenerateRaw(png.data(), png.size());
    material.useRetexture = true;
    Log("PNG preview for " + geometry.materials[index].name + ": " + path + " (package unchanged).");
    SetTextured(true);
}

void ModelPreview::UsePackageTexture(size_t index)
{
    if (index >= materials.size())
        return;
    materials[index].useRetexture = false;
    Log("Using package texture for " + geometry.materials[index].name + ".");
    SetTextured(true);
}

void ModelPreview::UseRetexture(size_t index)
{
    if (index >= materials.size() || !materials[index].retexture.id)
        throw std::runtime_error("Load a retexture PNG first.");
    materials[index].useRetexture = true;
    Log("Using retexture for " + geometry.materials[index].name + ".");
    SetTextured(true);
}

void ModelPreview::ClearRetexture(size_t index)
{
    if (index >= materials.size())
        return;
    renderer.DeleteTexture(materials[index].retexture);
    materials[index].retexturePath.clear();
    UsePackageTexture(index);
}

void ModelPreview::ApplyRetexture(PackageSession& session, size_t index)
{
    if (SelectionKey::Of(session) != shown || index >= materials.size() || !materials[index].packageTexture)
        throw std::runtime_error("Select a material with a texture in this package first.");
    const MaterialState& material = materials[index];
    if (material.retexturePath.empty())
        throw std::runtime_error("Load a retexture PNG first.");
    const auto png = ReadFile(std::filesystem::u8path(material.retexturePath));
    if (BUCRC().GenerateRaw(png.data(), png.size()) != material.retextureCRC)
        throw std::runtime_error("Retexture changed on disk. Reload PNG to preview it before applying.");

    const CMChunkResourceHeader header = *material.packageTexture;
    const RZTexture texture = RZTexture::Load(*shown.package, header);
    ReplaceTexture(session, header, material.retexturePath, texture.desc.width, texture.desc.height);
    renderDirty = true;
}

void ModelPreview::ApplyMaterialFlags(PackageSession& session, size_t index, uint32_t flags)
{
    if (!session.package || !SelectionKey::Of(session).SameChunk(shown))
        throw std::runtime_error("Select the model material in this package first.");
    if (index >= geometry.materials.size())
        throw std::runtime_error("Select a material first.");

    const auto resource = session.SelectedResource();
    if (!resource || resource->GetResourceType() != kCMResourceType_Geometry)
        throw std::runtime_error("Material flags can only be edited on a Geometry resource.");

    auto matchesResource = [&](CMChunk& chunk) -> bool {
        const uint32_t id = chunk.GetMaskedID();
        if ((id != GenSub_Resource && id != Gen_PostLoadData) || !chunk.FindChild(Geo_Geometry))
            return false;
        const CMChunk* headerChunk = chunk.FindChild(GenSub_ResourceHeader);
        if (!headerChunk)
            return false;
        const CMChunkResourceHeader candidate(*headerChunk);
        return candidate.GetResourceType() == kCMResourceType_Geometry && candidate.IsSameResource(*resource);
        };

    CMChunk* resourceChunk = nullptr;
    for (CMChunk& root : session.package->rootChunks)
    {
        if (matchesResource(root))
        {
            resourceChunk = &root;
            break;
        }
        std::function<CMChunk*(CMChunk&)> findDesc = [&](CMChunk& node) -> CMChunk* {
            for (CMChunk& child : node.children)
            {
                if (matchesResource(child))
                    return &child;
                if (CMChunk* found = findDesc(child))
                    return found;
            }
            return nullptr;
            };
        resourceChunk = findDesc(root);
        if (resourceChunk)
            break;
    }
    if (!resourceChunk)
        throw std::runtime_error("Could not locate the geometry resource chunk for this model.");

    std::vector<CMChunk*> materialChunks;
    std::function<void(CMChunk&)> collect = [&](CMChunk& node) {
        for (CMChunk& child : node.children)
        {
            if (child.GetMaskedID() == Geo_MaterialInfo)
                materialChunks.push_back(&child);
            collect(child);
        }
        };
    collect(*resourceChunk);

    if (index >= materialChunks.size())
        throw std::runtime_error("Material index does not match any Geo_MaterialInfo chunk.");

    CMChunk& matChunk = *materialChunks[index];
    constexpr size_t kFlagsOffset = 32;
    if (matChunk.data.size() < kFlagsOffset + 4)
        throw std::runtime_error("Material chunk is too small to contain materialFlags.");

    uint8_t* p = matChunk.data.data() + kFlagsOffset;
    if (matChunk.isLittleEndian)
    {
        p[0] = uint8_t(flags);
        p[1] = uint8_t(flags >> 8);
        p[2] = uint8_t(flags >> 16);
        p[3] = uint8_t(flags >> 24);
    }
    else
    {
        p[0] = uint8_t(flags >> 24);
        p[1] = uint8_t(flags >> 16);
        p[2] = uint8_t(flags >> 8);
        p[3] = uint8_t(flags);
    }

    geometry.materials[index].flags = flags;

    session.MarkEdited("Material flags updated (CELLSHADING / RIM_LIGHTING). Save the package to write to disk.");
    Log("Material \"" + geometry.materials[index].name + "\" flags set to 0x" +
        ([&] {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%08X", flags);
            return std::string(buf);
            })());
}

void ModelPreview::ExportGeometryObj(const std::string& path) const
{
    if (geometry.meshes.empty())
        throw std::runtime_error("No meshes to export.");

    std::ostringstream out;
    out << "# pkzgui OBJ export\n";
    out << "# model " << name << "\n";
    out << "mtllib " << SanitizeFileName(name) << ".mtl\n";

    size_t vertexBase = 1;
    size_t uvBase = 1;
    size_t normalBase = 1;
    for (size_t mi = 0; mi < geometry.meshes.size(); ++mi)
    {
        const RZGeometry::Mesh& mesh = geometry.meshes[mi];
        const std::string matName = mesh.material < geometry.materials.size()
            ? geometry.materials[mesh.material].name
            : ("mat_" + std::to_string(mesh.material));
        out << "o mesh_" << mi << "\n";
        out << "usemtl " << matName << "\n";

        const size_t vertexCount = mesh.VertexCount();
        for (size_t v = 0; v < vertexCount; ++v)
        {
            out << "v " << mesh.positions[v * 3 + 0] << ' ' << mesh.positions[v * 3 + 1] << ' '
                << mesh.positions[v * 3 + 2] << '\n';
        }
        if (mesh.hasUV && mesh.texcoords.size() >= vertexCount * 2)
        {
            for (size_t v = 0; v < vertexCount; ++v)
                out << "vt " << mesh.texcoords[v * 2 + 0] << ' ' << mesh.texcoords[v * 2 + 1] << '\n';
        }
        if (mesh.normals.size() >= vertexCount * 3)
        {
            for (size_t v = 0; v < vertexCount; ++v)
                out << "vn " << mesh.normals[v * 3 + 0] << ' ' << mesh.normals[v * 3 + 1] << ' '
                << mesh.normals[v * 3 + 2] << '\n';
        }

        const bool writeUv = mesh.hasUV && mesh.texcoords.size() >= vertexCount * 2;
        const bool writeNormal = mesh.normals.size() >= vertexCount * 3;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            out << "f";
            for (size_t c = 0; c < 3; ++c)
            {
                const size_t idx = mesh.indices[i + c];
                out << ' ' << (vertexBase + idx);
                if (writeUv || writeNormal)
                {
                    out << '/';
                    if (writeUv)
                        out << (uvBase + idx);
                    if (writeNormal)
                        out << '/' << (normalBase + idx);
                }
            }
            out << '\n';
        }

        vertexBase += vertexCount;
        if (writeUv)
            uvBase += vertexCount;
        if (writeNormal)
            normalBase += vertexCount;
    }

    const std::string text = out.str();
    WriteFile(std::filesystem::u8path(path), std::vector<uint8_t>(text.begin(), text.end()));
}

const std::optional<CMChunkResourceHeader>& ModelPreview::PackageTexture(size_t material) const
{
    return materials.at(material).packageTexture;
}

void ModelPreview::HandleCameraInput(bool blocked)
{
    if (blocked)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)
            renderDirty = true;
        camera.Orbit(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        camera.Zoom(io.MouseWheel);
        renderDirty = true;
    }
}

void ModelPreview::RenderFrame(int width, int height)
{
    if (isHierarchy)
    {
        renderer.RenderSkeleton(bonePositions, boneParents, camera, width, height);
        return;
    }
    const auto styles = MeshStyles();
    if (animationSkin) renderer.UpdateVertices(geometry.meshes);
    renderer.Render(geometry.meshes, styles, camera, twoSided, width, height);
}

std::vector<ModelRenderer::MeshStyle> ModelPreview::MeshStyles()
{
    std::vector<ModelRenderer::MeshStyle> styles(geometry.meshes.size());
    for (size_t i = 0; i < styles.size(); ++i)
    {
        const RZGeometry::Mesh& mesh = geometry.meshes[i];
        const bool hasMaterial = mesh.material < geometry.materials.size();
        ModelRenderer::MeshStyle& style = styles[i];
        style.texture = textured && mesh.hasUV ? MaterialTexture(mesh.material) : 0;
        style.uvDivisor = mesh.integerUV ? uvDivisor : 1.0f;
        style.alphaCutout = hasMaterial && materials[mesh.material].alphaCutout;
        const float* color = style.texture && hasMaterial ? geometry.materials[mesh.material].color
            : kMeshPalette[mesh.material % std::size(kMeshPalette)];
        std::copy_n(color, 3, style.color);
    }
    return styles;
}

unsigned int ModelPreview::MaterialTexture(size_t index)
{
    if (index >= materials.size())
        return 0;
    MaterialState& material = materials[index];
    if (material.useRetexture && material.retexture.id)
        return material.retexture.id;
    if (material.textureRequested)
        return material.texture.id;
    material.textureRequested = true;
    if (!material.packageTexture || !shown.package)
        return 0;

    const CMChunkMaterialInfo& info = geometry.materials[index];
    try
    {
        const RZTexture texture = RZTexture::Load(*shown.package, *material.packageTexture);
        if (!texture.desc.Is2D() || texture.desc.width > ModelRenderer::kMaxTextureSize ||
            texture.desc.height > ModelRenderer::kMaxTextureSize)
            throw std::runtime_error("Unsupported or oversized material texture.");
        material.texture = renderer.UploadTexture(texture.DecodeLevel0(), texture.desc.width, texture.desc.height,
            info.addressMode == 1);
    }
    catch (const std::exception& ex)
    {
        Log("Texture " + info.name + ": " + ex.what());
    }
    return material.texture.id;
}

void ModelPreview::Log(std::string message)
{
    logMessages.push_back(std::move(message));
}
