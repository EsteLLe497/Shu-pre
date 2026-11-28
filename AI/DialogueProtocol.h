#pragma once

#include "DialogueTypes.h"

namespace AI
{
    bool validInput(const std::string& text);
    bool validDialogue(const std::string& text);
    std::string showRequest(const DialogueConfig& config);
    std::string chatRequest(const DialogueRequest& request);
    bool localModelResponse(const std::string& response);
    bool parseDialogue(const std::string& response, std::string& dialogue);
    bool parseDialogue(const std::string& response, std::string& dialogue, HeroineEmotion& emotion);
    bool parseDialogue(const std::string& response, std::string& dialogue, HeroineEmotion& emotion, HeroineGestureKind& gesture);
    const char* fallbackDialogue();
    const char* statusText(DialogueCode code);
}
