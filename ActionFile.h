// =========================================================
// ファイルの情報[ActionFile.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <string>
#include <vector>

struct ActionSequence;

namespace ActionFile
{
    constexpr char attackPath[] = "asset/actions/Attack01.json";
    bool validate(const ActionSequence& action);
    bool load(const char* path, ActionSequence& action);
    std::string pathForName(const std::string& name);
    std::vector<std::string> listNames();
    bool loadNamed(const std::string& name, ActionSequence& action);
    bool saveNamed(const std::string& name, const ActionSequence& action);
    bool loadAttack(ActionSequence& action);
    bool saveAttack(const ActionSequence& action);
}
