#include "HeroineExpression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

HeroineExpression::HeroineExpression()
{
    m_presets[1] = {{ { "Fcl_BRW_Fun", 0.45f }, { "Fcl_EYE_Fun", 0.30f }, { "Fcl_MTH_Joy", 0.60f } }};
    m_presets[2] = {{ { "Fcl_BRW_Sorrow", 0.65f }, { "Fcl_EYE_Sorrow", 0.25f }, { "Fcl_MTH_Sorrow", 0.35f } }};
    m_presets[3] = {{ { "Fcl_BRW_Surprised", 0.65f }, { "Fcl_EYE_Surprised", 0.35f }, { "Fcl_MTH_Surprised", 0.45f } }};
    m_presets[4] = {{ { "Fcl_BRW_Angry", 0.50f }, { "Fcl_EYE_Angry", 0.25f }, { "Fcl_MTH_Angry", 0.35f } }};
}

void HeroineExpression::bind(const std::vector<std::string>& names)
{
    m_names = names;
    m_blend.fill(0);
    m_weights.clear();
}

bool HeroineExpression::available(const std::string& name) const
{
    return !name.empty() && std::find(m_names.begin(), m_names.end(), name) != m_names.end();
}

void HeroineExpression::setConversation(Phase phase, HeroineEmotion emotion)
{
    m_phase = phase;
    m_emotion = emotion >= HeroineEmotion::neutral && emotion < HeroineEmotion::count ? emotion : HeroineEmotion::neutral;
}

void HeroineExpression::preview(HeroineEmotion emotion, float strength)
{
    if (emotion < HeroineEmotion::neutral || emotion >= HeroineEmotion::count || !std::isfinite(strength)) return;
    m_preview = true;
    m_previewEmotion = emotion;
    m_previewStrength = std::max(0.0f, std::min(1.0f, strength));
}

HeroineEmotion HeroineExpression::target() const
{
    if (m_preview) return m_previewEmotion;
    if (!enabled || m_phase == Phase::idle) return HeroineEmotion::neutral;
    return m_phase == Phase::thinking ? HeroineEmotion::troubled : m_emotion;
}

void HeroineExpression::update(float dt)
{
    if (!std::isfinite(dt) || dt < 0) return;
    const float blend = 1.0f - std::exp(-8.0f * std::min(dt, 0.1f));
    const auto selected = target();
    const float strength = m_preview ? m_previewStrength : m_phase == Phase::thinking ? 0.35f : 1.0f;
    m_weights.clear();
    for (size_t i = 1; i < emotionCount; ++i)
    {
        const float desired = i == static_cast<size_t>(selected) ? strength : 0;
        m_blend[i] += (desired - m_blend[i]) * blend;
        if (m_blend[i] < 0.0001f) m_blend[i] = 0;
        for (const auto& channel : m_presets[i])
        {
            if (!available(channel.name) || !std::isfinite(channel.weight)) continue;
            const float weight = std::max(0.0f, std::min(1.0f, channel.weight)) * m_blend[i];
            if (weight <= 0) continue;
            const auto found = std::find_if(m_weights.begin(), m_weights.end(), [&](const auto& value) { return value.first == channel.name; });
            if (found == m_weights.end()) m_weights.push_back({ channel.name, weight });
            else found->second = std::min(1.0f, found->second + weight);
        }
    }
}

bool HeroineExpression::read(std::istream& input)
{
    auto candidate = m_presets;
    std::string format;
    int version = 0;
    if (!(input >> format >> version) || format != "heroineExpressions" || version != 1) return false;
    for (size_t i = 1; i < emotionCount; ++i)
        for (size_t part = 0; part < 3; ++part)
        {
            std::string emotion;
            size_t index = 0;
            auto& channel = candidate[i][part];
            if (!(input >> emotion >> index >> std::quoted(channel.name) >> channel.weight)
                || emotion != emotionName(static_cast<HeroineEmotion>(i)) || index != part
                || channel.name.size() > 128 || !std::isfinite(channel.weight) || channel.weight < 0 || channel.weight > 1) return false;
        }
    if (!(input >> std::ws).eof()) return false;
    m_presets = candidate;
    return true;
}

float HeroineExpression::blendWeight() const
{
    float weight = 0;
    for (size_t i = 1; i < emotionCount; ++i)
        for (const auto& channel : m_presets[i])
            if (channel.weight > 0 && available(channel.name)) { weight += m_blend[i]; break; }
    return std::min(1.0f, weight);
}

void HeroineExpression::write(std::ostream& output) const
{
    output << "heroineExpressions 1\n" << std::setprecision(9);
    for (size_t i = 1; i < emotionCount; ++i)
        for (size_t part = 0; part < 3; ++part)
            output << emotionName(static_cast<HeroineEmotion>(i)) << ' ' << part << ' '
                << std::quoted(m_presets[i][part].name) << ' ' << m_presets[i][part].weight << '\n';
}
