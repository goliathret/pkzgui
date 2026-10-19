#include "animation/animation_placement.h"

BUMatrix AnimationPreviewPlacement(const RZHAnim& clip, const RZHierarchy& skeleton)
{
    size_t root = skeleton.bones.size();
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (skeleton.bones[i].parent == UINT32_MAX)
        {
            if (root != skeleton.bones.size())
                return BUMatrix{}; // Multiple independent roots have no single actor placement.
            root = i;
        }
    if (root == skeleton.bones.size())
        return BUMatrix{};
    const auto first = clip.Sample(skeleton, 0);
    return skeleton.bones[root].bind.Matrix() * first[root].InverseMatrix();
}
