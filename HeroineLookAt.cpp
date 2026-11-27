#include "HeroineLookAt.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <string>

constexpr float HeroineLookAtSettings::maxEyeYawDegrees;
constexpr float HeroineLookAtSettings::maxEyePitchDegrees;

namespace
{
    constexpr float radians = 0.017453292519943295f;
    bool range(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
    float clamp(float value, float limit) { return std::max(-limit, std::min(limit, value)); }
    void limitEyes(float& yaw, float& pitch, float yawLimit, float pitchLimit)
    {
        yaw = clamp(yaw, yawLimit);
        pitch = clamp(pitch, pitchLimit);
        const float x = yawLimit > 0 ? yaw / yawLimit : 0;
        const float y = pitchLimit > 0 ? pitch / pitchLimit : 0;
        const float length = sqrtf(x * x + y * y);
        if (length > 1) { yaw /= length; pitch /= length; }
    }
    float outside(float value, float deadZone)
    {
        return value > deadZone ? value - deadZone : value < -deadZone ? value + deadZone : 0.0f;
    }
    float smooth(float current, float target, float response, float dt)
    {
        return current + (target - current) * (1.0f - expf(-response * dt));
    }
    float fade(float value, float start, float end)
    {
        if (value <= start) return 1.0f;
        if (value >= end) return 0.0f;
        const float t = (end - value) / (end - start);
        return t * t * (3.0f - 2.0f * t);
    }
}

bool HeroineLookAtSettings::valid() const
{
    return range(headYawDegrees, 0, 35) && range(headPitchDegrees, 0, 20)
        && range(eyeYawDegrees, 0, maxEyeYawDegrees) && range(eyePitchDegrees, 0, maxEyePitchDegrees)
        && range(eyeOnlyYawDegrees, 0, eyeYawDegrees) && range(eyeOnlyPitchDegrees, 0, eyePitchDegrees)
        && range(viewYawDegrees, 20, 85) && range(nearDistance, 0.1f, 2)
        && range(maxDistance, nearDistance + 0.5f, 30)
        && range(headResponse, 1, 12) && range(eyeResponse, 2, 30) && range(neckWeight, 0, 0.5f);
}

void HeroineLookAtSettings::constrainEyes()
{
    eyeYawDegrees = std::min(eyeYawDegrees, maxEyeYawDegrees);
    eyePitchDegrees = std::min(eyePitchDegrees, maxEyePitchDegrees);
    eyeOnlyYawDegrees = std::min(eyeOnlyYawDegrees, eyeYawDegrees);
    eyeOnlyPitchDegrees = std::min(eyeOnlyPitchDegrees, eyePitchDegrees);
}

bool HeroineLookAtSettings::read(std::istream& input)
{
    std::string format;
    int version = 0, on = 0, eyes = 0, head = 0;
    HeroineLookAtSettings candidate;
    if (!(input >> format >> version) || format != "heroineLookAt" || version != 1
        || !(input >> on >> eyes >> head >> candidate.headYawDegrees >> candidate.headPitchDegrees
            >> candidate.eyeYawDegrees >> candidate.eyePitchDegrees >> candidate.eyeOnlyYawDegrees >> candidate.eyeOnlyPitchDegrees
            >> candidate.viewYawDegrees >> candidate.nearDistance >> candidate.maxDistance
            >> candidate.headResponse >> candidate.eyeResponse >> candidate.neckWeight)
        || (on != 0 && on != 1) || (eyes != 0 && eyes != 1) || (head != 0 && head != 1)
        || !range(candidate.eyeYawDegrees, 0, 35) || !range(candidate.eyePitchDegrees, 0, 25)
        || !range(candidate.eyeOnlyYawDegrees, 0, candidate.eyeYawDegrees)
        || !range(candidate.eyeOnlyPitchDegrees, 0, candidate.eyePitchDegrees)
        || !(input >> std::ws).eof()) return false;
    // Version 1 presets may contain the former, wider eye ranges.
    candidate.constrainEyes();
    if (!candidate.valid()) return false;
    candidate.enabled = on != 0;
    candidate.eyesEnabled = eyes != 0;
    candidate.headEnabled = head != 0;
    *this = candidate;
    return true;
}

void HeroineLookAtSettings::write(std::ostream& output) const
{
    output << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "heroineLookAt 1\n" << enabled << ' ' << eyesEnabled << ' ' << headEnabled << '\n'
        << headYawDegrees << ' ' << headPitchDegrees << ' ' << eyeYawDegrees << ' ' << eyePitchDegrees << '\n'
        << eyeOnlyYawDegrees << ' ' << eyeOnlyPitchDegrees << ' ' << viewYawDegrees << '\n'
        << nearDistance << ' ' << maxDistance << ' ' << headResponse << ' ' << eyeResponse << ' ' << neckWeight << '\n';
}

void HeroineLookAt::reset()
{
    m_headYaw = m_headPitch = m_eyeYaw = m_eyePitch = 0;
    m_tracking = false;
}

void HeroineLookAt::update(float yaw, float pitch, float distance, bool targetAvailable, float dt)
{
    if (!settings.valid()) { reset(); return; }
    if (!std::isfinite(dt) || dt <= 0) return;
    dt = std::min(dt, 0.1f);
    const float headYawLimit = settings.headEnabled ? settings.headYawDegrees * radians : 0;
    const float headPitchLimit = settings.headEnabled ? settings.headPitchDegrees * radians : 0;
    const float eyeYawLimit = settings.eyesEnabled ? settings.eyeYawDegrees * radians : 0;
    const float eyePitchLimit = settings.eyesEnabled ? settings.eyePitchDegrees * radians : 0;
    float weight = 0;
    if (settings.enabled && targetAvailable && std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(distance)
        && distance >= settings.nearDistance)
    {
        const float yawRange = std::min(settings.viewYawDegrees * radians, headYawLimit + eyeYawLimit + 10 * radians);
        const float pitchRange = std::min(70 * radians, headPitchLimit + eyePitchLimit + 10 * radians);
        weight = fade(fabsf(yaw), std::max(0.0f, yawRange - 10 * radians), yawRange)
            * fade(fabsf(pitch), std::max(0.0f, pitchRange - 10 * radians), pitchRange)
            * fade(distance, settings.maxDistance * 0.8f, settings.maxDistance);
    }
    m_tracking = weight > 0;
    const float desiredYaw = weight > 0 ? yaw * weight : 0;
    const float desiredPitch = weight > 0 ? pitch * weight : 0;
    const float headYaw = clamp(outside(desiredYaw, settings.eyesEnabled ? settings.eyeOnlyYawDegrees * radians : 0), headYawLimit);
    const float headPitch = clamp(outside(desiredPitch, settings.eyesEnabled ? settings.eyeOnlyPitchDegrees * radians : 0), headPitchLimit);
    m_headYaw = clamp(smooth(m_headYaw, headYaw, settings.headResponse, dt), headYawLimit);
    m_headPitch = clamp(smooth(m_headPitch, headPitch, settings.headResponse, dt), headPitchLimit);
    // Eyes compensate for the current (slower) head angle, rather than its eventual goal.
    float eyeYaw = desiredYaw - m_headYaw, eyePitch = desiredPitch - m_headPitch;
    limitEyes(eyeYaw, eyePitch, eyeYawLimit, eyePitchLimit);
    m_eyeYaw = smooth(m_eyeYaw, eyeYaw, settings.eyeResponse, dt);
    m_eyePitch = smooth(m_eyePitch, eyePitch, settings.eyeResponse, dt);
    // An elliptical cone also bounds diagonal gaze, where two axis maxima look extreme.
    limitEyes(m_eyeYaw, m_eyePitch, eyeYawLimit, eyePitchLimit);
}
