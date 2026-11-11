// =========================================================
// ファイルの情報[MorphTarget.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <cstddef>
#include <string>

// 表情に重ねる追加シェイプキーと、ピーク時の強さ。
struct MorphTarget
{
    static constexpr std::size_t maxExtraTargets = 16;

    std::string name;
    float weight = 1.0f;
    float start = 0.65f;
    float peak = 0.72f;
    float end = 0.84f;
    bool smooth = true;

    bool operator==(const MorphTarget& other) const
    {
        return name == other.name && weight == other.weight && start == other.start
            && peak == other.peak && end == other.end && smooth == other.smooth;
    }
};

