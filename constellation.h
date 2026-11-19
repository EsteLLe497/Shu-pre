// =========================================================
// êØç¿ÇÃå`Ç∆ê⁄ë±èÛë‘ [constellation.h]
// =========================================================
#pragma once
#include <DirectXMath.h>
#include <array>

namespace constellation
{
    constexpr unsigned int maxStars = 16;
    constexpr unsigned int maxLinks = 16;

    struct Link
    {
        unsigned int from;
        unsigned int to;
    };

    struct Shape
    {
        const DirectX::XMFLOAT2* stars;
        unsigned int starCount;
        const Link* links;
        unsigned int linkCount;
        DirectX::XMFLOAT3 color;
    };

    struct alignas(16) DrawData
    {
        DirectX::XMFLOAT4 color{};
        DirectX::XMFLOAT4 state{};
        DirectX::XMFLOAT4 bounds{};
        DirectX::XMFLOAT4 previewLine{};
        DirectX::XMFLOAT4 previewState{};
        std::array<DirectX::XMFLOAT4, maxStars> stars{};
        std::array<DirectX::XMFLOAT4, maxLinks> links{};
    };

    const Shape& scorpio();
    void open(const Shape& shape);
    void reset();
    void project(DirectX::FXMMATRIX viewProjection, DirectX::XMFLOAT3 center, float radius, float yaw,
        float visibility, bool canPick, DrawData& data, DirectX::XMFLOAT3* aimedStar = nullptr);
}
