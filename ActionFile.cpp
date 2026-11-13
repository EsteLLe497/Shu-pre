// =========================================================
// ファイルの情報[ActionFile.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "ActionFile.h"
#include "ActionSequence.h"
#include "PlayerAnimationAssets.h"
#include <cmath>
#include <fstream>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#pragma comment(lib, "windowsapp.lib")

namespace
{
    using namespace winrt::Windows::Data::Json;

    constexpr char actionDirectory[] = "asset/actions";
    constexpr int formatVersion = 1;
    constexpr std::streamoff maxFileSize = 128 * 1024;

    struct JsonApartment
    {
        JsonApartment() { winrt::init_apartment(); }
        ~JsonApartment() { winrt::uninit_apartment(); }
    };

    // 連続で読み書きするときも、スレッドが終わるまで WinRT を維持する。
    void ensureJsonApartment()
    {
        static thread_local const JsonApartment apartment;
    }

    bool inRange(float value, float minimum, float maximum)
    {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }

    bool validExtraMorphs(const ActionSequence& action)
    {
        const auto& morphs = action.extraMorphs;
        if (morphs.size() > MorphTarget::maxExtraTargets)
            return false;

        for (auto it = morphs.begin(); it != morphs.end(); ++it)
        {
            if (it->name.empty() || it->name.size() > 1024 || it->name == action.targetMorph
                || !inRange(it->weight, 0.0f, 1.0f) || !inRange(it->start, 0.0f, it->peak)
                || !inRange(it->peak, it->start, it->end) || !inRange(it->end, it->peak, action.duration))
                return false;

            if (std::any_of(morphs.begin(), it, [&](const MorphTarget& other) { return other.name == it->name; }))
                return false;
        }

        return true;
    }

    bool validWeapon(const WeaponTrack& weapon, float duration)
    {
        const auto validVector = [](const auto& values, float limit) {
            return std::all_of(values.begin(), values.end(),
                [limit](float value) { return inRange(value, -limit, limit); });
        };
        return (!weapon.enabled || !weapon.model.empty())
            && weapon.model.size() <= 240
            && inRange(weapon.appear, 0.0f, weapon.middle)
            && inRange(weapon.middle, weapon.appear, weapon.disappear)
            && inRange(weapon.disappear, weapon.middle, duration)
            && inRange(weapon.scale, 0.001f, 20.0f)
            && inRange(weapon.trailLength, 0.01f, 1.0f)
            && inRange(weapon.dissolveDuration, 0.08f, 1.5f)
            && inRange(weapon.dissolveGlow, 0.1f, 3.0f)
            && inRange(weapon.particleCount, 8, 128)
            && validVector(weapon.trailColor, 1.0f)
            && validVector(weapon.dissolveColor, 1.0f)
            && validVector(weapon.particleColor, 1.0f)
            && validVector(weapon.pivot, 10.0f)
            && validVector(weapon.trailBase, 10.0f) && validVector(weapon.trailTip, 10.0f)
            && validVector(weapon.cameraTarget, 10.0f)
            && std::all_of(weapon.keys.begin(), weapon.keys.end(), [&](const auto& key) {
                return validVector(key.position, 10.0f) && validVector(key.angles, 1440.0f);
            });
    }

    JsonArray encodeVector(const std::array<float, 3>& vector)
    {
        JsonArray values;
        for (float value : vector)
            values.Append(JsonValue::CreateNumberValue(value));
        return values;
    }

    std::array<float, 3> decodeVector(const JsonArray& values)
    {
        if (values.Size() != 3)
            throw winrt::hresult_invalid_argument();
        return { static_cast<float>(values.GetNumberAt(0)), static_cast<float>(values.GetNumberAt(1)),
            static_cast<float>(values.GetNumberAt(2)) };
    }

