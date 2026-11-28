// =========================================================
// ファイルの情報[GraphicsLabGeometry.h]
//
// 制作者:ozawa souma        日付:2026/11/28
// =========================================================

#pragma once

#include "renderer.h"
#include <array>
#include <vector>

class MeshField;

namespace LabLandscape
{
    enum class Surface : unsigned int
    {
        ground, solid, steel, wood, concrete, roof, bark, leaves, mountains, ballast, signalLens, stationSign,
        keyakiBark, sugiBark, keyakiFoliage, konaraFoliage, sugiFoliage, count
    };

    constexpr bool isTreeFoliage(Surface kind)
    {
        return kind == Surface::keyakiFoliage || kind == Surface::konaraFoliage || kind == Surface::sugiFoliage;
    }

    constexpr bool isTreeBark(Surface kind)
    {
        return kind == Surface::keyakiBark || kind == Surface::bark || kind == Surface::sugiBark;
    }

    constexpr bool isVegetation(Surface kind)
    {
        return kind == Surface::leaves || isTreeBark(kind) || isTreeFoliage(kind);
    }

    using Batches = std::array<std::vector<VERTEX_3D>, static_cast<unsigned int>(Surface::count)>;
    Batches build(const MeshField& terrain);
}
