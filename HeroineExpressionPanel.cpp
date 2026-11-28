#include "main.h"
#include "renderer.h"
#include "Heroine.h"
#include "HeroineExpressionPanel.h"
#include "Player.h"
#include "Camera.h"
#include "manager.h"
#include "imgui/imgui.h"
#include <fstream>
#include <locale>

namespace
{
    const char* settingsPath = "asset/ai/heroine-expressions.cfg";
    const char* labels[] = { u8"通常", u8"笑顔", u8"困る", u8"驚き", u8"怒り" };
    const char* status = nullptr;

    bool save(const HeroineExpression& expression)
    {
        char temporary[MAX_PATH]{};
        if (!GetTempFileNameA("asset/ai", "exp", 0, temporary)) return false;
        std::ofstream output(temporary);
        output.imbue(std::locale::classic()); expression.write(output); output.close();
        const bool saved = output && MoveFileExA(temporary, settingsPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved) DeleteFileA(temporary);
        return saved;
    }
}

void HeroineExpressionPanel::draw(Heroine& heroine)
{
    auto& expression = heroine.expression();
    static int selected = 1;
    static float strength = 1;
    ImGui::TextWrapped(u8"既存モーフを眉・目・口で組み合わせます。Ollamaなしで確認できます。");
    ImGui::Checkbox(u8"会話に合わせて表情を変える", &expression.enabled);
    const char* phase = expression.phase() == HeroineExpression::Phase::thinking ? u8"考え中"
        : expression.phase() == HeroineExpression::Phase::speaking ? u8"発話中（字幕）" : u8"待機";
    ImGui::Text(u8"%s / %s / %s", phase, labels[static_cast<int>(expression.target())], expression.previewing() ? u8"手動優先" : u8"会話連動");
    ImGui::Text(u8"使用可能なモーフ: %zu", expression.morphNames().size());
    ImGui::Separator();
    const bool changed = ImGui::Combo(u8"表情", &selected, u8"通常\0笑顔\0困る\0驚き\0怒り\0");
    const bool gainChanged = ImGui::SliderFloat(u8"プレビュー強度", &strength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    const auto emotion = static_cast<HeroineEmotion>(selected);
    const bool play = ImGui::Button(u8"この表情を試す");
    if (changed || gainChanged || play) expression.preview(emotion, strength);
    ImGui::SameLine();
    if (ImGui::Button(u8"会話連動に戻す")) expression.useConversation();
    ImGui::TextDisabled(u8"F3を閉じても手動プレビューは維持します。");
    if (selected != 0)
    {
        auto& preset = expression.preset(emotion);
        const char* parts[] = { u8"眉", u8"目", u8"口" };
        for (size_t i = 0; i < preset.size(); ++i)
        {
            auto& channel = preset[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(parts[i]);
            if (ImGui::BeginCombo("##Morph", channel.name.empty() ? u8"使用しない" : channel.name.c_str()))
            {
                if (ImGui::Selectable(u8"使用しない", channel.name.empty())) channel.name.clear();
                for (const auto& name : expression.morphNames())
                    if (ImGui::Selectable(name.c_str(), name == channel.name)) channel.name = name;
                ImGui::EndCombo();
            }
            ImGui::SliderFloat(u8"強さ", &channel.weight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (!channel.name.empty() && !expression.available(channel.name))
                ImGui::TextColored({ 1, 0.65f, 0.3f, 1 }, u8"このモーフはモデルにありません（適用せずスキップ）");
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    if (ImGui::Button(u8"表情設定を保存")) status = save(expression) ? u8"表情設定を保存しました。" : u8"保存できませんでした。";
    ImGui::SameLine();
    if (ImGui::Button(u8"表情設定を読込"))
    {
        std::ifstream input(settingsPath); input.imbue(std::locale::classic());
        status = expression.read(input) ? u8"表情設定を読み込みました。" : u8"設定が不正です。現在の設定を維持します。";
    }
    if (status) ImGui::TextWrapped("%s", status);
    if (ImGui::CollapsingHeader(u8"表情確認用の配置"))
    {
        ImGui::TextWrapped(u8"ヒロインをプレイヤーの前1.1mへ移し、こちらへ向けます。配置は保存しません。");
        if (ImGui::Button(u8"ヒロインを目の前に配置"))
        {
            auto* player = Manager::GetGameObj<Player>();
            auto* camera = Manager::GetGameObj<Camera>();
            if (player && camera)
            {
                auto forward = camera->GetFoward(); forward.y = 0;
                if (forward.lenght() > 0.001f)
                {
                    forward.normalize();
                    if (heroine.resetMovement(player->GetPosition() + forward * 1.1f))
                    {
                        heroine.SetRotation({ 0, atan2f(-forward.x, -forward.z), 0 });
                        heroine.lookAtPlayer();
                        status = u8"確認用に配置しました。";
                    }
                    else status = u8"障害物・地形外のため配置できません。少し場所を変えてください。";
                }
            }
        }
    }
}
