// =========================================================
// ファイルの情報[PlayerAnimationAssets.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "animationModel.h"
#include "PlayerAnimationAssets.h"

namespace
{
    const PlayerAnimationAssets::Clip* findBuiltIn(const std::string& name)
    {
        for (const auto& clip : PlayerAnimationAssets::clips)
        {
            if (_stricmp(clip.name, name.c_str()) == 0)
                return &clip;
        }

        return nullptr;
    }
}

// =========================================================
// モーション名をファイル名として安全に扱える形式に限定する
// =========================================================
bool PlayerAnimationAssets::isMotionName(const std::string& name)
{
    if (name.empty() || name.size() > 64 || name.front() == ' ' || name.back() == ' ')
        return false;

    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
    });
}

bool PlayerAnimationAssets::isBuiltInMotion(const std::string& name)
{
    return findBuiltIn(name) != nullptr;
}

std::string PlayerAnimationAssets::canonicalMotionName(const std::string& name)
{
    const auto* builtIn = findBuiltIn(name);
    return builtIn ? builtIn->name : name;
}

// =========================================================
// 一覧にはファイル名だけを取得し、FBX は選ばれたときに読む
// =========================================================
std::vector<std::string> PlayerAnimationAssets::listMotions()
{
    std::vector<std::string> names;
    for (const auto& clip : clips)
        names.emplace_back(clip.name);

    const auto customStart = names.size();
    WIN32_FIND_DATAA data{};
    const std::string pattern = std::string(motionsDirectory) + "/*.fbx";
    const HANDLE search = FindFirstFileA(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return names;

    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        const std::string file = data.cFileName;
        if (file.size() <= 4 || _stricmp(file.c_str() + file.size() - 4, ".fbx") != 0)
            continue;

        const auto name = file.substr(0, file.size() - 4);
        if (isMotionName(name) && !isBuiltInMotion(name))
            names.push_back(name);
    } while (FindNextFileA(search, &data));

    FindClose(search);
    std::sort(names.begin() + customStart, names.end());
    return names;
}

bool PlayerAnimationAssets::loadMotion(AnimationModel& model, const std::string& name)
{
    if (!isMotionName(name))
        return false;

    const auto* builtIn = findBuiltIn(name);
    const char* key = builtIn ? builtIn->name : name.c_str();
    if (model.hasAnimation(key))
        return true;
    const std::string path = std::string(motionsDirectory) + "/" + key + ".fbx";
    const DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)
        && model.LoadAnimation(path.c_str(), key))
        return true;

    // 同名の差し替えが使えなければ、新しい標準モーションを使う。
    return builtIn && path != builtIn->path && model.LoadAnimation(builtIn->path, key);
}

// =========================================================
// 本編とプレビューで同じ FBX とモーションの検証を行う
// =========================================================
PlayerAnimationAssets::LoadResult PlayerAnimationAssets::loadCharacter(AnimationModel& model, const char* path)
{
    if (!model.tryLoad(path))
        return LoadResult::modelFailed;

    if (model.missingTextures() > 0)
    {
        model.Uninit();
        return LoadResult::missingTextures;
    }

    model.setSkeletonUnitScale(skeletonScale);
    for (const auto& clip : clips)
    {
        if (!loadMotion(model, clip.name))
        {
            model.Uninit();
            return LoadResult::motionMismatch;
        }
    }

    return LoadResult::loaded;
}
