#include "model/gltf_export.h"
#include "platform/platform.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace
{

    std::string JStr(const std::string& s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
                o += '\\';
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            }
            else
                o += char(c);
        }
        return o + '"';
    }

    std::string JFloat(float v)
    {
        if (!std::isfinite(v))
            v = 0.f;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.9g", double(v));
        return buf;
    }

    std::string JMatrix(const BUMatrix& m)
    {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << '[';
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                if (r || c)
                    o << ',';
                o << JFloat(m.m[r][c]);
            }
        o << ']';
        return o.str();
    }

    BUMatrix InvertRigid(const BUMatrix& m)
    {
        BUMatrix out{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                out.m[r][c] = m.m[c][r];
        const float tx = m.m[3][0], ty = m.m[3][1], tz = m.m[3][2];
        out.m[3][0] = -(out.m[0][0] * tx + out.m[1][0] * ty + out.m[2][0] * tz);
        out.m[3][1] = -(out.m[0][1] * tx + out.m[1][1] * ty + out.m[2][1] * tz);
        out.m[3][2] = -(out.m[0][2] * tx + out.m[1][2] * ty + out.m[2][2] * tz);
        out.m[0][3] = out.m[1][3] = out.m[2][3] = 0.f;
        out.m[3][3] = 1.f;
        return out;
    }

    void Decompose(const BUMatrix& m, float t[3], float q[4], float s[3])
    {
        t[0] = m.m[3][0];
        t[1] = m.m[3][1];
        t[2] = m.m[3][2];

        s[0] = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2]);
        s[1] = std::sqrt(m.m[1][0] * m.m[1][0] + m.m[1][1] * m.m[1][1] + m.m[1][2] * m.m[1][2]);
        s[2] = std::sqrt(m.m[2][0] * m.m[2][0] + m.m[2][1] * m.m[2][1] + m.m[2][2] * m.m[2][2]);

        const float det =
            m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
            m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
            m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
        if (det < 0.f)
            s[0] = -s[0];

        for (int k = 0; k < 3; ++k)
            if (std::fabs(s[k]) < 1e-8f)
                s[k] = 1.f;

        const float r00 = m.m[0][0] / s[0], r01 = m.m[1][0] / s[1], r02 = m.m[2][0] / s[2];
        const float r10 = m.m[0][1] / s[0], r11 = m.m[1][1] / s[1], r12 = m.m[2][1] / s[2];
        const float r20 = m.m[0][2] / s[0], r21 = m.m[1][2] / s[1], r22 = m.m[2][2] / s[2];

        const float tr = r00 + r11 + r22;
        if (tr > 0.f)
        {
            const float n = std::sqrt(tr + 1.f) * 2.f;
            q[3] = 0.25f * n;
            q[0] = (r21 - r12) / n;
            q[1] = (r02 - r20) / n;
            q[2] = (r10 - r01) / n;
        }
        else if (r00 > r11 && r00 > r22)
        {
            const float n = std::sqrt(1.f + r00 - r11 - r22) * 2.f;
            q[3] = (r21 - r12) / n;
            q[0] = 0.25f * n;
            q[1] = (r01 + r10) / n;
            q[2] = (r02 + r20) / n;
        }
        else if (r11 > r22)
        {
            const float n = std::sqrt(1.f + r11 - r00 - r22) * 2.f;
            q[3] = (r02 - r20) / n;
            q[0] = (r01 + r10) / n;
            q[1] = 0.25f * n;
            q[2] = (r12 + r21) / n;
        }
        else
        {
            const float n = std::sqrt(1.f + r22 - r00 - r11) * 2.f;
            q[3] = (r10 - r01) / n;
            q[0] = (r02 + r20) / n;
            q[1] = (r12 + r21) / n;
            q[2] = 0.25f * n;
        }

        const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (len > 1e-8f)
        {
            q[0] /= len;
            q[1] /= len;
            q[2] /= len;
            q[3] /= len;
        }
        else
        {
            q[0] = q[1] = q[2] = 0.f;
            q[3] = 1.f;
        }
    }


    struct Bin
    {
        std::vector<uint8_t> bytes;

        size_t align(size_t a = 4)
        {
            while (bytes.size() % a)
                bytes.push_back(0);
            return bytes.size();
        }

        template <typename T>
        size_t append(const T* data, size_t count)
        {
            const size_t off = align(4);
            const size_t n = count * sizeof(T);
            const auto* p = reinterpret_cast<const uint8_t*>(data);
            bytes.insert(bytes.end(), p, p + n);
            return off;
        }

        size_t appendBytes(const std::vector<uint8_t>& v)
        {
            const size_t off = align(4);
            bytes.insert(bytes.end(), v.begin(), v.end());
            return off;
        }
    };

    struct Acc
    {
        int index = -1;
        size_t offset = 0;
        size_t count = 0;
        size_t byteLength = 0;
        int componentType = 0;
        std::string type;
        bool normalized = false;
        float min3[3]{}, max3[3]{};
        bool hasMinMax = false;
    };

    struct Builder
    {
        Bin bin;
        std::vector<Acc> accessors;
        std::vector<std::string> imagesJson;
        std::vector<int> textureIndexForMaterial;
        std::vector<std::string> materialJson;
        std::vector<std::string> meshPrimJson;
        std::vector<std::string> nodeJson;
        std::vector<int> jointNodeIndices;
        int skinIndex = -1;
        int meshNodeIndex = -1;
        std::vector<std::string> animJson;

        Acc& addAccessor(size_t offset, size_t count, size_t strideBytes, int componentType,
            const std::string& type, bool normalized = false)
        {
            Acc a;
            a.index = int(accessors.size());
            a.offset = offset;
            a.count = count;
            a.byteLength = count * strideBytes;
            a.componentType = componentType;
            a.type = type;
            a.normalized = normalized;
            accessors.push_back(a);
            return accessors.back();
        }
    };

    void WriteGlb(const std::string& path, const std::string& json, const std::vector<uint8_t>& bin)
    {
        std::string jsonPad = json;
        while (jsonPad.size() % 4)
            jsonPad.push_back(' ');
        std::vector<uint8_t> binPad = bin;
        while (binPad.size() % 4)
            binPad.push_back(0);

        const uint32_t jsonLen = uint32_t(jsonPad.size());
        const uint32_t binLen = uint32_t(binPad.size());
        const uint32_t total = 12 + 8 + jsonLen + (binLen ? 8 + binLen : 0);

        std::vector<uint8_t> out;
        out.reserve(total);
        auto push32 = [&](uint32_t v) {
            out.push_back(uint8_t(v));
            out.push_back(uint8_t(v >> 8));
            out.push_back(uint8_t(v >> 16));
            out.push_back(uint8_t(v >> 24));
            };
        out.push_back('g');
        out.push_back('l');
        out.push_back('T');
        out.push_back('F');
        push32(2);
        push32(total);
        push32(jsonLen);
        push32(0x4E4F534A);
        out.insert(out.end(), jsonPad.begin(), jsonPad.end());
        if (binLen)
        {
            push32(binLen);
            push32(0x004E4942);
            out.insert(out.end(), binPad.begin(), binPad.end());
        }
        WriteFile(std::filesystem::u8path(path), out);
    }

    bool TryReadInfluences(const RZGeometrySkin* skin, size_t meshIndex, size_t vertexCount,
        size_t boneCount, std::vector<uint16_t>& joints, std::vector<float>& weights)
    {
        auto rigid = [&]() {
            joints.assign(vertexCount * 4, 0);
            weights.assign(vertexCount * 4, 0.f);
            for (size_t v = 0; v < vertexCount; ++v)
                weights[v * 4] = 1.f;
            };
        rigid();

        if (!skin || meshIndex >= skin->meshes.size())
            return false;
        const auto& inf = skin->meshes[meshIndex];
        if (inf.size() < vertexCount)
            return false;

        joints.assign(vertexCount * 4, 0);
        weights.assign(vertexCount * 4, 0.f);
        for (size_t v = 0; v < vertexCount; ++v)
        {
            float sum = 0.f;
            for (size_t i = 0; i < 4; ++i)
            {
                const float w = inf[v].weights[i];
                const uint32_t bone = inf[v].bones[i];
                if (!(w > 0.f) || bone >= boneCount)
                    continue;
                joints[v * 4 + i] = uint16_t(bone);
                weights[v * 4 + i] = w;
                sum += w;
            }
            if (sum > 1e-6f)
            {
                for (size_t i = 0; i < 4; ++i)
                    weights[v * 4 + i] /= sum;
            }
            else
                weights[v * 4] = 1.f;
        }
        return true;
    }

}