    JsonObject encodeWeapon(const WeaponTrack& weapon)
    {
        JsonObject data;
        data.SetNamedValue(L"model", JsonValue::CreateStringValue(winrt::to_hstring(weapon.model)));
        data.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(weapon.enabled));
        data.SetNamedValue(L"appear", JsonValue::CreateNumberValue(weapon.appear));
        data.SetNamedValue(L"middle", JsonValue::CreateNumberValue(weapon.middle));
        data.SetNamedValue(L"disappear", JsonValue::CreateNumberValue(weapon.disappear));
        data.SetNamedValue(L"scale", JsonValue::CreateNumberValue(weapon.scale));
        data.SetNamedValue(L"pivot", encodeVector(weapon.pivot));
        data.SetNamedValue(L"trail", JsonValue::CreateBooleanValue(weapon.trail));
        data.SetNamedValue(L"trailLength", JsonValue::CreateNumberValue(weapon.trailLength));
        data.SetNamedValue(L"trailColor", encodeVector(weapon.trailColor));
        JsonObject dissolve;
        dissolve.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(weapon.dissolve));
        dissolve.SetNamedValue(L"duration", JsonValue::CreateNumberValue(weapon.dissolveDuration));
        dissolve.SetNamedValue(L"glow", JsonValue::CreateNumberValue(weapon.dissolveGlow));
        dissolve.SetNamedValue(L"color", encodeVector(weapon.dissolveColor));
        dissolve.SetNamedValue(L"particles", JsonValue::CreateBooleanValue(weapon.dissolveParticles));
        dissolve.SetNamedValue(L"particleCount", JsonValue::CreateNumberValue(weapon.particleCount));
        dissolve.SetNamedValue(L"particleColor", encodeVector(weapon.particleColor));
        data.SetNamedValue(L"dissolve", dissolve);
        data.SetNamedValue(L"trailBase", encodeVector(weapon.trailBase));
        data.SetNamedValue(L"trailTip", encodeVector(weapon.trailTip));
        data.SetNamedValue(L"effectOnWeapon", JsonValue::CreateBooleanValue(weapon.effectOnWeapon));
        data.SetNamedValue(L"cameraEnabled", JsonValue::CreateBooleanValue(weapon.cameraEnabled));
        data.SetNamedValue(L"cameraTarget", encodeVector(weapon.cameraTarget));

        JsonArray keys;
        for (const auto& key : weapon.keys)
        {
            JsonObject pose;
            pose.SetNamedValue(L"position", encodeVector(key.position));
            pose.SetNamedValue(L"angles", encodeVector(key.angles));
            keys.Append(pose);
        }
        data.SetNamedValue(L"keys", keys);
        return data;
    }

    WeaponTrack decodeWeapon(const JsonObject& data)
    {
        WeaponTrack weapon;
        weapon.model = winrt::to_string(data.GetNamedString(L"model"));
        weapon.enabled = data.GetNamedBoolean(L"enabled");
        weapon.appear = static_cast<float>(data.GetNamedNumber(L"appear"));
        weapon.middle = static_cast<float>(data.GetNamedNumber(L"middle"));
        weapon.disappear = static_cast<float>(data.GetNamedNumber(L"disappear"));
        weapon.scale = static_cast<float>(data.GetNamedNumber(L"scale"));
        weapon.pivot = decodeVector(data.GetNamedArray(L"pivot"));
        weapon.trail = data.GetNamedBoolean(L"trail");
        weapon.trailLength = static_cast<float>(data.GetNamedNumber(L"trailLength"));
        if (data.HasKey(L"trailColor"))
            weapon.trailColor = decodeVector(data.GetNamedArray(L"trailColor"));
        if (data.HasKey(L"dissolve"))
        {
            const auto dissolve = data.GetNamedObject(L"dissolve");
            weapon.dissolve = dissolve.GetNamedBoolean(L"enabled");
            weapon.dissolveDuration = static_cast<float>(dissolve.GetNamedNumber(L"duration"));
            weapon.dissolveGlow = static_cast<float>(dissolve.GetNamedNumber(L"glow"));
            weapon.dissolveColor = decodeVector(dissolve.GetNamedArray(L"color"));
            weapon.dissolveParticles = dissolve.GetNamedBoolean(L"particles");
            const double count = dissolve.GetNamedNumber(L"particleCount");
            if (!(count >= 8 && count <= 128) || std::floor(count) != count)
                throw winrt::hresult_invalid_argument();
            weapon.particleCount = static_cast<int>(count);
            weapon.particleColor = decodeVector(dissolve.GetNamedArray(L"particleColor"));
        }
        weapon.trailBase = decodeVector(data.GetNamedArray(L"trailBase"));
        weapon.trailTip = decodeVector(data.GetNamedArray(L"trailTip"));
        weapon.effectOnWeapon = data.GetNamedBoolean(L"effectOnWeapon");
        weapon.cameraEnabled = data.GetNamedBoolean(L"cameraEnabled");
        weapon.cameraTarget = decodeVector(data.GetNamedArray(L"cameraTarget"));
        const auto keys = data.GetNamedArray(L"keys");
        if (keys.Size() != weapon.keys.size())
            throw winrt::hresult_invalid_argument();
        for (uint32_t i = 0; i < keys.Size(); ++i)
        {
            const auto pose = keys.GetObjectAt(i);
            weapon.keys[i].position = decodeVector(pose.GetNamedArray(L"position"));
            weapon.keys[i].angles = decodeVector(pose.GetNamedArray(L"angles"));
        }
        return weapon;
    }

    bool isValid(const ActionSequence& action)
    {
        return PlayerAnimationAssets::isMotionName(action.clip) && inRange(action.duration, 0.5f, 10.0f)
            && inRange(action.faceStart, 0.0f, action.facePeak)
            && inRange(action.facePeak, action.faceStart, action.faceEnd)
            && inRange(action.faceEnd, action.facePeak, action.duration)
            && inRange(action.faceStrength, 0.0f, 1.0f)
            && (!action.faceEnabled || !action.targetMorph.empty())
            && action.baseMorph.size() <= 1024 && action.targetMorph.size() <= 1024
            && inRange(action.speed, 0.1f, 2.0f)
            && (action.weapon.model.empty() ? !action.weapon.enabled : validWeapon(action.weapon, action.duration))
            && validExtraMorphs(action)
            && inRange(action.effectStart, 0.0f, action.duration)
            && inRange(action.effectX, -2.0f, 2.0f) && inRange(action.effectHeight, 0.0f, 2.0f);
    }

    JsonObject encode(const ActionSequence& action)
    {
        JsonObject motion;
        motion.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(action.motionEnabled));
        motion.SetNamedValue(L"clip", JsonValue::CreateStringValue(winrt::to_hstring(action.clip)));

        JsonObject expression;
        expression.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(action.faceEnabled));
        expression.SetNamedValue(L"base", JsonValue::CreateStringValue(winrt::to_hstring(action.baseMorph)));
        expression.SetNamedValue(L"target", JsonValue::CreateStringValue(winrt::to_hstring(action.targetMorph)));
        if (!action.extraMorphs.empty())
        {
            JsonArray extraTargets;
            for (const auto& morph : action.extraMorphs)
            {
                JsonObject target;
                target.SetNamedValue(L"name", JsonValue::CreateStringValue(winrt::to_hstring(morph.name)));
                target.SetNamedValue(L"weight", JsonValue::CreateNumberValue(morph.weight));
                target.SetNamedValue(L"start", JsonValue::CreateNumberValue(morph.start));
                target.SetNamedValue(L"peak", JsonValue::CreateNumberValue(morph.peak));
                target.SetNamedValue(L"end", JsonValue::CreateNumberValue(morph.end));
                target.SetNamedValue(L"smooth", JsonValue::CreateBooleanValue(morph.smooth));
                extraTargets.Append(target);
            }

            expression.SetNamedValue(L"extraTargets", extraTargets);
        }
        expression.SetNamedValue(L"start", JsonValue::CreateNumberValue(action.faceStart));
        expression.SetNamedValue(L"peak", JsonValue::CreateNumberValue(action.facePeak));
        expression.SetNamedValue(L"end", JsonValue::CreateNumberValue(action.faceEnd));
        expression.SetNamedValue(L"weight", JsonValue::CreateNumberValue(action.faceStrength));
        expression.SetNamedValue(L"smooth", JsonValue::CreateBooleanValue(action.smoothFaceBlend));

        JsonObject effect;
        effect.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(action.effectEnabled));
        effect.SetNamedValue(L"start", JsonValue::CreateNumberValue(action.effectStart));
        effect.SetNamedValue(L"offsetX", JsonValue::CreateNumberValue(action.effectX));
        effect.SetNamedValue(L"height", JsonValue::CreateNumberValue(action.effectHeight));

        JsonObject root;
        root.SetNamedValue(L"version", JsonValue::CreateNumberValue(formatVersion));
        root.SetNamedValue(L"duration", JsonValue::CreateNumberValue(action.duration));
        root.SetNamedValue(L"speed", JsonValue::CreateNumberValue(action.speed));
        if (!action.weapon.model.empty())
            root.SetNamedValue(L"weapon", encodeWeapon(action.weapon));
        root.SetNamedValue(L"motion", motion);
        root.SetNamedValue(L"expression", expression);
        root.SetNamedValue(L"effect", effect);
        return root;
    }

    ActionSequence decode(const JsonObject& root)
    {
        const auto motion = root.GetNamedObject(L"motion");
        const auto expression = root.GetNamedObject(L"expression");
        const auto effect = root.GetNamedObject(L"effect");

        ActionSequence action;
        action.duration = static_cast<float>(root.GetNamedNumber(L"duration"));
        action.speed = static_cast<float>(root.GetNamedNumber(L"speed", 1.0));
        if (root.HasKey(L"weapon"))
            action.weapon = decodeWeapon(root.GetNamedObject(L"weapon"));
        action.motionEnabled = motion.GetNamedBoolean(L"enabled");
        action.clip = PlayerAnimationAssets::canonicalMotionName(winrt::to_string(motion.GetNamedString(L"clip")));
        action.faceEnabled = expression.GetNamedBoolean(L"enabled");
        action.baseMorph = winrt::to_string(expression.GetNamedString(L"base"));
        action.targetMorph = winrt::to_string(expression.GetNamedString(L"target"));

        action.faceStart = static_cast<float>(expression.GetNamedNumber(L"start"));
        action.facePeak = static_cast<float>(expression.GetNamedNumber(L"peak"));
        action.faceEnd = static_cast<float>(expression.GetNamedNumber(L"end"));
        action.faceStrength = static_cast<float>(expression.GetNamedNumber(L"weight"));
        action.smoothFaceBlend = expression.GetNamedBoolean(L"smooth");

        if (expression.HasKey(L"extraTargets"))
        {
            const auto targets = expression.GetNamedArray(L"extraTargets");
            if (targets.Size() > MorphTarget::maxExtraTargets)
                throw winrt::hresult_invalid_argument();

            for (const auto& entry : targets)
            {
                const auto target = entry.GetObject();
                MorphTarget morph;
                morph.name = winrt::to_string(target.GetNamedString(L"name"));
                morph.weight = static_cast<float>(target.GetNamedNumber(L"weight"));
                if (!inRange(morph.weight, 0.0f, 1.0f))
                    throw winrt::hresult_invalid_argument();

                if (target.HasKey(L"start") || target.HasKey(L"peak") || target.HasKey(L"end")
                    || target.HasKey(L"smooth"))
                {
                    morph.start = static_cast<float>(target.GetNamedNumber(L"start"));
                    morph.peak = static_cast<float>(target.GetNamedNumber(L"peak"));
                    morph.end = static_cast<float>(target.GetNamedNumber(L"end"));
                    morph.smooth = target.GetNamedBoolean(L"smooth");
                }
                else
                {
                    // 旧データでは、主キーの時刻と強さを追加キーにも掛けていた。
                    morph.start = action.faceStart;
                    morph.peak = action.facePeak;
                    morph.end = action.faceEnd;
                    morph.weight *= action.faceStrength;
                    morph.smooth = action.smoothFaceBlend;
                }

                action.extraMorphs.push_back(std::move(morph));
            }
        }
        action.effectEnabled = effect.GetNamedBoolean(L"enabled");
        action.effectStart = static_cast<float>(effect.GetNamedNumber(L"start"));
        action.effectX = static_cast<float>(effect.GetNamedNumber(L"offsetX"));
        action.effectHeight = static_cast<float>(effect.GetNamedNumber(L"height"));
        return action;
    }

    bool writeAtomically(const char* path, const std::string& json)
    {
        if (!CreateDirectoryA(actionDirectory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;

        char temporary[MAX_PATH];
        if (!GetTempFileNameA(actionDirectory, "act", 0, temporary))
            return false;

        std::ofstream file(temporary, std::ios::binary);
        file << json << '\n';
        file.close();
        const bool saved = file && MoveFileExA(temporary, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved)
            DeleteFileA(temporary);

        return saved;
    }

    bool saveAction(const char* path, const ActionSequence& action)
    {
        if (!isValid(action))
            return false;

        try
        {
            ensureJsonApartment();
            return writeAtomically(path, winrt::to_string(encode(action).Stringify()));
        }
        catch (const winrt::hresult_error&)
        {
            return false;
        }
    }
}

// =========================================================
// 検証に通った JSON だけを反映し、保存時は途中のファイルを公開しない
// =========================================================
bool ActionFile::validate(const ActionSequence& action)
{
    return isValid(action);
}

bool ActionFile::load(const char* path, ActionSequence& action)
{
    if (!path || !*path)
        return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;

    const auto size = file.tellg();
    if (size <= 0 || size > maxFileSize)
        return false;

    std::string json(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(&json[0], static_cast<std::streamsize>(size)))
        return false;

    try
    {
        ensureJsonApartment();
        const auto root = JsonObject::Parse(winrt::to_hstring(json));
        if (root.GetNamedNumber(L"version") != formatVersion)
            return false;

        auto candidate = decode(root);
        if (!isValid(candidate))
            return false;

        action = std::move(candidate);
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}

bool ActionFile::loadAttack(ActionSequence& action)
{
    return load(attackPath, action);
}

// =========================================================
// 入力をファイル名として検証し、保存先を asset/actions に限定する
// =========================================================
std::string ActionFile::pathForName(const std::string& name)
{
    std::string stem = name;
    if (stem.size() >= 5 && _stricmp(stem.c_str() + stem.size() - 5, ".json") == 0)
        stem.resize(stem.size() - 5);

    if (stem.empty() || stem.size() > 64 || stem.front() == ' ' || stem.back() == ' ')
        return {};

    const bool validCharacters = std::all_of(stem.begin(), stem.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
    });
    if (!validCharacters)
        return {};

    constexpr const char* reserved[] = { "CON", "PRN", "AUX", "NUL" };
    for (const auto* device : reserved)
    {
        if (_stricmp(stem.c_str(), device) == 0)
            return {};
    }

    if (stem.size() == 4 && stem[3] >= '0' && stem[3] <= '9' &&
        (_strnicmp(stem.c_str(), "COM", 3) == 0 || _strnicmp(stem.c_str(), "LPT", 3) == 0))
        return {};

    return std::string(actionDirectory) + "/" + stem + ".json";
}

// =========================================================
// 保存フォルダーの JSON を列挙し、読み込める名前だけ一覧にする
// =========================================================
std::vector<std::string> ActionFile::listNames()
{
    std::vector<std::string> names;
    const auto pattern = std::string(actionDirectory) + "/*.json";
    WIN32_FIND_DATAA entry{};
    const HANDLE search = FindFirstFileA(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return names;

    do
    {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        const std::string fileName = entry.cFileName;
        if (fileName.size() <= 5 || _stricmp(fileName.c_str() + fileName.size() - 5, ".json") != 0)
            continue;

        const auto name = fileName.substr(0, fileName.size() - 5);
        if (!pathForName(name).empty())
            names.push_back(name);
    } while (FindNextFileA(search, &entry));

    FindClose(search);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    return names;
}

bool ActionFile::loadNamed(const std::string& name, ActionSequence& action)
{
    const auto path = pathForName(name);
    return !path.empty() && load(path.c_str(), action);
}

bool ActionFile::saveNamed(const std::string& name, const ActionSequence& action)
{
    const auto path = pathForName(name);
    return !path.empty() && saveAction(path.c_str(), action);
}

bool ActionFile::saveAttack(const ActionSequence& action)
{
    return saveAction(attackPath, action);
}
