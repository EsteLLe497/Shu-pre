#include "HeroineGesture.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>

namespace
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float radians = pi / 180.0f;
    constexpr float fadeSeconds = 0.2f;
    float smooth(float value)
    {
        value = std::max(0.0f, std::min(1.0f, value));
        return value * value * (3.0f - 2.0f * value);
    }
}

bool HeroineGesture::Settings::valid() const
{
    return std::isfinite(nodDegrees) && nodDegrees >= 0 && nodDegrees <= 12
        && std::isfinite(tiltDegrees) && tiltDegrees >= 0 && tiltDegrees <= 10
        && std::isfinite(nodSeconds) && nodSeconds >= 0.5f && nodSeconds <= 3
        && std::isfinite(tiltSeconds) && tiltSeconds >= 0.5f && tiltSeconds <= 3;
}

bool HeroineGesture::Settings::read(std::istream& input)
{
    auto candidate = *this;
    std::string format;
    int version = 0;
    if (!(input >> format >> version >> candidate.nodDegrees >> candidate.tiltDegrees >> candidate.nodSeconds >> candidate.tiltSeconds)
        || format != "heroineGestures" || version != 1 || !candidate.valid() || !(input >> std::ws).eof()) return false;
    *this = candidate;
    return true;
}

void HeroineGesture::Settings::write(std::ostream& output) const
{
    output << "heroineGestures 1\n" << std::setprecision(9) << nodDegrees << ' ' << tiltDegrees << ' ' << nodSeconds << ' ' << tiltSeconds << '\n';
}

void HeroineGesture::begin(HeroineGestureKind kind)
{
    if (kind <= HeroineGestureKind::none || kind >= HeroineGestureKind::count || !settings.valid()) { stop(); return; }
    m_start = m_pose;
    m_kind = kind;
    m_time = 0;
    m_duration = kind == HeroineGestureKind::nod ? settings.nodSeconds : settings.tiltSeconds;
    m_amplitude = (kind == HeroineGestureKind::nod ? settings.nodDegrees : settings.tiltDegrees) * radians;
    m_releasing = false;
}

bool HeroineGesture::playDialogue(HeroineGestureKind kind)
{
    if (!enabled || m_manual) return false;
    begin(kind);
    return true;
}

void HeroineGesture::preview(HeroineGestureKind kind)
{
    m_manual = true;
    begin(kind);
}

void HeroineGesture::stopDialogue() { if (!m_manual) stop(); }

void HeroineGesture::stop()
{
    if (m_releasing || !active()) return;
    m_start = m_pose;
    m_kind = HeroineGestureKind::none;
    m_time = 0;
    m_duration = fadeSeconds;
    m_releasing = true;
}

void HeroineGesture::useConversation() { m_manual = false; stop(); }

void HeroineGesture::reset()
{
    m_kind = HeroineGestureKind::none;
    m_pose = m_start = {};
    m_time = 0;
    m_manual = m_releasing = false;
}

float HeroineGesture::progress() const { return active() ? std::min(1.0f, m_time / m_duration) : 0; }

void HeroineGesture::update(float dt)
{
    if (!std::isfinite(dt) || dt <= 0) return;
    if (!enabled && !m_manual) stop();
    if (!active()) return;
    m_time = std::min(m_duration, m_time + std::min(0.1f, dt));
    if (m_time >= m_duration)
    {
        m_kind = HeroineGestureKind::none;
        m_pose = {};
        m_releasing = false;
        return;
    }
    const float blend = smooth(m_time / fadeSeconds);
    Pose target;
    if (!m_releasing)
    {
        const float envelope = std::sin(pi * m_time / m_duration);
        if (m_kind == HeroineGestureKind::nod) target.pitch = -m_amplitude * envelope * envelope;
        else target.roll = m_amplitude * envelope * envelope;
    }
    m_pose.pitch = m_start.pitch + (target.pitch - m_start.pitch) * blend;
    m_pose.roll = m_start.roll + (target.roll - m_start.roll) * blend;
}

HeroineGesture::Pose HeroineGesture::compose(float lookYaw, float lookPitch, float yawLimit, float pitchLimit) const
{
    if (!std::isfinite(lookYaw) || !std::isfinite(lookPitch) || !std::isfinite(yawLimit) || !std::isfinite(pitchLimit)) return {};
    yawLimit = std::max(0.0f, std::min(35.0f * radians, yawLimit));
    pitchLimit = std::max(0.0f, std::min(20.0f * radians, pitchLimit));
    Pose result;
    result.yaw = std::max(-yawLimit, std::min(yawLimit, lookYaw));
    result.pitch = std::max(-pitchLimit, std::min(pitchLimit, lookPitch + m_pose.pitch));
    result.roll = std::max(-10.0f * radians, std::min(10.0f * radians, m_pose.roll));
    const float length = std::sqrt(result.yaw * result.yaw + result.pitch * result.pitch + result.roll * result.roll);
    const float scale = length > maxCombinedDegrees * radians ? maxCombinedDegrees * radians / length : 1;
    result.yaw *= scale; result.pitch *= scale; result.roll *= scale;
    return result;
}
