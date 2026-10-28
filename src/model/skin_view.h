#pragma once
#include "Resource/RZGeometrySkin.h"
#include <cstddef>
#include <cstdint>
#include <vector>

enum class SkinShading
{
    Material,
    BoneWeight,
    BoneColors,
};

struct SkinSummary
{
    size_t vertices = 0;
    size_t weighted = 0;
    uint32_t maxInfluences = 0;
    std::vector<size_t> boneVertices;
};

void WeightRampColor(float weight, float rgb[3]);
void BoneColor(uint32_t bone, float rgb[3]);
std::vector<std::vector<float>> SkinVertexColors(const RZGeometrySkin& skin, SkinShading shading, int bone);
SkinSummary SummarizeSkin(const RZGeometrySkin& skin);
