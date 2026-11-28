#include "../main.h"
#include "../renderer.h"
#include "../HeroineConversation.h"
#include "SubtitlePanel.h"
#include "SubtitleLayout.h"
#include "../imgui/imgui.h"
#include <cmath>

namespace
{
    ImFont* subtitleFont = nullptr;
    float fontScale = 1.0f;
    std::string cachedText;
    float cachedWidth = 0;
    float cachedSize = 0;
    std::vector<AI::SubtitlePage> pages;
}

void AI::SubtitlePanel::initFont(const std::string& fallbackPath)
{
    reset();
    const char* path = "asset/fonts/cinecaption226.ttf";
    auto& io = ImGui::GetIO();
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    subtitleFont = io.Fonts->AddFontFromFileTTF(path, 32.0f);
    if (subtitleFont && !fallbackPath.empty())
    {
        ImFontConfig config;
        config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(fallbackPath.c_str(), 32.0f, &config);
    }
}

void AI::SubtitlePanel::reset()
{
    subtitleFont = nullptr;
    cachedText.clear(); pages.clear();
    cachedWidth = cachedSize = 0;
}

void AI::SubtitlePanel::drawSettings()
{
    if (!ImGui::CollapsingHeader(u8"映画字幕")) return;
    ImGui::TextUnformatted(subtitleFont ? u8"フォント: Cinecaption 2.26" : u8"フォント: 標準（指定ファイルなし）");
    ImGui::SliderFloat(u8"字幕サイズ", &fontScale, 0.75f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(u8"長い返答は2行ごとに自動切替します。設定は今回の起動中のみ有効です。");
}

void AI::SubtitlePanel::draw(HeroineConversation& conversation)
{
    if (!conversation.subtitleVisible()) return;
    const auto size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0 || size.y <= 0) return;
    auto* list = ImGui::GetForegroundDrawList();
    const float fontSize = std::max(12.0f, std::min(size.y * 0.046f * fontScale, size.x / 12.0f));
    const float width = std::max(1.0f, std::min(size.x * 0.80f, fontSize * 22.0f));
    ImGui::PushFont(subtitleFont ? subtitleFont : ImGui::GetFont(), fontSize);
    if (cachedText != conversation.subtitle() || cachedWidth != width || cachedSize != fontSize)
    {
        cachedText = conversation.subtitle(); cachedWidth = width; cachedSize = fontSize;
        pages = SubtitleLayout::paginate(cachedText, width, [](const std::string& text) { return ImGui::CalcTextSize(text.c_str()).x; });
    }
    if (!pages.empty())
    {
        conversation.setSubtitleDuration(SubtitleLayout::duration(pages));
        const auto& page = pages[SubtitleLayout::pageAt(pages, conversation.subtitleElapsed())];
        const float lineHeight = fontSize * 1.18f;
        const float height = fontSize + (page.lines.size() - 1) * lineHeight;
        const float bottom = size.y * 0.06f;
        float y = size.y - bottom - height;
        const float outline = std::max(1.0f, fontSize / 28.0f);
        for (const auto& line : page.lines)
        {
            const float x = std::floor((size.x - ImGui::CalcTextSize(line.c_str()).x) * 0.5f);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy) list->AddText({ x + dx * outline, y + dy * outline }, IM_COL32(0, 0, 0, 230), line.c_str());
            list->AddText({ x, y }, IM_COL32(255, 255, 255, 255), line.c_str());
            y += lineHeight;
        }
    }
    ImGui::PopFont();
}
