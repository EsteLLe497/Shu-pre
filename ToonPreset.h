// =========================================================
// ファイルの情報[ToonPreset.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

struct CameraOrbit;
struct ToonSettings;

namespace ToonPreset
{
    bool save(const ToonSettings& toon, const CameraOrbit& orbit);
    bool load(ToonSettings& toon, CameraOrbit& orbit);
    bool saveAppearance(const ToonSettings& toon);
    bool loadAppearance(ToonSettings& toon);
}
