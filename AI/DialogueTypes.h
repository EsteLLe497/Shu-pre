#pragma once

#include <string>
#include <vector>
#include <iosfwd>
#include "../HeroineExpression.h"
#include "../HeroineGesture.h"

namespace AI
{
    enum class DialogueCode { success, unavailable, timeout, cancelled, invalidResponse, remoteModel };

    struct DialogueConfig
    {
        std::string model = "qwen3:4b";
        unsigned int port = 11434;
        unsigned int timeoutMs = 45000;
        unsigned int maxTokens = 320;
        unsigned int contextTokens = 4096;
        std::string personality;
        bool valid() const;
        bool read(std::istream& input);
        void write(std::ostream& output) const;
    };

    struct DialogueContext
    {
        float playerDistance = 0;
        bool heroineMoving = false;
    };

    struct DialogueTurn
    {
        std::string player;
        std::string heroine;
    };

    struct DialogueRequest
    {
        DialogueConfig config;
        DialogueContext context;
        std::string text;
        std::vector<DialogueTurn> history;
    };

    struct DialogueResult
    {
        DialogueCode code = DialogueCode::unavailable;
        std::string dialogue;
        float elapsedSeconds = 0;
        unsigned int httpStatus = 0;
        HeroineEmotion emotion = HeroineEmotion::neutral;
        HeroineGestureKind gesture = HeroineGestureKind::none;
    };
}
