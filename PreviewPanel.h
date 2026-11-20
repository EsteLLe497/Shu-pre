// =========================================================
// ファイルの情報[PreviewPanel.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "ActionSequence.h"

#include "PreviewTimeline.h"
struct ToonSettings;
class PreviewCamera;

struct PreviewControls
{
    std::string clipName = "Walk";
    std::vector<std::string> motionNames;
    const char* motionStatus = nullptr;
    int morphIndex = 0;
    int baseMorphIndex = -1;
    std::vector<MorphTarget> extraMorphs;
    float effectX = 0.75f;
    float effectHeight = 0.20f;
    WeaponTrack weapon;
    int category = 0;
    std::array<PreviewTimeline, 3> categoryTimelines;
    std::array<std::string, 3> categoryNames{ "PlayerAction", "Enemy", "Attack01" };
    std::array<float, 3> categoryEffectX{0.75f, 0.75f, 0.75f};
    std::array<float, 3> categoryEffectHeight{0.2f, 0.2f, 0.2f};
    int selectedKey = 0;
    int enemyAction = 0;
    bool showPlayerContext = true;
    bool showWorldAxes = true;
    bool moveWholePath = true;
    char characterPath[512]{};
    char weaponPath[241]{};
    std::string weaponStatus;
    std::string assetStatus;
    const char* shaderStatus = nullptr;
    char actionName[70] = "PlayerAction";
    std::vector<std::string> savedActionNames;
    std::string actionStatus;
};

namespace PreviewPanel
{
    bool draw(PreviewTimeline& timeline, PreviewControls& controls, PreviewCamera& camera,
        ToonSettings& shader, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion,
        const std::function<bool(const std::string&)>& loadWeapon,
        const std::function<double()>& clipDuration);
}
