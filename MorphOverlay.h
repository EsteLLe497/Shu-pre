#pragma once

#include "assimp/vector3.h"
#include "assimp/vector3.inl"

namespace MorphOverlay
{
    inline bool affects(const aiVector3D& base, const aiVector3D& overlay)
    {
        return (overlay - base).SquareLength() > 0.000000000001f;
    }

    // Blend to an absolute shape only on vertices it affects. An already closed
    // eyelid must not receive the close delta twice; mouth expressions stay intact.
    inline aiVector3D blend(const aiVector3D& base, const aiVector3D& overlay,
        const aiVector3D& expression, float weight)
    {
        return affects(base, overlay) ? expression + (overlay - expression) * weight : expression;
    }
}
