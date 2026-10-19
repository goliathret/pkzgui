#include "animation/animation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace animation
{
namespace
{
struct Reader
{
    const std::vector<uint8_t> &data;
    bool little;
    size_t at = 0, limit;
    Reader(const std::vector<uint8_t> &bytes, bool le) : data(bytes), little(le), limit(bytes.size())
    {
    }
    void Require(size_t n) const
    {
        if (at > limit || n > limit - at)
            throw std::runtime_error("Animation data is truncated.");
    }
    void Skip(size_t n)
    {
        Require(n);
        at += n;
    }
    void Align(size_t n)
    {
        Skip((n - at % n) % n);
    }
    uint8_t U8()
    {
        Require(1);
        return data[at++];
    }
    uint16_t U16()
    {
        uint16_t a = U8(), b = U8();
        return little ? a | (b << 8) : (a << 8) | b;
    }
    uint32_t U32()
    {
        uint32_t a = U16(), b = U16();
        return little ? a | (b << 16) : (a << 16) | b;
    }
    float F32()
    {
        const uint32_t bits = U32();
        float value;
        std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value))
            throw std::runtime_error("Animation contains a non-finite value.");
        return value;
    }
};
Value Normalize(Value q)
{
    float n = 0;
    for (float x : q)
        n += x * x;
    if (n < 1e-12f)
        throw std::runtime_error("Animation contains an invalid quaternion.");
    for (float &x : q)
        x /= std::sqrt(n);
    return q;
}
Value Interpolate(Value a, Value b, float t, bool rotation)
{
    float dot = 0;
    if (rotation)
    {
        a = Normalize(a);
        b = Normalize(b);
        for (int i = 0; i < 4; ++i)
            dot += a[i] * b[i];
        if (dot < 0)
        {
            for (float &x : b)
                x = -x;
            dot = -dot;
        }
    }
    float left = 1 - t, right = t;
    if (rotation && dot < .9995f)
    {
        const float angle = std::acos(std::clamp(dot, -1.f, 1.f));
        left = std::sin((1 - t) * angle) / std::sin(angle);
        right = std::sin(t * angle) / std::sin(angle);
    }
    Value value{};
    for (int i = 0; i < 4; ++i)
        value[i] = left * a[i] + right * b[i];
    return rotation ? Normalize(value) : value;
}
Value SampleChannel(const std::vector<Segment> &keys, float time, const Value &fallback, bool rotation)
{
    auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                 [](float t, const Segment &key) { return t < key.start; });
    if (next == keys.begin())
        return fallback;
    const Segment &key = *std::prev(next);
    if (!key.interpolate)
        return key.from;
    const float t =
        key.end > key.start ? std::clamp((time - key.start) / (key.end - key.start), 0.f, 1.f) : 1.f;
    return Interpolate(key.from, key.to, t, rotation);
}
void Channel(Reader &r, std::vector<Segment> &keys, uint16_t flags, int kind, float time, float interval)
{
    // EOT v5 readers: 0x821A8F18 (scale), 0x821A9130 (position), 0x821A9420 (quaternion).
    const uint16_t current = kind == 0 ? 1 : kind == 1 ? 512 : 16;
    const uint16_t future = kind == 0 ? 2 : kind == 1 ? 1024 : 32;
    const uint16_t timed = kind == 0 ? 4 : kind == 1 ? 2048 : 64;
    if (!(flags & (current | future)))
        return;
    Segment key;
    key.start = time;
    key.end = time + interval;
    key.from = kind == 1 ? Value{0, 0, 0, 1} : kind == 2 ? Value{1, 1, 1, 0} : Value{};
    if (!keys.empty())
        key.from = keys.back().interpolate ? keys.back().to : keys.back().from;
    auto read = [&]() {
        Value value{};
        if (kind == 1)
        {
            r.Align(2);
            for (float &x : value)
                x = int16_t(r.U16()) / 32767.f;
            return Normalize(value);
        }
        const bool byte = kind == 0 && (flags & 8192);
        const bool half = (flags & (kind == 0 ? 8 : 128)) != 0;
        if (!byte)
            r.Align(half ? 2 : 4);
        for (int i = 0; i < 3; ++i)
            value[i] = byte   ? int8_t(r.U8()) * (.05f / 127.f)
                       : half ? int16_t(r.U16()) * (kind == 0 ? 10.f / 32767.f : 1.f / 16383.f)
                              : r.F32();
        return value;
    };
    if (flags & current)
        key.from = kind == 2 && (flags & 256) ? Value{1, 1, 1, 0} : read();
    key.interpolate = (flags & future) != 0;
    key.to = key.interpolate ? read() : key.from;
    if (flags & timed)
        key.end = time + r.U8() * interval;
    keys.push_back(key);
}
void Decode(Clip &clip, const CMChunk &chunk, bool little)
{
    if (chunk.version != 5)
        throw std::runtime_error("Only EOT v5 animation tracks are supported.");
    Reader r(chunk.data, little);
    while (r.at < r.data.size())
    {
        r.limit = r.data.size();
        r.Require(12);
        const size_t begin = r.at;
        const float time = r.F32();
        const uint16_t count = r.U16();
        const uint8_t flags = r.U8(), morphs = r.U8();
        const uint32_t size = r.U32();
        r.Require(size);
        const size_t end = r.at + size;
        r.limit = end;
        if (morphs == 255)
            break;
        if (time < 0 || time > clip.duration + clip.interval + .01f)
            throw std::runtime_error("Animation frame time is out of range.");
        if (flags & ~3)
            throw std::runtime_error("Unknown animation frame flags.");
        if (flags & 1)
            r.Skip(36);
        if (flags & 2)
        {
            const uint32_t root = r.U32();
            if (root & ~127u)
                throw std::runtime_error("Unknown animation root-motion flags.");
            for (int bit = 0; bit < 7; ++bit)
                if (root & (1u << bit))
                    r.Skip(12);
        }
        for (uint16_t i = 0; i < count; ++i)
        {
            const uint8_t control = r.U8(), index = r.U8();
            uint16_t bits = 0;
            if (index >= clip.tracks.size())
                throw std::runtime_error("Animation track index is out of range.");
            if (!control)
            {
                r.Align(2);
                bits = r.U16();
            }
            else if ((control & 3) == 1)
                bits = (control & 124) << 2;
            else if ((control & 3) == 2)
                bits = (control & 28) << 7;
            else if ((control & 3) == 3)
                bits = ((control & 60) >> 2) | ((control & 64) << 7);
            else
                throw std::runtime_error("Unknown animation track encoding.");
            auto &track = clip.tracks[index];
            Channel(r, track.scale, bits, 2, time, clip.interval);
            Channel(r, track.translation, bits, 0, time, clip.interval);
            Channel(r, track.rotation, bits, 1, time, clip.interval);
        }
        r.Align(4);
        if (!morphs && r.at != end)
            throw std::runtime_error("Animation frame size does not match its tracks.");
        r.at = end;
        if (r.at <= begin)
            throw std::runtime_error("Invalid animation frame size.");
    }
}
} // namespace
const CMChunk *Find(const CMChunk &chunk, uint32_t id)
{
    if (chunk.GetMaskedID() == id)
        return &chunk;
    for (const auto &child : chunk.children)
        if (auto *found = Find(child, id))
            return found;
    return nullptr;
}
const CMChunk *Resource(const PKPackage &package, const CMChunkResourceHeader &header)
{
    return package.FindChunkIf([&](const CMChunk &chunk) {
        if (chunk.GetMaskedID() != GenSub_Resource)
            return false;
        const auto *h = chunk.FindChild(GenSub_ResourceHeader);
        return h && CMChunkResourceHeader(*h).IsSameResource(header);
    });
}
Mat4 Transform::Matrix() const
{
    const Value q = Normalize(rotation);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m;
    m.m[0] = (1 - 2 * y * y - 2 * z * z) * scale[0];
    m.m[1] = (2 * x * y + 2 * z * w) * scale[0];
    m.m[2] = (2 * x * z - 2 * y * w) * scale[0];
    m.m[4] = (2 * x * y - 2 * z * w) * scale[1];
    m.m[5] = (1 - 2 * x * x - 2 * z * z) * scale[1];
    m.m[6] = (2 * y * z + 2 * x * w) * scale[1];
    m.m[8] = (2 * x * z + 2 * y * w) * scale[2];
    m.m[9] = (2 * y * z - 2 * x * w) * scale[2];
    m.m[10] = (1 - 2 * x * x - 2 * y * y) * scale[2];
    m.m[12] = translation[0];
    m.m[13] = translation[1];
    m.m[14] = translation[2];
    return m;
}
Mat4 Transform::InverseMatrix() const
{
    Transform inverse;
    inverse.rotation = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    Mat4 result = inverse.Matrix();
    for (int row = 0; row < 3; ++row)
    {
        if (!std::isfinite(scale[row]) || std::abs(scale[row]) < 1e-8f)
            throw std::runtime_error("Cannot align an animation with a singular root scale.");
        for (int column = 0; column < 3; ++column)
            result.m[column * 4 + row] /= scale[row];
        result.m[12 + row] = -(result.m[row] * translation[0] + result.m[4 + row] * translation[1] +
                               result.m[8 + row] * translation[2]);
    }
    return result;
}
Skeleton Skeleton::Load(const CMChunk &resource, bool little)
{
    const auto *header = resource.FindChild(GenSub_ResourceHeader);
    const auto *bones = Find(resource, 0x385);
    const auto *trs = Find(resource, 0x389);
    const auto *names = Find(resource, 0x38b);
    if (!header || !bones || bones->version != 9)
        throw std::runtime_error("Missing EOT v9 hierarchy bones.");
    Skeleton result;
    CMChunkResourceHeader h(*header);
    result.name = h.GetName();
    result.crc = h.GetCRC();
    Reader r(bones->data, little);
    const size_t count = r.U32();
    r.Skip(8);
    if (!count || count > 512)
        throw std::runtime_error("Invalid hierarchy bone count.");
    r.Require(count * (8 + 148));
    result.bones.resize(count);
    std::unordered_set<uint32_t> hashes, indices;
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t hash = r.U32(), index = r.U32();
        if (index >= count || !hashes.insert(hash).second || !indices.insert(index).second)
            throw std::runtime_error("Invalid or ambiguous hierarchy bone CRC table.");
        result.bones[index].crc = hash;
    }
    for (size_t i = 0; i < count; ++i)
    {
        Bone &bone = result.bones[i];
        for (float &x : bone.localBind.m)
            x = r.F32();
        for (float &x : bone.inverseBind.m)
            x = r.F32();
        bone.parent = r.U32();
        r.Skip(16);
        if (bone.parent != UINT32_MAX && bone.parent >= count)
            throw std::runtime_error("Hierarchy parent index is out of range.");
        if (names && names->data.size() / 64 > i)
        {
            const char *text = reinterpret_cast<const char *>(names->data.data() + i * 64);
            bone.name.assign(text, strnlen(text, 64));
        }
    }
    if (!trs || trs->version != 3 || trs->data.size() != count * 40)
        throw std::runtime_error("Animation requires a complete v3 hierarchy bind pose.");
    Reader t(trs->data, little);
    for (auto &bone : result.bones)
    {
        for (int i = 0; i < 3; ++i)
            bone.bind.translation[i] = t.F32();
        for (float &x : bone.bind.rotation)
            x = t.F32();
        bone.bind.rotation = Normalize(bone.bind.rotation);
        for (int i = 0; i < 3; ++i)
            bone.bind.scale[i] = t.F32();
    }
    std::vector<Transform> bind;
    for (auto &bone : result.bones)
        bind.push_back(bone.bind);
    result.WorldPose(bind);
    return result;
}
std::vector<Mat4> Skeleton::WorldPose(const std::vector<Transform> &local) const
{
    if (local.size() != bones.size())
        throw std::runtime_error("Pose size does not match the skeleton.");
    std::vector<Mat4> world(bones.size());
    std::vector<uint8_t> visited(bones.size());
    std::function<void(size_t)> visit = [&](size_t i) {
        if (visited[i] == 2)
            return;
        if (visited[i] == 1)
            throw std::runtime_error("Hierarchy contains a parent cycle.");
        visited[i] = 1;
        const uint32_t parent = bones[i].parent;
        if (parent != UINT32_MAX)
        {
            if (parent >= bones.size())
                throw std::runtime_error("Invalid bone parent.");
            visit(parent);
        }
        world[i] = parent == UINT32_MAX ? local[i].Matrix() : world[parent] * local[i].Matrix();
        visited[i] = 2;
    };
    for (size_t i = 0; i < bones.size(); ++i)
        visit(i);
    return world;
}
Clip Clip::Load(const PKPackage &package, const CMChunk &resource, bool decode)
{
    const auto *h = resource.FindChild(GenSub_ResourceHeader);
    const auto *info = Find(resource, 0x4b2);
    if (!h || !info || info->version != 21)
        throw std::runtime_error("Missing EOT v21 animation info.");
    CMChunkResourceHeader header(*h);
    Clip clip;
    clip.name = header.GetName();
    const auto *post = package.FindRootAtOffset(header.GetDataOffset());
    const auto *ph = post ? post->FindChild(GenSub_ResourceHeader) : nullptr;
    if (!ph || !CMChunkResourceHeader(*ph).IsSameResource(header))
        throw std::runtime_error("Animation post-load resource does not match.");
    Reader r(info->data, package.isLittleEndian);
    clip.hierarchyCRC = r.U32();
    clip.duration = r.F32();
    clip.interval = r.F32();
    if (clip.duration < 0 || clip.duration > 86400 || clip.interval <= 0 || clip.interval > 1)
        throw std::runtime_error("Invalid animation timing.");
    r.at = 76;
    const size_t count = r.U32();
    if (count > 255)
        throw std::runtime_error("Too many animation tracks.");
    clip.tracks.resize(count);
    const auto *crcs = Find(*post, 0x4bc);
    if (count && (!crcs || crcs->data.size() != count * 8))
        throw std::runtime_error("Animation bone CRC table is missing or truncated.");
    if (count)
    {
        Reader table(crcs->data, package.isLittleEndian);
        std::unordered_set<uint32_t> hashes, indices;
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t crc = table.U32(), index = table.U32();
            if (index >= count || !indices.insert(index).second || !hashes.insert(crc).second)
                throw std::runtime_error("Ambiguous animation bone mapping.");
            clip.tracks[index].boneCRC = crc;
        }
    }
    if (!decode)
        return clip;
    size_t buffers = 0;
    std::function<void(const CMChunk &)> read = [&](const CMChunk &c) {
        if (c.GetMaskedID() == 0x4b1)
        {
            Decode(clip, c, package.isLittleEndian);
            ++buffers;
        }
        for (auto &child : c.children)
            read(child);
    };
    read(*post);
    if (!buffers)
        throw std::runtime_error("Animation keyframe buffers are missing.");
    for (auto &track : clip.tracks)
        for (auto *channel : {&track.translation, &track.rotation, &track.scale})
            std::stable_sort(channel->begin(), channel->end(),
                             [](const Segment &a, const Segment &b) { return a.start < b.start; });
    return clip;
}
std::vector<int> Clip::Map(const Skeleton &skeleton) const
{
    std::unordered_map<uint32_t, int> bones;
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (!bones.emplace(skeleton.bones[i].crc, int(i)).second)
            throw std::runtime_error("Ambiguous skeleton bone CRC.");
    std::vector<int> map;
    for (auto &track : tracks)
    {
        auto it = bones.find(track.boneCRC);
        map.push_back(it == bones.end() ? -1 : it->second);
    }
    return map;
}
Mat4 Clip::PreviewPlacement(const Skeleton &skeleton) const
{
    size_t root = skeleton.bones.size();
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (skeleton.bones[i].parent == UINT32_MAX)
        {
            if (root != skeleton.bones.size())
                return Mat4{}; // Multiple independent roots have no single actor placement.
            root = i;
        }
    if (root == skeleton.bones.size())
        return Mat4{};
    const auto first = Sample(skeleton, 0);
    return skeleton.bones[root].bind.Matrix() * first[root].InverseMatrix();
}
std::vector<Transform> Clip::Sample(const Skeleton &skeleton, float time) const
{
    if (!std::isfinite(time))
        throw std::runtime_error("Invalid animation sample time.");
    time = std::clamp(time, 0.f, duration);
    auto map = Map(skeleton);
    std::vector<Transform> pose;
    for (auto &bone : skeleton.bones)
        pose.push_back(bone.bind);
    for (size_t i = 0; i < tracks.size(); ++i)
        if (map[i] >= 0)
        {
            const auto &track = tracks[i];
            auto &transform = pose[size_t(map[i])];
            transform.translation = SampleChannel(track.translation, time, transform.translation, false);
            transform.rotation = SampleChannel(track.rotation, time, transform.rotation, true);
            transform.scale = SampleChannel(track.scale, time, transform.scale, false);
        }
    return pose;
}
} // namespace animation
