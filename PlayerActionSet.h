// =========================================================
// ファイルの情報[PlayerActionSet.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include "ActionSequence.h"
#include <array>

enum class PlayerActionKind { idle, walk, run, yawn, attack, jump, runJump, land, count };

enum class AttackFlow { attack01, attack02, finish };

// Input advances the combo; playback completion only consumes buffered clicks.
class AttackCombo
{
public:
    static constexpr unsigned int stageCount = 3;
    static constexpr float followupWindow = 0.65f;

    AttackFlow next() const { return m_next; }

    bool buffer()
    {
        const auto remaining = stageCount - static_cast<unsigned int>(m_next);
        if (!m_started || m_finishStarted || m_buffered >= remaining)
            return false;
        ++m_buffered;
        return true;
    }

    void started()
    {
        m_started = true;
        m_followupTime = 0.0f;
        if (m_next == AttackFlow::finish)
            m_finishStarted = true;
        else
            m_next = static_cast<AttackFlow>(static_cast<unsigned int>(m_next) + 1);
    }

    void finished()
    {
        if (m_finishStarted)
            reset();
        else if (m_started)
            m_followupTime = followupWindow;
    }

    bool takeBuffered()
    {
        if (!m_buffered)
            return false;
        --m_buffered;
        return true;
    }

    void advance(float dt)
    {
        if (m_followupTime <= 0.0f || dt <= 0.0f)
            return;
        m_followupTime -= dt;
        if (m_followupTime <= 0.0f)
            reset();
    }

    void reset()
    {
        m_next = AttackFlow::attack01;
        m_buffered = 0;
        m_started = m_finishStarted = false;
        m_followupTime = 0.0f;
    }

private:
    AttackFlow m_next = AttackFlow::attack01;
    unsigned int m_buffered = 0;
    bool m_started = false;
    bool m_finishStarted = false;
    float m_followupTime = 0.0f;
};

class PlayerActionSet
{
public:
    struct Slot
    {
        ActionSequence sequence;
        bool loaded = false;
        bool motionReady = false;
        bool faceReady = false;
    };

    void load();
    const Slot& attackSlot(AttackFlow flow) const
    {
        return m_attackSlots[static_cast<std::size_t>(flow)];
    }
    Slot& get(PlayerActionKind kind) { return m_slots[index(kind)]; }
    const Slot& get(PlayerActionKind kind) const { return m_slots[index(kind)]; }

    // 入力があるまでは、同じ待機中に一度だけ発火する。
    bool updateIdleTimer(bool eligible, float dt)
    {
        if (!eligible)
        {
            m_idleSeconds = 0.0;
            m_idleTriggered = false;
            return false;
        }

        if (m_idleTriggered)
            return false;

        m_idleSeconds += dt;
        if (m_idleSeconds + 0.0001 < 10.0)
            return false;

        m_idleTriggered = true;
        return true;
    }

private:
    static constexpr std::size_t index(PlayerActionKind kind)
    {
        return static_cast<std::size_t>(kind);
    }

    std::array<Slot, static_cast<std::size_t>(PlayerActionKind::count)> m_slots{};
    std::array<Slot, AttackCombo::stageCount> m_attackSlots{};
    double m_idleSeconds = 0.0;
    bool m_idleTriggered = false;
};
