#pragma once

#include "Vector3.h"
#include <functional>

// World collision and animation stay with Heroine; this class owns the move request.
class HeroineMovement
{
public:
    enum class State { idle, moving, paused, arrived, blocked };
    using ResolvePosition = std::function<bool(Vector3&)>;

    bool begin(const Vector3& start, const Vector3& target, float speed,
        float arrivalRadius = 0.05f, bool repeat = false);
    void update(Vector3& position, float dt, const ResolvePosition& resolve);
    void stop();
    void setPaused(bool paused);
    State state() const { return m_state; }
    const Vector3& target() const { return m_target; }
    float remaining(const Vector3& position) const;
    unsigned int completedLegs() const { return m_completedLegs; }
    static bool validPosition(const Vector3& position);

private:
    Vector3 m_start{ 0, 0, 0 };
    Vector3 m_target{ 0, 0, 0 };
    float m_speed = 1.2f;
    float m_arrivalRadius = 0.05f;
    bool m_repeat = false;
    unsigned int m_completedLegs = 0;
    State m_state = State::idle;
};
