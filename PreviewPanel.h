// =========================================================
// ファイルの情報[PreviewPanel.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "MorphTarget.h"

struct PreviewTimeline;
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
    std::string assetStatus;
    const char* shaderStatus = nullptr;
    char actionName[70] = "Attack01";
    std::vector<std::string> savedActionNames;
    std::string actionStatus;
};

namespace PreviewPanel
{
    bool draw(PreviewTimeline& timeline, PreviewControls& controls, PreviewCamera& camera,
        ToonSettings& shader, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion);
}
