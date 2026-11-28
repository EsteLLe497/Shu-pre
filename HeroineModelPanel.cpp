#include "main.h"
#include "renderer.h"
#include "Heroine.h"
#include "HeroineAnimationAssets.h"
#include "HeroineModelPanel.h"
#include "imgui/imgui.h"

namespace
{
    std::string fileName(const std::string& path)
    {
        const auto slash = path.find_last_of("/\\");
        return path.substr(slash == std::string::npos ? 0 : slash + 1);
    }
}

void HeroineModelPanel::draw(Heroine& heroine)
{
    static auto paths = HeroineAnimationAssets::listCharacters();
    const auto current = fileName(heroine.modelPath());
    ImGui::TextWrapped(u8"本編に表示するヒロインのモデルを切り替えます。");
    if (ImGui::BeginCombo(u8"モデル", current.empty() ? u8"未読込" : current.c_str()))
    {
        for (const auto& path : paths)
            if (ImGui::Selectable(fileName(path).c_str(), path == heroine.modelPath())) heroine.setModel(path);
        ImGui::EndCombo();
    }
    if (ImGui::Button(u8"モデル一覧を更新")) paths = HeroineAnimationAssets::listCharacters();
    ImGui::SameLine();
    ImGui::BeginDisabled(heroine.modelPath().empty());
    if (ImGui::Button(u8"現在のモデルを再読込"))
    {
        const auto path = heroine.modelPath();
        heroine.setModel(path);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", heroine.modelPath().c_str());
    ImGui::TextWrapped("%s", heroine.modelStatus().c_str());
    ImGui::Separator();
    ImGui::Text(u8"表情モーフ: %zu", heroine.expression().morphNames().size());
    ImGui::Text(u8"瞳の追従: %s / 閉眼: %s", heroine.eyesReady() ? u8"対応" : u8"未対応", heroine.eyelidsReady() ? u8"対応" : u8"未対応");
    ImGui::TextWrapped(u8"切替時は位置と向きを維持し、移動テスト・単発モーションを停止して待機へ戻します。会話履歴は維持します。");
    ImGui::TextWrapped(u8"この選択は起動中のみ有効です。再起動すると標準モデルへ戻ります。");
    if (heroine.expression().morphNames().empty())
        ImGui::TextWrapped(u8"表情と閉眼を使う場合は、シェイプキーを含むFBXを用意してください。");
}
