#pragma once

#include "assimp/matrix4x4.h"

namespace BonePoseRotation
{
    // Assimp uses column vectors. Conjugate a model-space correction into the
    // parent frame, keeping the joint pivot fixed (including nonidentity FBX axes).
    inline aiMatrix4x4 apply(const aiMatrix4x4& parent, const aiMatrix4x4& local, const aiMatrix4x4& rotation)
    {
        const auto global = parent * local;
        auto rotated = rotation * global;
        rotated.a4 = global.a4;
        rotated.b4 = global.b4;
        rotated.c4 = global.c4;
        auto inverse = parent;
        inverse.Inverse();
        return inverse * rotated;
    }
}
