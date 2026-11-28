#include "SubtitleLayout.h"
#include <algorithm>

namespace
{
    bool contains(const std::string& set, const std::string& glyph)
    {
        return !glyph.empty() && set.find(glyph) != std::string::npos;
    }
}

std::vector<AI::SubtitlePage> AI::SubtitleLayout::paginate(const std::string& text, float width, const Measure& measure)
{
    std::vector<std::string> glyphs;
    for (size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        size_t count = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        count = std::min(count, text.size() - i);
        glyphs.push_back(text.substr(i, count));
        i += count;
    }
    const std::string closing = u8"、。，．！？!?：；)]}）」』】〉》ぁぃぅぇぉっゃゅょァィゥェォッャュョー";
    const std::string opening = u8"([{（「『【〈《";
    std::vector<SubtitlePage> pages;
    for (size_t start = 0; start < glyphs.size();)
    {
        while (start < glyphs.size() && (glyphs[start] == " " || glyphs[start] == "\n" || glyphs[start] == "\r" || glyphs[start] == "\t")) ++start;
        if (start == glyphs.size()) break;
        size_t end = start;
        size_t space = start;
        std::string line;
        while (end < glyphs.size() && glyphs[end] != "\n" && glyphs[end] != "\r")
        {
            if (end > start && measure(line + glyphs[end]) > width) break;
            if (glyphs[end] == " ") space = end;
            line += glyphs[end++];
        }
        // Prefer word boundaries and keep Japanese closing marks with the previous glyph.
        if (end < glyphs.size() && glyphs[end] != "\n" && glyphs[end] != "\r")
        {
            if (space > start && glyphs[end] != " ") end = space;
            while (end > start + 1 && (contains(closing, glyphs[end]) || contains(opening, glyphs[end - 1]))) --end;
        }
        line.clear();
        for (size_t i = start; i < end; ++i) line += glyphs[i];
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (pages.empty() || pages.back().lines.size() == 2) pages.push_back({});
        pages.back().lines.push_back(line);
        start = end;
    }
    for (auto& page : pages)
    {
        size_t count = 0;
        for (const auto& line : page.lines)
            for (unsigned char c : line) if ((c & 0xc0) != 0x80) ++count;
        page.seconds = std::max(3.0f, 0.8f + static_cast<float>(count) / 8.0f);
    }
    return pages;
}

float AI::SubtitleLayout::duration(const std::vector<SubtitlePage>& pages)
{
    float seconds = 0;
    for (const auto& page : pages) seconds += page.seconds;
    return seconds;
}

size_t AI::SubtitleLayout::pageAt(const std::vector<SubtitlePage>& pages, float elapsed)
{
    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (elapsed < pages[i].seconds) return i;
        elapsed -= pages[i].seconds;
    }
    return pages.empty() ? 0 : pages.size() - 1;
}
