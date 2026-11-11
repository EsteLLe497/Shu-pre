// =========================================================
// ファイルの情報[ToonPreset.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "Camera.h"
#include "ToonPreset.h"
#include "ToonShader.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace
{
    constexpr char settingsPath[] = "toonSettings.cfg";
    constexpr int formatVersion = 1;

    bool inRange(float value, float minValue, float maxValue)
    {
        return std::isfinite(value) && minValue <= value && value <= maxValue;
    }

    bool validColor(const float (&color)[3])
    {
        return inRange(color[0], 0.0f, 1.0f) && inRange(color[1], 0.0f, 1.0f)
            && inRange(color[2], 0.0f, 1.0f);
    }

    bool validSettings(const ToonSettings& toon, const CameraOrbit& orbit)
    {
        return toon.shadingMode >= 0 && toon.shadingMode <= 2
            && toon.lightBands >= 2 && toon.lightBands <= 8
            && inRange(toon.lightThreshold, 0.0f, 0.98f) && inRange(toon.edgeSoftness, 0.0f, 0.20f)
            && inRange(toon.shadowBrightness, 0.0f, 1.0f) && validColor(toon.shadowTint)
            && inRange(toon.exposure, 0.0f, 3.0f)
            && inRange(toon.outlineWidth, 0.0f, 0.08f) && validColor(toon.outlineColor)
            && inRange(toon.rimStrength, 0.0f, 1.0f) && inRange(toon.rimPower, 1.0f, 12.0f)
            && validColor(toon.rimColor) && inRange(toon.lightYaw, -XM_PI, XM_PI)
            && inRange(toon.lightPitch, -XM_PIDIV2, XM_PIDIV2)
            && inRange(toon.modelYaw, -XM_PI, XM_PI) && inRange(orbit.yaw, -XM_PI, XM_PI)
            && inRange(orbit.pitch, CameraOrbit::minPitch, CameraOrbit::maxPitch)
            && inRange(orbit.distance, CameraOrbit::minDistance, CameraOrbit::maxDistance)
            && inRange(orbit.lookHeight, CameraOrbit::minLookHeight, CameraOrbit::maxLookHeight);
    }
}

// =========================================================
// 書き込みが完了してから前回の保存ファイルを置き換える
// =========================================================
bool ToonPreset::save(const ToonSettings& toon, const CameraOrbit& orbit)
{
    if (!validSettings(toon, orbit))
        return false;

    // 同じフォルダーに一時ファイルを作り、別の起動と書き込み先が重ならないようにする。
    char temporaryPath[MAX_PATH];
    if (!GetTempFileNameA(".", "tn", 0, temporaryPath))
        return false;

    std::ofstream file(temporaryPath);

    file.imbue(std::locale::classic());
    file << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "toonSettings " << formatVersion << '\n'
         << toon.shadingMode << ' ' << toon.lightBands << ' ' << toon.lightThreshold << ' '
         << toon.edgeSoftness << ' ' << toon.shadowBrightness << ' ' << toon.exposure << ' '
         << toon.useBaseTextures << '\n'
         << toon.shadowTint[0] << ' ' << toon.shadowTint[1] << ' ' << toon.shadowTint[2] << '\n'
         << toon.outlineEnabled << ' ' << toon.outlineWidth << ' '
         << toon.outlineColor[0] << ' ' << toon.outlineColor[1] << ' ' << toon.outlineColor[2] << '\n'
         << toon.rimStrength << ' ' << toon.rimPower << ' '
         << toon.rimColor[0] << ' ' << toon.rimColor[1] << ' ' << toon.rimColor[2] << '\n'
         << toon.lightYaw << ' ' << toon.lightPitch << ' ' << toon.modelYaw << '\n'
         << orbit.yaw << ' ' << orbit.pitch << ' ' << orbit.distance << ' ' << orbit.lookHeight << '\n';
    file.close();

    const bool saved = file && MoveFileExA(temporaryPath, settingsPath,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!saved)
        DeleteFileA(temporaryPath);

    return saved;
}

// =========================================================
// すべての値を読み取り、範囲を確認してから現在値に反映
// =========================================================
bool ToonPreset::load(ToonSettings& toon, CameraOrbit& orbit)
{
    std::ifstream file(settingsPath);
    file.imbue(std::locale::classic());
    std::string format;
    int version;
    if (!(file >> format >> version) || format != "toonSettings" || version != formatVersion)
        return false;

    ToonSettings loadedToon;
    CameraOrbit loadedOrbit;
    if (!(file >> loadedToon.shadingMode >> loadedToon.lightBands >> loadedToon.lightThreshold
        >> loadedToon.edgeSoftness >> loadedToon.shadowBrightness >> loadedToon.exposure >> loadedToon.useBaseTextures
        >> loadedToon.shadowTint[0] >> loadedToon.shadowTint[1] >> loadedToon.shadowTint[2]
        >> loadedToon.outlineEnabled >> loadedToon.outlineWidth
        >> loadedToon.outlineColor[0] >> loadedToon.outlineColor[1] >> loadedToon.outlineColor[2]
        >> loadedToon.rimStrength >> loadedToon.rimPower
        >> loadedToon.rimColor[0] >> loadedToon.rimColor[1] >> loadedToon.rimColor[2]
        >> loadedToon.lightYaw >> loadedToon.lightPitch >> loadedToon.modelYaw
        >> loadedOrbit.yaw >> loadedOrbit.pitch >> loadedOrbit.distance >> loadedOrbit.lookHeight))
        return false;

    file >> std::ws;
    if (!file.eof() || !validSettings(loadedToon, loadedOrbit))
        return false;

    toon = loadedToon;
    orbit = loadedOrbit;
    return true;
}
