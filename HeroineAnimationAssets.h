// =========================================================
// ファイルの情報[HeroineAnimationAssets.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <string>
#include <vector>

class AnimationModel;

// ヒロインとプレビューで同じモデルとモーションを使う。
namespace HeroineAnimationAssets
{
    constexpr char modelsDirectory[] = "asset/model/Player_Movement";
    constexpr char model[] = "asset/model/Player_Movement/Player.fbx";
    constexpr char previewModel[] = "asset/model/Player_Movement/Player_Preview.fbx";
    constexpr float skeletonScale = 0.01f;
    constexpr char motionsDirectory[] = "asset/model/Player_Movement/Motions";

    bool isMotionName(const std::string& name);
    bool isBuiltInMotion(const std::string& name);
    std::string canonicalMotionName(const std::string& name);
    std::vector<std::string> listMotions(const std::string& characterPath = {});
    std::vector<std::string> listCharacters();
    bool loadMotion(AnimationModel& model, const std::string& name, const std::string& characterPath = {});

    enum class LoadResult { loaded, modelFailed, motionMismatch, missingTextures };

    LoadResult loadCharacter(AnimationModel& model, const char* path, bool requireMotions = true);

    struct Clip
    {
        const char* name;
        const char* path;
    };

    constexpr Clip clips[] = {
        { "Idle", "asset/model/Player_Movement/Motions/Idle.fbx" },
        { "Walk", "asset/model/Player_Movement/Motions/FemaleWalk.fbx" },
        { "Run", "asset/model/Player_Movement/Motions/FemaleRun.fbx" }
    };
}
