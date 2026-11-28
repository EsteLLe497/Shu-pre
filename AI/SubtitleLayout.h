#pragma once
#include <functional>
#include <string>
#include <vector>

namespace AI
{
    struct SubtitlePage
    {
        std::vector<std::string> lines;
        float seconds = 3.0f;
    };

    namespace SubtitleLayout
    {
        using Measure = std::function<float(const std::string&)>;
        std::vector<SubtitlePage> paginate(const std::string& text, float width, const Measure& measure);
        float duration(const std::vector<SubtitlePage>& pages);
        size_t pageAt(const std::vector<SubtitlePage>& pages, float elapsed);
    }
}
