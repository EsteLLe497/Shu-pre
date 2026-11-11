// =========================================================
// ファイルの情報[ActionSequence.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "ActionSequence.h"
#include <algorithm>

bool hasExpressionMorphs(const ActionSequence& action, const std::vector<std::string>& names)
{
    const auto has = [&](const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };

    return has(action.targetMorph) && (action.baseMorph.empty() || has(action.baseMorph))
        && std::all_of(action.extraMorphs.begin(), action.extraMorphs.end(), [&](const MorphTarget& morph) {
            return has(morph.name);
        });
}

// =========================================================
// プレビューと本編で同じ表情のカーブを使う
// =========================================================
float sampleExpression(float time, float start, float peak, float end, float strength, bool smooth)
{
    if (time < start || time > end)
        return 0.0f;

    if (time == peak)
        return strength;

    const float phase = time < peak ? (time - start) / (peak - start) : (end - time) / (end - peak);
    return strength * (smooth ? phase * phase * (3.0f - 2.0f * phase) : phase);
}

void ActionPlayback::start(float time)
{
    if (m_active)
        return;

    m_time = time;
    m_effectFired = false;
    m_active = true;
}

bool ActionPlayback::takeEffect(const ActionSequence& action)
{
    if (!m_active || !action.effectEnabled || m_effectFired || m_time < action.effectStart)
        return false;

    m_effectFired = true;
    return true;
}

void ActionPlayback::advance(const ActionSequence& action, float dt)
{
    advanceTo(action.duration, dt, false);
}

void ActionPlayback::advanceTo(float endTime, float dt, bool holdAtEnd)
{
    if (!m_active)
        return;

    // 終端にあるイベントも、最後のサンプルで一度処理する。
    if (m_time >= endTime)
        m_active = holdAtEnd;
    else
        m_time = std::min(endTime, m_time + dt);
}
