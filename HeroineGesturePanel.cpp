#include "main.h"
#include "renderer.h"
#include "Heroine.h"
#include "HeroineGesturePanel.h"
#include "imgui/imgui.h"
#include <fstream>
#include <locale>

namespace
{
    const char* path = "asset/ai/heroine-gestures.cfg";
    const char* status = nullptr;
    bool save(const HeroineGesture::Settings& settings)
    {
        if (!settings.valid()) return false;
        char temporary[MAX_PATH]{};
        if (!GetTempFileNameA("asset/ai", "gst", 0, temporary)) return false;
        std::ofstream output(temporary);
        output.imbue(std::locale::classic()); settings.write(output); output.close();
        const bool saved = output && MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved) DeleteFileA(temporary);
        return saved;
    }
}

void HeroineGesturePanel::draw(Heroine& heroine)
{
    auto& gesture = heroine.gesture();
    auto& settings = gesture.settings;
    ImGui::TextWrapped(u8"会話に小さなうなずき・首かしげを重ねます。追加のモーション素材は不要です。");
    ImGui::Checkbox(u8"会話の仕草を有効にする", &gesture.enabled);
    ImGui::Text(u8"%s / %s", gesture.previewing() ? u8"手動優先" : u8"会話連動", gesture.active() ? gestureName(gesture.kind()) : u8"待機");
    ImGui::BeginDisabled(!heroine.gesturesReady());
    if (ImGui::Button(u8"うなずきを試す")) gesture.preview(HeroineGestureKind::nod);
    ImGui::SameLine();
    if (ImGui::Button(u8"首かしげを試す")) gesture.preview(HeroineGestureKind::tilt);
    if (ImGui::Button(u8"停止")) gesture.stop();
    ImGui::SameLine();
    if (ImGui::Button(u8"会話連動に戻す")) gesture.useConversation();
    ImGui::ProgressBar(gesture.progress(), { -1, 0 });
    ImGui::SliderFloat(u8"うなずきの強さ", &settings.nodDegrees, 0, 12, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat(u8"うなずきの時間", &settings.nodSeconds, 0.5f, 3, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat(u8"首かしげの強さ", &settings.tiltDegrees, 0, 10, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat(u8"首かしげの時間", &settings.tiltSeconds, 0.5f, 3, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    if (!heroine.gesturesReady()) ImGui::TextWrapped(u8"頭のボーンが見つからないため、このモデルでは仕草を適用しません。");
    const auto offset = gesture.offset();
    ImGui::Text(u8"仕草の追加角度: 上下 %.1f / 傾き %.1f deg", XMConvertToDegrees(offset.pitch), XMConvertToDegrees(offset.roll));
    ImGui::TextWrapped(u8"うなずきは視線と合成後も『視線・Look At』の頭上下上限で制限します。設定変更は次の再生から反映します。");
    ImGui::TextWrapped(u8"手動再生後は『会話連動に戻す』で解除してください。F3を閉じても手動優先は維持します。");
    if (ImGui::Button(u8"仕草設定を保存")) status = save(settings) ? u8"仕草設定を保存しました。" : u8"保存できませんでした。";
    ImGui::SameLine();
    if (ImGui::Button(u8"仕草設定を読込"))
    {
        std::ifstream input(path); input.imbue(std::locale::classic());
        status = settings.read(input) ? u8"仕草設定を読み込みました。" : u8"設定が不正です。現在の設定を維持します。";
    }
    if (status) ImGui::TextWrapped("%s", status);
}