void ExportGeometryGlb(const std::string& path,
    const RZGeometry& geometry,
    const std::vector<GlbTextureImage>& textures,
    const RZHierarchy* skeleton,
    const RZGeometrySkin* skin,
    const RZHAnim* clip,
    float sampleHz)
{
    if (geometry.meshes.empty())
        throw std::runtime_error("No meshes to export.");

    Builder b;

    std::unordered_map<size_t, int> matToTex;
    for (const auto& img : textures)
    {
        if (img.pngBytes.empty())
            continue;
        const size_t off = b.bin.appendBytes(img.pngBytes);
        const int imageIndex = int(b.imagesJson.size());
        std::ostringstream ij;
        ij << "{\"name\":" << JStr(img.name)
            << ",\"mimeType\":\"image/png\",\"bufferView\":" << imageIndex << '}';
        b.imagesJson.push_back(ij.str());
        (void)off;
        matToTex[img.materialIndex] = imageIndex;
    }
    struct ImageView
    {
        size_t offset;
        size_t length;
        std::string name;
        size_t materialIndex;
    };
    b.bin.bytes.clear();
    b.imagesJson.clear();
    std::vector<ImageView> imageViews;
    for (const auto& img : textures)
    {
        if (img.pngBytes.empty())
            continue;
        const size_t off = b.bin.appendBytes(img.pngBytes);
        imageViews.push_back({off, img.pngBytes.size(), img.name, img.materialIndex});
        matToTex[img.materialIndex] = int(imageViews.size() - 1);
    }

    const size_t materialCount = std::max(geometry.materials.size(), size_t(1));
    for (size_t mi = 0; mi < materialCount; ++mi)
    {
        const bool hasMat = mi < geometry.materials.size();
        const std::string matName = hasMat ? geometry.materials[mi].name : ("mat_" + std::to_string(mi));
        std::ostringstream mj;
        mj << "{\"name\":" << JStr(matName) << ",\"doubleSided\":true"
            << ",\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":0.8";
        if (hasMat)
        {
            const float* c = geometry.materials[mi].color;
            mj << ",\"baseColorFactor\":[" << JFloat(c[0]) << ',' << JFloat(c[1]) << ','
                << JFloat(c[2]) << ",1]";
        }
        auto it = matToTex.find(mi);
        if (it != matToTex.end())
            mj << ",\"baseColorTexture\":{\"index\":" << it->second << '}';
        mj << '}';
        if (hasMat)
        {
            const std::string& n = geometry.materials[mi].name;
            const auto lower = [](std::string s) {
                for (char& c : s)
                    c = char(std::tolower(unsigned(c)));
                return s;
                };
            const std::string ln = lower(n);
            if (ln.find("_da") != std::string::npos || ln.find("alphatest") != std::string::npos)
                mj << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
        }
        mj << '}';
        b.materialJson.push_back(mj.str());
    }

    bool anyWeights = false;
    for (size_t mi = 0; mi < geometry.meshes.size(); ++mi)
    {
        const RZGeometry::Mesh& mesh = geometry.meshes[mi];
        const size_t vc = mesh.VertexCount();
        if (vc == 0 || mesh.indices.size() < 3)
            continue;

        float pmin[3] = {mesh.positions[0], mesh.positions[1], mesh.positions[2]};
        float pmax[3] = {pmin[0], pmin[1], pmin[2]};
        for (size_t v = 0; v < vc; ++v)
        {
            for (int k = 0; k < 3; ++k)
            {
                const float x = mesh.positions[v * 3 + k];
                pmin[k] = std::min(pmin[k], x);
                pmax[k] = std::max(pmax[k], x);
            }
        }
        const size_t posOff = b.bin.append(mesh.positions.data(), vc * 3);
        const int posAccIdx = b.addAccessor(posOff, vc, 12, 5126, "VEC3").index;
        {
            Acc& pa = b.accessors[posAccIdx];
            pa.hasMinMax = true;
            std::copy_n(pmin, 3, pa.min3);
            std::copy_n(pmax, 3, pa.max3);
        }

        int normAccIndex = -1;
        if (mesh.normals.size() >= vc * 3)
        {
            const size_t off = b.bin.append(mesh.normals.data(), vc * 3);
            normAccIndex = b.addAccessor(off, vc, 12, 5126, "VEC3").index;
        }

        int uvAccIndex = -1;
        if (mesh.hasUV && mesh.texcoords.size() >= vc * 2)
        {
            const float div = mesh.integerUV ? 1024.f : 1.f;
            std::vector<float> uvs(vc * 2);
            for (size_t v = 0; v < vc; ++v)
            {
                uvs[v * 2 + 0] = mesh.texcoords[v * 2 + 0] / div;
                uvs[v * 2 + 1] = mesh.texcoords[v * 2 + 1] / div;
            }
            const size_t off = b.bin.append(uvs.data(), uvs.size());
            uvAccIndex = b.addAccessor(off, vc, 8, 5126, "VEC2").index;
        }

        int jointsAcc = -1, weightsAcc = -1;
        if (skeleton)
        {
            std::vector<uint16_t> joints;
            std::vector<float> weights;
            if (TryReadInfluences(skin, mi, vc, skeleton->bones.size(), joints, weights))
                anyWeights = true;
            const size_t jOff = b.bin.append(joints.data(), joints.size());
            jointsAcc = b.addAccessor(jOff, vc, 8, 5123, "VEC4").index;
            const size_t wOff = b.bin.append(weights.data(), weights.size());
            weightsAcc = b.addAccessor(wOff, vc, 16, 5126, "VEC4").index;
        }

        const size_t iOff = b.bin.append(mesh.indices.data(), mesh.indices.size());
        const int idxAcc = b.addAccessor(iOff, mesh.indices.size(), 4, 5125, "SCALAR").index;

        const int matIdx = mesh.material < b.materialJson.size() ? int(mesh.material) : 0;
        std::ostringstream pj;
        pj << "{\"attributes\":{\"POSITION\":" << posAccIdx;
        if (normAccIndex >= 0)
            pj << ",\"NORMAL\":" << normAccIndex;
        if (uvAccIndex >= 0)
            pj << ",\"TEXCOORD_0\":" << uvAccIndex;
        if (jointsAcc >= 0)
            pj << ",\"JOINTS_0\":" << jointsAcc;
        if (weightsAcc >= 0)
            pj << ",\"WEIGHTS_0\":" << weightsAcc;
        pj << "},\"indices\":" << idxAcc << ",\"material\":" << matIdx << '}';
        b.meshPrimJson.push_back(pj.str());
    }
    if (b.meshPrimJson.empty())
        throw std::runtime_error("No exportable primitives.");

    std::vector<int> sceneRoots;
    if (skeleton && !skeleton->bones.empty())
    {
        const size_t nb = skeleton->bones.size();
        std::vector<CMTransforms> bindLocal;
        bindLocal.reserve(nb);
        for (const auto& bone : skeleton->bones)
            bindLocal.push_back(bone.bind);
        const auto worldBind = skeleton->WorldPose(bindLocal);

        b.jointNodeIndices.resize(nb);
        for (size_t i = 0; i < nb; ++i)
        {
            b.jointNodeIndices[i] = int(b.nodeJson.size());
            std::ostringstream nj;
            nj << "{\"name\":" << JStr(skeleton->bones[i].name.empty()
                ? ("bone_" + std::to_string(i))
                : skeleton->bones[i].name)
                << ",\"matrix\":" << JMatrix(skeleton->bones[i].bind.Matrix());
            nj << '}';
            b.nodeJson.push_back(nj.str());
        }
        std::vector<std::vector<int>> children(nb);
        for (size_t i = 0; i < nb; ++i)
        {
            const uint32_t p = skeleton->bones[i].parent;
            if (p < nb)
                children[p].push_back(int(i));
            else
                sceneRoots.push_back(int(i));
        }
        for (size_t i = 0; i < nb; ++i)
        {
            if (children[i].empty())
                continue;
            std::string& nj = b.nodeJson[i];
            nj.pop_back();
            std::ostringstream ch;
            ch << ",\"children\":[";
            for (size_t c = 0; c < children[i].size(); ++c)
            {
                if (c)
                    ch << ',';
                ch << children[i][c];
            }
            ch << "]}";
            nj += ch.str();
        }

        std::vector<float> ibm(nb * 16);
        for (size_t i = 0; i < nb; ++i)
        {
            const BUMatrix& inv = skeleton->bones[i].inverseBind;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    ibm[i * 16 + r * 4 + c] = inv.m[r][c];
        }
        const size_t ibmOff = b.bin.append(ibm.data(), ibm.size());
        const int ibmAcc = b.addAccessor(ibmOff, nb, 64, 5126, "MAT4").index;

        b.meshNodeIndex = int(b.nodeJson.size());
        {
            std::ostringstream nj;
            nj << "{\"name\":" << JStr(geometry.name.empty() ? "mesh" : geometry.name)
                << ",\"mesh\":0,\"skin\":0}";
            b.nodeJson.push_back(nj.str());
        }
        sceneRoots.push_back(b.meshNodeIndex);
        b.skinIndex = 0;

        (void)ibmAcc;
        static_cast<void>(ibmAcc);
        b.animJson.push_back(std::to_string(ibmAcc));
    }
    else
    {
        b.meshNodeIndex = int(b.nodeJson.size());
        std::ostringstream nj;
        nj << "{\"name\":" << JStr(geometry.name.empty() ? "mesh" : geometry.name) << ",\"mesh\":0}";
        b.nodeJson.push_back(nj.str());
        sceneRoots.push_back(b.meshNodeIndex);
    }

    int animIbmMarker = -1;
    if (!b.animJson.empty())
    {
        animIbmMarker = std::stoi(b.animJson[0]);
        b.animJson.clear();
    }

    if (clip && skeleton && !skeleton->bones.empty() && sampleHz > 0.f && clip->info.duration > 0.f)
    {
        const size_t nb = skeleton->bones.size();
        const float duration = clip->info.duration;
        const int frameCount = std::max(2, int(std::ceil(duration * sampleHz)) + 1);
        std::vector<float> times;
        times.resize(static_cast<size_t>(frameCount));
        for (int f = 0; f < frameCount; ++f)
            times[size_t(f)] = std::min(duration, float(f) / sampleHz);

        std::vector<float> trans(size_t(frameCount) * nb * 3);
        std::vector<float> rots(size_t(frameCount) * nb * 4);
        std::vector<float> scales(size_t(frameCount) * nb * 3);
        for (int f = 0; f < frameCount; ++f)
        {
            const auto pose = clip->Sample(*skeleton, times[size_t(f)]);
            for (size_t i = 0; i < nb; ++i)
            {
                float t[3], q[4], s[3];
                const BUMatrix mat = (i < pose.size()) ? pose[i].Matrix() : skeleton->bones[i].bind.Matrix();
                Decompose(mat, t, q, s);
                const size_t base = size_t(f) * nb + i;
                trans[base * 3 + 0] = t[0];
                trans[base * 3 + 1] = t[1];
                trans[base * 3 + 2] = t[2];
                rots[base * 4 + 0] = q[0];
                rots[base * 4 + 1] = q[1];
                rots[base * 4 + 2] = q[2];
                rots[base * 4 + 3] = q[3];
                scales[base * 3 + 0] = s[0];
                scales[base * 3 + 1] = s[1];
                scales[base * 3 + 2] = s[2];
            }
        }

        const size_t timeOff = b.bin.append(times.data(), times.size());
        Acc& timeAcc = b.addAccessor(timeOff, times.size(), 4, 5126, "SCALAR");
        timeAcc.hasMinMax = true;
        timeAcc.min3[0] = times.front();
        timeAcc.max3[0] = times.back();

        std::ostringstream channels, samplers;
        channels << '[';
        samplers << '[';
        int samplerIdx = 0;
        for (size_t i = 0; i < nb; ++i)
        {
            std::vector<float> bt(size_t(frameCount) * 3);
            std::vector<float> br(size_t(frameCount) * 4);
            std::vector<float> bs(size_t(frameCount) * 3);
            for (int f = 0; f < frameCount; ++f)
            {
                const size_t base = size_t(f) * nb + i;
                bt[size_t(f) * 3 + 0] = trans[base * 3 + 0];
                bt[size_t(f) * 3 + 1] = trans[base * 3 + 1];
                bt[size_t(f) * 3 + 2] = trans[base * 3 + 2];
                br[size_t(f) * 4 + 0] = rots[base * 4 + 0];
                br[size_t(f) * 4 + 1] = rots[base * 4 + 1];
                br[size_t(f) * 4 + 2] = rots[base * 4 + 2];
                br[size_t(f) * 4 + 3] = rots[base * 4 + 3];
                bs[size_t(f) * 3 + 0] = scales[base * 3 + 0];
                bs[size_t(f) * 3 + 1] = scales[base * 3 + 1];
                bs[size_t(f) * 3 + 2] = scales[base * 3 + 2];
            }
            const size_t tOff = b.bin.append(bt.data(), bt.size());
            const int tAcc = b.addAccessor(tOff, size_t(frameCount), 12, 5126, "VEC3").index;
            const size_t rOff = b.bin.append(br.data(), br.size());
            const int rAcc = b.addAccessor(rOff, size_t(frameCount), 16, 5126, "VEC4").index;
            const size_t sOff = b.bin.append(bs.data(), bs.size());
            const int sAcc = b.addAccessor(sOff, size_t(frameCount), 12, 5126, "VEC3").index;

            auto addChan = [&](const char* path, int input, int output) {
                if (samplerIdx)
                {
                    samplers << ',';
                    channels << ',';
                }
                samplers << "{\"input\":" << input << ",\"output\":" << output
                    << ",\"interpolation\":\"LINEAR\"}";
                channels << "{\"sampler\":" << samplerIdx << ",\"target\":{\"node\":"
                    << b.jointNodeIndices[i] << ",\"path\":\"" << path << "\"}}";
                ++samplerIdx;
                };
            addChan("translation", timeAcc.index, tAcc);
            addChan("rotation", timeAcc.index, rAcc);
            addChan("scale", timeAcc.index, sAcc);
        }
        channels << ']';
        samplers << ']';

        std::ostringstream aj;
        aj << "{\"name\":" << JStr(clip->name) << ",\"samplers\":" << samplers.str()
            << ",\"channels\":" << channels.str() << '}';
        b.animJson.push_back(aj.str());
    }

    std::ostringstream views;
    views << '[';
    bool firstView = true;
    auto emitView = [&](size_t offset, size_t length, int target ) {
        if (!firstView)
            views << ',';
        firstView = false;
        views << "{\"buffer\":0,\"byteOffset\":" << offset << ",\"byteLength\":" << length;
        if (target)
            views << ",\"target\":" << target;
        views << '}';
        };
    for (const auto& iv : imageViews)
        emitView(iv.offset, iv.length, 0);
    const int imageViewBase = 0;
    const int accessorViewBase = int(imageViews.size());
    for (const Acc& a : b.accessors)
    {
        int target = 0;
        if (a.type == "SCALAR" && a.componentType == 5125)
            target = 34963;
        else if (a.componentType == 5126 || a.componentType == 5123)
            target = 34962;
        emitView(a.offset, a.byteLength, target);
    }
    views << ']';

    std::ostringstream accJson;
    accJson << '[';
    for (size_t i = 0; i < b.accessors.size(); ++i)
    {
        const Acc& a = b.accessors[i];
        if (i)
            accJson << ',';
        accJson << "{\"bufferView\":" << (accessorViewBase + int(i))
            << ",\"componentType\":" << a.componentType
            << ",\"count\":" << a.count
            << ",\"type\":\"" << a.type << '"';
        if (a.normalized)
            accJson << ",\"normalized\":true";
        if (a.hasMinMax)
        {
            if (a.type == "SCALAR")
                accJson << ",\"min\":[" << JFloat(a.min3[0]) << "],\"max\":[" << JFloat(a.max3[0]) << ']';
            else if (a.type == "VEC3")
                accJson << ",\"min\":[" << JFloat(a.min3[0]) << ',' << JFloat(a.min3[1]) << ','
                << JFloat(a.min3[2]) << "],\"max\":[" << JFloat(a.max3[0]) << ','
                << JFloat(a.max3[1]) << ',' << JFloat(a.max3[2]) << ']';
        }
        accJson << '}';
    }
    accJson << ']';

    std::ostringstream images;
    images << '[';
    for (size_t i = 0; i < imageViews.size(); ++i)
    {
        if (i)
            images << ',';
        images << "{\"name\":" << JStr(imageViews[i].name)
            << ",\"mimeType\":\"image/png\",\"bufferView\":" << (imageViewBase + int(i)) << '}';
    }
    images << ']';

    std::ostringstream texs;
    texs << '[';
    for (size_t i = 0; i < imageViews.size(); ++i)
    {
        if (i)
            texs << ',';
        texs << "{\"source\":" << i << '}';
    }
    texs << ']';

    std::ostringstream mats;
    mats << '[';
    for (size_t i = 0; i < b.materialJson.size(); ++i)
    {
        if (i)
            mats << ',';
        mats << b.materialJson[i];
    }
    mats << ']';

    std::ostringstream prims;
    prims << '[';
    for (size_t i = 0; i < b.meshPrimJson.size(); ++i)
    {
        if (i)
            prims << ',';
        prims << b.meshPrimJson[i];
    }
    prims << ']';

    std::ostringstream nodes;
    nodes << '[';
    for (size_t i = 0; i < b.nodeJson.size(); ++i)
    {
        if (i)
            nodes << ',';
        nodes << b.nodeJson[i];
    }
    nodes << ']';

    std::ostringstream sceneNodes;
    sceneNodes << '[';
    for (size_t i = 0; i < sceneRoots.size(); ++i)
    {
        if (i)
            sceneNodes << ',';
        sceneNodes << sceneRoots[i];
    }
    sceneNodes << ']';

    std::ostringstream root;
    root << '{'
        << "\"asset\":{\"version\":\"2.0\",\"generator\":\"pkzgui gltf_export\"},"
        << "\"scene\":0,"
        << "\"scenes\":[{\"nodes\":" << sceneNodes.str() << "}],"
        << "\"nodes\":" << nodes.str() << ','
        << "\"meshes\":[{\"name\":" << JStr(geometry.name) << ",\"primitives\":" << prims.str() << "}],"
        << "\"materials\":" << mats.str() << ','
        << "\"accessors\":" << accJson.str() << ','
        << "\"bufferViews\":" << views.str() << ','
        << "\"buffers\":[{\"byteLength\":" << b.bin.bytes.size() << "}]";

    if (!imageViews.empty())
    {
        root << ",\"images\":" << images.str();
        root << ",\"textures\":" << texs.str();
    }
    if (b.skinIndex >= 0 && skeleton)
    {
        root << ",\"skins\":[{\"joints\":[";
        for (size_t i = 0; i < b.jointNodeIndices.size(); ++i)
        {
            if (i)
                root << ',';
            root << b.jointNodeIndices[i];
        }
        root << "],\"inverseBindMatrices\":" << animIbmMarker
            << ",\"skeleton\":" << (sceneRoots.empty() ? 0 : sceneRoots.front()) << "}]";
    }
    if (!b.animJson.empty())
    {
        root << ",\"animations\":[";
        for (size_t i = 0; i < b.animJson.size(); ++i)
        {
            if (i)
                root << ',';
            root << b.animJson[i];
        }
        root << ']';
    }
    root << '}';

    {
        const std::string js = root.str();
        std::ofstream dbg("gltf_debug.json", std::ios::binary);
        dbg.write(js.data(), std::streamsize(js.size()));

        const size_t p = 102768;
        if (p < js.size())
        {
            std::ofstream ctx("gltf_debug_ctx.txt", std::ios::binary);
            const size_t s = p > 120 ? p - 120 : 0;
            ctx << js.substr(s, 240);
        }
    }

    WriteGlb(path, root.str(), b.bin.bytes);

    if (skeleton && !anyWeights && skin)
    {
        (void)skin;
    }
}
