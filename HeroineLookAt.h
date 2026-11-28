#pragma once

#include <iosfwd>

struct HeroineLookAtSettings
{
    static constexpr float maxEyeYawDegrees = 12.0f;
    static constexpr float maxEyePitchDegrees = 6.0f;
    bool enabled = true;
    bool eyesEnabled = true;
    bool headEnabled = true;
    float headYawDegrees = 20.0f;
    float headPitchDegrees = 8.0f;
    float eyeYawDegrees = 8.0f;
    float eyePitchDegrees = 4.0f;
    float eyeOnlyYawDegrees = 6.0f;
    float eyeOnlyPitchDegrees = 2.0f;
    float viewYawDegrees = 65.0f;
    float nearDistance = 0.3f; // Start closing the eyes, rather than releasing gaze.
    bool closeEyesWhenNear = true;
    float fullyClosedDistance = 0.08f;
    float eyelidResponse = 12.0f;
    float maxDistance = 6.0f;
    float headResponse = 4.0f;
    float eyeResponse = 18.0f;
    float neckWeight = 0.35f;

    bool valid() const;
    void constrainEyes();
    bool read(std::istream& input);
    void write(std::ostream& output) const;
};

// Angular policy only. The model adapter handles each bone's different local axes.
class HeroineLookAt
{
public:
    HeroineLookAtSettings settings;
    void update(float yaw, float pitch, float distance, bool targetAvailable, float dt, float proximityDistance = -1.0f);
    void reset();
    float headYaw() const { return m_headYaw; }
    float headPitch() const { return m_headPitch; }
    float eyeYaw() const { return m_eyeYaw; }
    float eyePitch() const { return m_eyePitch; }
    bool tracking() const { return m_tracking; }
    float eyeClosure() const { return m_eyeClosure; }

private:
    float m_headYaw = 0.0f;
    float m_headPitch = 0.0f;
    float m_eyeYaw = 0.0f;
    float m_eyePitch = 0.0f;
    bool m_tracking = false;
    float m_eyeClosure = 0.0f;
};
