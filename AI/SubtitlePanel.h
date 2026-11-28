#pragma once
#include <string>
class HeroineConversation;

namespace AI
{
    namespace SubtitlePanel
    {
        void initFont(const std::string& fallbackPath);
        void reset();
        void draw(HeroineConversation& conversation);
        void drawSettings();
    }
}
