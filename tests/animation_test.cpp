#include "animation/animation.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
void Check(bool condition, const char *message)
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
    catch (const std::exception &)
    {
        return true;
    }
    return false;
}
void Put32(CMChunk &c, size_t at, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        c.data.at(at + i) = uint8_t(value >> (c.isLittleEndian ? 8 * i : 24 - 8 * i));
}
PKPackage Fixture(bool little)
{
    PKPackage package;
    package.isLittleEndian = little;
    auto resource = CMChunk::Container(GenSub_Resource, 1);
    resource.AddChild(
        CMChunkResourceHeader::Build(99, kCMResourceType_HAnim, 31, 0, 4096, 0, "test clip", little));
    auto info = CMChunk::Leaf(0x4b2, 21, {});
    info.isLittleEndian = little;
    info.Append<uint32_t>(123);
    info.Append(1.f);
    info.Append(1.f / 30);
    info.data.resize(76);
    info.Append<uint32_t>(2);
    info.Append<uint32_t>(0);
    resource.AddChild(std::move(info));
    auto post = CMChunk::Container(Gen_PostLoadData, 1);
    post.offset = 4096;
    post.AddChild(
        CMChunkResourceHeader::Build(99, kCMResourceType_HAnim, 31, 0, 4096, 0, "test clip", little));
    auto table = CMChunk::Leaf(0x4bc, 1, {});
    table.isLittleEndian = little;
    table.Append<uint32_t>(0x10);
    table.Append<uint32_t>(1);
    table.Append<uint32_t>(0x20);
    table.Append<uint32_t>(0);
    post.AddChild(std::move(table));
    auto data = CMChunk::Leaf(0x4b1, 5, {});
    data.isLittleEndian = little;
    data.Append(0.f);
    data.Append<uint16_t>(2);
    data.Append<uint8_t>(1);
    data.Append<uint8_t>(0);
    data.Append<uint32_t>(0);
    for (int i = 0; i < 9; ++i)
        data.Append(0.f);
    auto align = [&](size_t n) {
        while (data.data.size() % n)
            data.Append<uint8_t>(0);
    };
    auto quat = [&] {
        align(2);
        for (int i = 0; i < 3; ++i)
            data.Append<int16_t>(0);
        data.Append<int16_t>(32767);
    };
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(0);
    data.Append<uint16_t>(0x39f);
    for (int i = 0; i < 3; ++i)
        data.Append<int16_t>(0);
    data.Append<int16_t>(3277);
    data.Append<int16_t>(0);
    data.Append<int16_t>(0);
    data.Append<uint8_t>(30);
    quat();
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(1);
    data.Append<uint16_t>(0x399);
    data.Append<int16_t>(0);
    data.Append<int16_t>(3277);
    data.Append<int16_t>(0);
    quat();
    align(4);
    Put32(data, 8, uint32_t(data.data.size() - 12));
    data.Append(1.f);
    data.Append<uint16_t>(0);
    data.Append<uint8_t>(0);
    data.Append<uint8_t>(255);
    data.Append<uint32_t>(0);
    post.AddChild(std::move(data));
    package.rootChunks.push_back(std::move(resource));
    package.rootChunks.push_back(std::move(post));
    return package;
}
animation::Skeleton Rig()
{
    animation::Skeleton rig;
    rig.bones.resize(3);
    rig.bones[0].crc = 0x20;
    rig.bones[1].crc = 0x10;
    rig.bones[1].parent = 0;
    rig.bones[2].crc = 0x30;
    rig.bones[2].parent = 1;
    rig.bones[2].bind.translation = {0, 0, 2, 0};
    return rig;
}
void DecodeAndMapping()
{
    for (bool little : {false, true})
    {
        auto package = Fixture(little);
        auto clip = animation::Clip::Load(package, package.rootChunks[0]);
        auto rig = Rig();
        Check(clip.Map(rig) == std::vector<int>({0, 1}), "CRC table order was mistaken for track order");
        auto pose = clip.Sample(rig, .5f);
        auto world = rig.WorldPose(pose);
        Check(Near(pose[0].translation[0], .5f), "Packed translation interpolation is wrong");
        Check(Near(world[1].m[12], .5f) && Near(world[1].m[13], 1.f), "Parent motion was not composed");
        Check(Near(world[2].m[14], 2.f), "Unanimated bone lost its bind pose");
        Check(Near(pose[0].scale[0], 1) && Near(pose[0].rotation[3], 1),
              "Identity channels were decoded incorrectly");
        rig.bones[1].crc = 0x40;
        Check(clip.Map(rig) == std::vector<int>({0, -1}), "Missing CRC was silently mapped to a bone");
        rig.bones[1].crc = 0x20;
        Check(Rejects([&] { clip.Map(rig); }), "Duplicate skeleton CRCs were accepted");
        package.rootChunks[1].children.back().data.resize(20);
        Check(Rejects([&] { animation::Clip::Load(package, package.rootChunks[0]); }),
              "Truncated keys were accepted");
    }
    auto rig = Rig();
    rig.bones[0].parent = 1;
    std::vector<animation::Transform> pose(3);
    Check(Rejects([&] { rig.WorldPose(pose); }), "Cyclic skeleton would recurse forever");
    auto package = Fixture(false);
    auto *table = package.rootChunks[1].FindChild(0x4bc);
    Put32(*table, 8, 0x10);
    Check(Rejects([&] { animation::Clip::Load(package, package.rootChunks[0]); }),
          "Ambiguous animation CRCs were accepted");
}
void SkinningAndRotation()
{
    auto rig = Rig();
    rig.bones.resize(2);
    rig.bones[1].parent = UINT32_MAX;
    animation::Clip clip;
    clip.duration = 1;
    clip.tracks.resize(1);
    clip.tracks[0].boneCRC = rig.bones[1].crc;
    animation::Segment rotation;
    rotation.start = 0;
    rotation.end = 1;
    rotation.interpolate = true;
    rotation.from = {0, 0, 0, 1};
    rotation.to = {0, 0, 1, 0};
    clip.tracks[0].rotation.push_back(rotation);
    auto world = rig.WorldPose(clip.Sample(rig, .5f));
    Check(Near(world[1].m[0], 0) && Near(world[1].m[1], 1),
          "Quaternion interpolation did not rotate 90 degrees");
    animation::Skin skin;
    skin.meshes.resize(1);
    skin.meshes[0].resize(1);
    skin.meshes[0][0].bones = {1, 0, 0, 0};
    skin.meshes[0][0].weights = {1, 0, 0, 0};
    RZGeometry geometry;
    geometry.meshes.resize(1);
    geometry.meshes[0].positions = {1, 0, 0};
    geometry.meshes[0].normals = {1, 0, 0};
    auto output = geometry;
    skin.Apply(rig, world, geometry, output);
    Check(Near(output.meshes[0].positions[0], 0) && Near(output.meshes[0].positions[1], 1),
          "Mesh did not follow its mapped bone");
    Check(Near(output.meshes[0].normals[0], 0) && Near(output.meshes[0].normals[1], 1),
          "Mesh normals did not follow the bone");
    Check(geometry.meshes[0].positions == std::vector<float>({1, 0, 0}), "Skinning overwrote bind vertices");
    world[1] = Mat4{};
    world[1].m[0] = 2;
    geometry.meshes[0].normals = {1, 1, 0};
    skin.Apply(rig, world, geometry, output);
    Check(Near(output.meshes[0].normals[0], 1 / std::sqrt(5.f)) &&
              Near(output.meshes[0].normals[1], 2 / std::sqrt(5.f)),
          "Nonuniform bone scale did not use inverse-transpose normals");
    skin.meshes[0][0].bones[0] = 20;
    Check(Rejects([&] { skin.Apply(rig, world, geometry, output); }),
          "Invalid skin palette index was accepted");
}
void SkinLayouts()
{
    for (const auto format : {0x1603u, 0x1627u})
        for (bool little : {false, true})
        {
            PKPackage package;
            package.isLittleEndian = little;
            auto source = CMChunk::Container(GenSub_Resource, 1);
            source.AddChild(CMChunkResourceHeader::Build(1, kCMResourceType_Geometry, 31, 0, 4096, 0,
                                                        "shared costume", little));
            auto geo = CMChunk::Container(Geo_Geometry, 3);
            auto lod = CMChunk::Container(Geo_GeomLOD, 3);
            auto info = CMChunk::Leaf(Geo_GeomLODInfo, 6, std::vector<uint8_t>(24));
            info.isLittleEndian = little;
            lod.AddChild(info);
            auto descriptor = CMChunk::Leaf(Geo_GeomMesh, 8, {});
            descriptor.isLittleEndian = little;
            const uint32_t stride = format == 0x1603 ? 24 : 32;
            for (uint32_t value : {0u, 0u, stride, format, 1u, 0u, 3u, 2u, UINT32_MAX, 2u, 4u, 0u, 0u, 1u})
                descriptor.Append(value);
            lod.AddChild(descriptor);
            geo.AddChild(lod);
            source.AddChild(geo);
            auto post = CMChunk::Container(Gen_PostLoadData, 1);
            post.offset = 4096;
            post.AddChild(source.children[0]);
            auto buffers = CMChunk::Container(Geo_GeomLOD, 3);
            buffers.AddChild(info);
            auto vertices = CMChunk::Leaf(GenSub_VertexList, 1, std::vector<uint8_t>(stride));
            vertices.isLittleEndian = little;
            Put32(vertices, stride - 8, 0x00000100);
            Put32(vertices, stride - 4, 0x000040BF);
            buffers.AddChild(vertices);
            post.AddChild(buffers);
            package.rootChunks = {source, post};
            RZGeometry geometry;
            geometry.meshes.resize(1);
            geometry.meshes[0].positions = {0, 0, 0};
            geometry.meshes[0].normals = {0, 1, 0};
            geometry.materials.resize(1); // Shader pass does not declare the appended skin inputs.
            auto rig = Rig();
            auto header = CMChunkResourceHeader(source.children[0]);
            const auto skin = animation::Skin::Load(package, header, geometry, rig);
            Check(skin.meshes[0][0].bones[0] == 0 && skin.meshes[0][0].bones[1] == 1,
                  "Shared costume palette decoded incorrectly");
            Check(Near(skin.meshes[0][0].weights[0], 191.f / 255) &&
                      Near(skin.meshes[0][0].weights[1], 64.f / 255),
                  "Appended skin weights decoded incorrectly");
            rig.bones.resize(1);
            Check(Rejects([&] { animation::Skin::Load(package, header, geometry, rig); }),
                  "Shared rig with a missing palette bone was accepted");
            rig = Rig();
            Put32(package.rootChunks[0].children[1].children[0].children[1], 12, format & ~0x1000u);
            Check(Rejects([&] { animation::Skin::Load(package, header, geometry, rig); }),
                  "Unskinned layout was guessed to contain skin weights");
        }
}
void PreviewPlacement()
{
    animation::Transform transform;
    transform.translation = {-1.688f, -1.612f, -.290f, 0};
    transform.rotation = {0, 0, std::sin(.4f), std::cos(.4f)};
    transform.scale = {2, 3, 4, 0};
    const auto identity = transform.InverseMatrix() * transform.Matrix();
    for (int i = 0; i < 16; ++i)
        Check(Near(identity.m[i], i % 5 == 0 ? 1.f : 0.f), "Root transform inverse is incorrect");
    transform.scale[1] = 0;
    Check(Rejects([&] { transform.InverseMatrix(); }), "Singular root alignment was accepted");

    auto rig = Rig();
    rig.bones.resize(2);
    rig.bones[0].parent = 1;
    rig.bones[1].parent = UINT32_MAX; // The actor root need not be the first bone.
    rig.bones[0].bind.translation = {0, 1, 0, 0};
    animation::Clip clip;
    clip.duration = 1;
    clip.tracks.resize(1);
    auto &track = clip.tracks[0];
    track.boneCRC = rig.bones[1].crc;
    animation::Segment translation;
    translation.start = 0;
    translation.end = 1;
    translation.interpolate = true;
    translation.from = {-1.688f, -1.612f, -.290f, 0};
    translation.to = {-1.688f, -.612f, -.290f, 0};
    track.translation.push_back(translation);
    animation::Segment rotation;
    rotation.start = 0;
    rotation.end = 1;
    rotation.interpolate = true;
    rotation.from = {0, 0, std::sin(.4f), std::cos(.4f)};
    rotation.to = {0, 0, std::sin(.6f), std::cos(.6f)};
    track.rotation.push_back(rotation);
    const auto placement = clip.PreviewPlacement(rig);
    const auto raw = rig.WorldPose(clip.Sample(rig, 0));
    const auto alignedRoot = placement * raw[1];
    for (int i = 0; i < 16; ++i)
        Check(Near(alignedRoot.m[i], i % 5 == 0 ? 1.f : 0.f),
              "Initial scene rotation or translation leaked into the preview");
    const auto alignedChild = placement * raw[0];
    Check(Near(alignedChild.m[12], 0) && Near(alignedChild.m[13], 1),
          "Root alignment changed the local skeletal pose");
    const auto later = placement * rig.WorldPose(clip.Sample(rig, 1))[1];
    Check(Near(later.m[12], std::sin(.8f)) && Near(later.m[13], std::cos(.8f)) &&
              Near(later.m[0], std::cos(.4f)),
          "Preview alignment removed subsequent root movement or turning");
    Check(Near(raw[1].m[13], -1.612f), "Alignment changed the original animation coordinates");
    rig.bones[0].parent = UINT32_MAX;
    const auto independent = clip.PreviewPlacement(rig);
    Check(Near(independent.m[12], 0) && Near(independent.m[13], 0) && Near(independent.m[0], 1),
          "An arbitrary root was chosen for independent hierarchies");
}
} // namespace
int main()
{
    try
    {
        DecodeAndMapping();
        SkinningAndRotation();
        SkinLayouts();
        PreviewPlacement();
        std::puts("PASS: EOT packed keys, CRC mapping, bind pose, hierarchy cycles and CPU skinning");
    }
    catch (const std::exception &ex)
    {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }
}
