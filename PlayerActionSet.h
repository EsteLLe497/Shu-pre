// =========================================================
// ファイルの情報[PlayerActionSet.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include "ActionSequence.h"
#include <array>

enum class PlayerActionKind { idle, walk, run, yawn, attack, jump, runJump, land, count };

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
    double m_idleSeconds = 0.0;
    bool m_idleTriggered = false;
};
