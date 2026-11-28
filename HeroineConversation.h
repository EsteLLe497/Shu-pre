#pragma once
#include "GameObject.h"
#include "AI/DialogueSession.h"

// Main-thread adapter. The AI worker receives values, never pointers into the world.
class HeroineConversation : public GameObject
{
public:
    struct Line { bool heroine; std::string text; };
    void Init() override;
    void Update() override;
    void Draw() override {};
    void Uninit() override;
    bool ask(const std::string& text);
    void cancel();
    void clear();
    bool busy() const { return m_session && m_session->busy(); }
    AI::DialogueConfig& config() { return m_config; }
    bool saveConfig();
    bool loadConfig();
    void showSubtitle();
    void setSubtitleDuration(float seconds);
    float subtitleElapsed() const { return m_subtitleDuration - m_subtitleSeconds; }
    const std::string& subtitle() const { return m_subtitle; }
    bool subtitleVisible() const { return m_subtitleSeconds > 0 && !m_subtitle.empty(); }
    const std::string& status() const { return m_status; }
    const std::vector<Line>& lines() const { return m_lines; }
    HeroineEmotion emotion() const { return m_emotion; }
    HeroineGestureKind gestureKind() const { return m_gesture; }
    float lastElapsedSeconds() const { return m_lastElapsed; }

private:
    std::unique_ptr<AI::DialogueSession> m_session;
    AI::DialogueConfig m_config;
    std::vector<Line> m_lines;
    std::string m_subtitle;
    std::string m_status;
    float m_subtitleSeconds = 0;
    float m_subtitleDuration = 0;
    float m_lastElapsed = 0;
    HeroineEmotion m_emotion = HeroineEmotion::neutral;
    HeroineGestureKind m_gesture = HeroineGestureKind::none;
    bool m_cancelling = false;
    bool m_waitingReply = false;
};
