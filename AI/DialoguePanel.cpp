#include "../main.h"
#include "../renderer.h"
#include "../HeroineConversation.h"
#include "DialoguePanel.h"
#include "DialogueProtocol.h"
#include "SubtitlePanel.h"
#include "../imgui/imgui.h"
#include <cstring>

void AI::DialoguePanel::draw(HeroineConversation& conversation)
{
    static char input[1601]{};
    const bool busy = conversation.busy();
    ImGui::TextWrapped(u8"ローカルOllamaへ話しかけます。返答は字幕と表情に反映します。移動やモーションは自動実行しません。");
    ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted(u8"話しかける内容（400文字まで）");
    ImGui::InputTextMultiline("##Dialogue input", input, sizeof(input), { -1, 90 });
    ImGui::BeginDisabled(!validInput(input) || !conversation.config().valid());
    if (ImGui::Button(u8"送信", { 100, 0 })) if (conversation.ask(input)) input[0] = '\0';
    ImGui::EndDisabled(); ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button(u8"キャンセル")) conversation.cancel();
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button(u8"会話をクリア")) conversation.clear();
    ImGui::TextWrapped("%s", conversation.status().c_str());
    ImGui::Text(u8"返答の表情: %s", emotionName(conversation.emotion()));
    if (conversation.lastElapsedSeconds() > 0) ImGui::Text(u8"直前の応答: %.1f 秒", conversation.lastElapsedSeconds());
    if (busy) ImGui::TextDisabled(u8"推論中。F3を閉じても処理は続きます。");
    if (ImGui::CollapsingHeader(u8"Ollama設定"))
    {
        auto& config = conversation.config();
        ImGui::BeginDisabled(busy);
        char model[97]{}; strncpy_s(model, config.model.c_str(), _TRUNCATE);
        if (ImGui::InputText(u8"モデル名", model, sizeof(model))) config.model = model;
        int port = static_cast<int>(config.port);
        if (ImGui::InputInt(u8"ポート", &port)) config.port = port > 0 ? static_cast<unsigned int>(port) : 0;
        int seconds = static_cast<int>(config.timeoutMs / 1000);
        if (ImGui::SliderInt(u8"応答上限（秒）", &seconds, 5, 120, "%d", ImGuiSliderFlags_AlwaysClamp)) config.timeoutMs = seconds * 1000;
        int tokens = static_cast<int>(config.maxTokens);
        if (ImGui::SliderInt(u8"最大生成トークン", &tokens, 64, 1024, "%d", ImGuiSliderFlags_AlwaysClamp)) config.maxTokens = tokens;
        ImGui::TextDisabled("http://127.0.0.1:%u", config.port);
        if (ImGui::Button(u8"AI設定を保存")) conversation.saveConfig();
        ImGui::SameLine();
        if (ImGui::Button(u8"AI設定を読込")) conversation.loadConfig();
        ImGui::EndDisabled();
        ImGui::TextWrapped(u8"Ollamaなしでもゲームは動作します。未接続・失敗時は定型文を表示します。");
    }
    SubtitlePanel::drawSettings();
    ImGui::Separator();
    ImGui::TextUnformatted(u8"最近の会話（このシーンのメモリ内のみ）");
    if (ImGui::BeginChild("Dialogue history", { 0, 180 }, ImGuiChildFlags_Borders))
        for (const auto& line : conversation.lines())
        {
            ImGui::TextColored(line.heroine ? ImVec4(0.86f, 0.75f, 1, 1) : ImVec4(0.65f, 0.85f, 1, 1), "%s", line.heroine ? u8"ヒロイン" : u8"プレイヤー");
            ImGui::TextWrapped("%s", line.text.c_str()); ImGui::Spacing();
        }
    ImGui::EndChild();
    if (!conversation.subtitle().empty() && ImGui::Button(u8"字幕をもう一度表示")) conversation.showSubtitle();
}
