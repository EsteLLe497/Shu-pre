// =========================================================
// ファイルの情報[HeroineAnimationAssets.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "animationModel.h"
#include "HeroineAnimationAssets.h"

namespace
{
    std::string characterMotionsDirectory(const std::string& characterPath)
    {
        const auto extension = characterPath.find_last_of('.');
        return extension == std::string::npos ? std::string{} : characterPath.substr(0, extension) + "/Motions";
    }

    const HeroineAnimationAssets::Clip* findBuiltIn(const std::string& name)
    {
        for (const auto& clip : HeroineAnimationAssets::clips)
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
bool HeroineAnimationAssets::isMotionName(const std::string& name)
{
    if (name.empty() || name.size() > 64 || name.front() == ' ' || name.back() == ' ')
        return false;

    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
    });
}

bool HeroineAnimationAssets::isBuiltInMotion(const std::string& name)
{
    return findBuiltIn(name) != nullptr;
}

std::string HeroineAnimationAssets::canonicalMotionName(const std::string& name)
{
    const auto* builtIn = findBuiltIn(name);
    return builtIn ? builtIn->name : name;
}

// =========================================================
// 一覧にはファイル名だけを取得し、FBX は選ばれたときに読む
// =========================================================
std::vector<std::string> HeroineAnimationAssets::listMotions(const std::string& characterPath)
{
    std::vector<std::string> names;
    for (const auto& clip : clips)
        names.emplace_back(clip.name);

    const auto customStart = names.size();
    for (const auto& directory : { std::string(motionsDirectory), characterMotionsDirectory(characterPath) })
    {
        if (directory.empty()) continue;
        WIN32_FIND_DATAA data{};
        const std::string pattern = std::string(directory) + "/*.fbx";
        const HANDLE search = FindFirstFileA(pattern.c_str(), &data);
        if (search == INVALID_HANDLE_VALUE)
            continue;

        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            const std::string file = data.cFileName;
            if (file.size() <= 4 || _stricmp(file.c_str() + file.size() - 4, ".fbx") != 0)
                continue;

            const auto name = file.substr(0, file.size() - 4);
            if (isMotionName(name) && std::none_of(names.begin(), names.end(), [&](const auto& existing) {
                return _stricmp(existing.c_str(), name.c_str()) == 0; }))
                names.push_back(name);
        } while (FindNextFileA(search, &data));

        FindClose(search);
    }
    std::sort(names.begin() + customStart, names.end());
    return names;
}

// プレビューのモデル選択は実ファイルから列挙し、FBXは選択時だけ読む。
std::vector<std::string> HeroineAnimationAssets::listCharacters()
{
    std::vector<std::string> paths;
    WIN32_FIND_DATAA data{};
    const std::string pattern = std::string(modelsDirectory) + "/*.fbx";
    const HANDLE search = FindFirstFileA(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return paths;
    do
    {
        const std::string file = data.cFileName;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && file.size() > 4
            && _stricmp(file.c_str() + file.size() - 4, ".fbx") == 0)
        {
            const std::string path = std::string(modelsDirectory) + "/" + file;
            if (path.size() < 512) paths.push_back(path);
        }
    } while (FindNextFileA(search, &data));
    FindClose(search);
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool HeroineAnimationAssets::loadMotion(AnimationModel& model, const std::string& name, const std::string& characterPath)
{
    if (!isMotionName(name))
        return false;

    const auto* builtIn = findBuiltIn(name);
    const char* key = builtIn ? builtIn->name : name.c_str();
    if (model.hasAnimation(key))
        return true;
    // 同じモデル名のフォルダーにある専用モーションを優先する。
    const auto dedicatedDirectory = characterMotionsDirectory(characterPath);
    if (!dedicatedDirectory.empty())
    {
        const auto dedicated = dedicatedDirectory + "/" + key + ".fbx";
        const DWORD attributes = GetFileAttributesA(dedicated.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return model.LoadAnimation(dedicated.c_str(), key);
    }
    const std::string path = std::string(motionsDirectory) + "/" + key + ".fbx";
    const DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)
        && model.LoadAnimation(path.c_str(), key))
        return true;

    // 同名の差し替えが使えなければ、新しい標準モーションを使う。
    if (builtIn)
        return path != builtIn->path && model.LoadAnimation(builtIn->path, key);

    return false;
}

// =========================================================
// 本編は標準モーション必須。プレビューは未対応の骨格も基準姿勢で確認する。
// =========================================================
HeroineAnimationAssets::LoadResult HeroineAnimationAssets::loadCharacter(AnimationModel& model, const char* path, bool requireMotions)
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
        if (!loadMotion(model, clip.name, path) && requireMotions)
        {
            model.Uninit();
            return LoadResult::motionMismatch;
        }
    }

    return LoadResult::loaded;
}
