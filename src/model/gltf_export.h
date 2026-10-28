#pragma once
#include "Resource/RZGeometry.h"
#include "Resource/RZHierarchy.h"
#include "Resource/RZHAnim.h"
#include "Resource/RZGeometrySkin.h"
#include <string>
#include <utility>
#include <vector>

struct GlbTextureImage
{
    std::string name;
    std::vector<uint8_t> pngBytes;
    size_t materialIndex = 0;
};

void ExportGeometryGlb(const std::string& path,
    const RZGeometry& geometry,
    const std::vector<GlbTextureImage>& textures,
    const RZHierarchy* skeleton = nullptr,
    const RZGeometrySkin* skin = nullptr,
    const RZHAnim* clip = nullptr,
    float sampleHz = 30.0f);
