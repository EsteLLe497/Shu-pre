#pragma once
#include <array>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

enum class HeroineEmotion { neutral, happy, troubled, surprised, angry, count };

inline const char* emotionName(HeroineEmotion emotion)
{
    const char* names[] = { "neutral", "happy", "troubled", "surprised", "angry" };
    return emotion >= HeroineEmotion::neutral && emotion < HeroineEmotion::count ? names[static_cast<size_t>(emotion)] : names[0];
}

inline bool parseEmotion(const std::string& name, HeroineEmotion& emotion)
{
    for (int i = 0; i < static_cast<int>(HeroineEmotion::count); ++i)
        if (name == emotionName(static_cast<HeroineEmotion>(i))) { emotion = static_cast<HeroineEmotion>(i); return true; }
    return false;
}

// Independent of the renderer and LLM. Only resolved model morphs reach the adapter.
class HeroineExpression
{
public:
    enum class Phase { idle, thinking, speaking };
    struct Channel { std::string name; float weight = 0; };
    using Preset = std::array<Channel, 3>; // Brow, eyes, mouth.
    using Weights = std::vector<std::pair<std::string, float>>;
    static constexpr size_t emotionCount = static_cast<size_t>(HeroineEmotion::count);
    static constexpr size_t maxChannels = (emotionCount - 1) * 3;

    HeroineExpression();
    void bind(const std::vector<std::string>& names);
    bool available(const std::string& name) const;
    const std::vector<std::string>& morphNames() const { return m_names; }
    Preset& preset(HeroineEmotion emotion) { return m_presets[static_cast<size_t>(emotion)]; }
    const Preset& preset(HeroineEmotion emotion) const { return m_presets[static_cast<size_t>(emotion)]; }
    void setConversation(Phase phase, HeroineEmotion emotion = HeroineEmotion::neutral);
    void preview(HeroineEmotion emotion, float strength = 1.0f);
    void useConversation() { m_preview = false; }
    bool previewing() const { return m_preview; }
    Phase phase() const { return m_phase; }
    HeroineEmotion target() const;
    void update(float dt);
    const Weights& weights() const { return m_weights; }
    float blendWeight() const;
    bool read(std::istream& input);
    void write(std::ostream& output) const;
    bool enabled = true;

private:
    std::array<Preset, emotionCount> m_presets;
    std::array<float, emotionCount> m_blend{};
    std::vector<std::string> m_names;
    Weights m_weights;
    Phase m_phase = Phase::idle;
    HeroineEmotion m_emotion = HeroineEmotion::neutral;
    HeroineEmotion m_previewEmotion = HeroineEmotion::neutral;
    float m_previewStrength = 1;
    bool m_preview = false;
};
