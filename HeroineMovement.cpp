#include "HeroineMovement.h"
#include <algorithm>
#include <cmath>

bool HeroineMovement::validPosition(const Vector3& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
        && fabsf(position.x) <= 100000.0f && fabsf(position.y) <= 100000.0f && fabsf(position.z) <= 100000.0f;
}

bool HeroineMovement::begin(const Vector3& start, const Vector3& target, float speed, float arrivalRadius, bool repeat)
{
    if (!validPosition(start) || !validPosition(target) || !std::isfinite(speed) || speed < 0.1f || speed > 5.0f
        || !std::isfinite(arrivalRadius) || arrivalRadius < 0.01f || arrivalRadius > 1.0f)
        return false;
    const float length = hypotf(target.x - start.x, target.z - start.z);
    if (repeat && length <= arrivalRadius) return false;
    m_start = start;
    m_target = target;
    m_speed = speed;
    m_arrivalRadius = arrivalRadius;
    m_repeat = repeat;
    m_completedLegs = length <= arrivalRadius ? 1 : 0;
    m_state = length <= arrivalRadius ? State::arrived : State::moving;
    return true;
}

float HeroineMovement::remaining(const Vector3& position) const
{
    return hypotf(m_target.x - position.x, m_target.z - position.z);
}

void HeroineMovement::stop()
{
    m_state = State::idle;
    m_repeat = false;
}

void HeroineMovement::setPaused(bool paused)
{
    if (paused && m_state == State::moving) m_state = State::paused;
    else if (!paused && m_state == State::paused) m_state = State::moving;
}

void HeroineMovement::update(Vector3& position, float dt, const ResolvePosition& resolve)
{
    if (!std::isfinite(dt) || dt <= 0.0f || !validPosition(position)) return;
    if (m_state == State::arrived && m_repeat)
    {
        std::swap(m_start, m_target);
        m_state = State::moving;
    }
    if (m_state != State::moving) return;

    float travel = m_speed * std::min(dt, 0.1f);
    while (travel > 0.0f)
    {
        const float distance = remaining(position);
        if (distance <= m_arrivalRadius)
        {
            m_state = State::arrived;
            ++m_completedLegs;
            break;
        }
        // Small steps keep thin scene colliders from being skipped at low frame rates.
        const float step = std::min(std::min(travel, distance), 0.08f);
        Vector3 next = position + Vector3{ m_target.x - position.x, 0, m_target.z - position.z } * (step / distance);
        if ((resolve && !resolve(next)) || !validPosition(next))
        {
            m_state = State::blocked;
            break;
        }
        position = next;
        travel -= step;
        if (remaining(position) <= m_arrivalRadius)
        {
            m_state = State::arrived;
            ++m_completedLegs;
            break;
        }
    }
}
