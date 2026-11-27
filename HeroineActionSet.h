#pragma once

#include "ActionSequence.h"
#include <array>

enum class HeroineActionKind { idle, walk, run, yawn, count };

class HeroineActionSet
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
    Slot& get(HeroineActionKind kind) { return m_slots[static_cast<size_t>(kind)]; }
    const Slot& get(HeroineActionKind kind) const { return m_slots[static_cast<size_t>(kind)]; }

private:
    std::array<Slot, static_cast<size_t>(HeroineActionKind::count)> m_slots{};
};
