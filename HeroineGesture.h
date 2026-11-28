#pragma once
#include <iosfwd>
#include <string>

enum class HeroineGestureKind { none, nod, tilt, count };

inline const char* gestureName(HeroineGestureKind kind)
{
    return kind == HeroineGestureKind::nod ? "nod" : kind == HeroineGestureKind::tilt ? "tilt" : "none";
}

inline bool parseGesture(const std::string& name, HeroineGestureKind& kind)
{
    for (int i = 0; i < static_cast<int>(HeroineGestureKind::count); ++i)
        if (name == gestureName(static_cast<HeroineGestureKind>(i))) { kind = static_cast<HeroineGestureKind>(i); return true; }
    return false;
}

// Procedural offsets only; no renderer, game object, or network dependencies.
class HeroineGesture
{
public:
    struct Pose { float yaw = 0; float pitch = 0; float roll = 0; }; // Radians.
    struct Settings
    {
        float nodDegrees = 6;
        float tiltDegrees = 8;
        float nodSeconds = 1.2f;
        float tiltSeconds = 1.8f;
        bool valid() const;
        bool read(std::istream& input);
        void write(std::ostream& output) const;
    } settings;
    static constexpr float maxCombinedDegrees = 42;
    bool enabled = true;
    bool playDialogue(HeroineGestureKind kind);
    void preview(HeroineGestureKind kind);
    void stopDialogue();
    void stop();
    void useConversation();
    void reset();
    void update(float dt);
    Pose compose(float lookYaw, float lookPitch, float yawLimit, float pitchLimit) const;
    Pose offset() const { return m_pose; }
    bool active() const { return m_kind != HeroineGestureKind::none || m_releasing; }
    bool previewing() const { return m_manual; }
    HeroineGestureKind kind() const { return m_kind; }
    float progress() const;

private:
    void begin(HeroineGestureKind kind);
    HeroineGestureKind m_kind = HeroineGestureKind::none;
    Pose m_pose, m_start;
    float m_time = 0;
    float m_duration = 1;
    float m_amplitude = 0;
    bool m_manual = false;
    bool m_releasing = false;
};
