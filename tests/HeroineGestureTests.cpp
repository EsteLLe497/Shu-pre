#include "../HeroineGesture.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    constexpr float rad = 0.017453292519943295f;
    int checks = 0;
    void check(bool ok, const char* message) { ++checks; if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
    void finish(HeroineGesture& gesture) { for (int i = 0; i < 200; ++i) gesture.update(1.0f / 60); }
}

int main()
{
    HeroineGesture gesture;
    std::ifstream input("asset/ai/heroine-gestures.cfg");
    check(gesture.settings.read(input), "shipped settings load");
    check(!gesture.active(), "idle has no extra motion");
    gesture.playDialogue(HeroineGestureKind::nod);
    check(gesture.offset().pitch == 0, "nod starts at neutral");
    for (int i = 0; i < 36; ++i) gesture.update(1.0f / 60);
    check(gesture.offset().pitch < -5.9f * rad && gesture.offset().roll == 0, "nod bends down at midpoint");
    auto pose = gesture.compose(0, -8 * rad, 20 * rad, 8 * rad);
    check(std::fabs(pose.pitch + 8 * rad) < 0.00001f, "combined gaze and nod obey pitch limit");
    const auto previous = gesture.offset();
    gesture.stopDialogue();
    check(gesture.offset().pitch == previous.pitch, "cancellation does not snap");
    for (int i = 0; i < 30; ++i) { gesture.stopDialogue(); gesture.update(1.0f / 60); }
    check(!gesture.active() && gesture.offset().pitch == 0, "repeated stop calls finish release");
    gesture.preview(HeroineGestureKind::tilt);
    check(!gesture.playDialogue(HeroineGestureKind::nod), "manual preview wins over AI");
    for (int i = 0; i < 54; ++i) gesture.update(1.0f / 60);
    check(gesture.offset().roll > 7.9f * rad && gesture.offset().pitch == 0, "tilt uses roll only");
    const auto frozen = gesture.offset();
    gesture.update(0); gesture.update(-1); gesture.update(std::numeric_limits<float>::quiet_NaN());
    check(gesture.offset().roll == frozen.roll, "invalid and zero time do not advance");
    gesture.preview(HeroineGestureKind::nod);
    check(gesture.offset().roll == frozen.roll, "retrigger starts from current pose");
    finish(gesture);
    check(!gesture.active() && gesture.offset().roll == 0 && gesture.offset().pitch == 0, "one shot ends exactly neutral");
    check(gesture.previewing(), "manual priority remains until explicitly released");
    gesture.useConversation();
    check(!gesture.previewing() && gesture.playDialogue(HeroineGestureKind::nod), "automatic mode can be restored");
    gesture.update(0.3f);
    check(gesture.progress() < 0.09f, "large frame times bounded to avoid pose jumps");
    gesture.enabled = false; finish(gesture);
    check(!gesture.active() && !gesture.playDialogue(HeroineGestureKind::tilt), "disabled automatic gestures stop and reject new play");
    gesture.preview(HeroineGestureKind::tilt); gesture.update(0.1f);
    check(gesture.active(), "manual preview works with automatic disabled");
    gesture.reset();
    check(!gesture.active() && !gesture.previewing() && gesture.offset().roll == 0, "model replacement clears procedural pose");
    auto unchanged = gesture.compose(30 * rad, 18 * rad, 35 * rad, 20 * rad);
    check(std::fabs(unchanged.yaw - 30 * rad) < 0.00001f && std::fabs(unchanged.pitch - 18 * rad) < 0.00001f, "idle preserves existing gaze tuning");
    gesture.preview(HeroineGestureKind::tilt);
    for (int i = 0; i < 200; ++i)
    {
        gesture.update(1.0f / 60);
        auto pose = gesture.compose(100 * rad, -100 * rad, 35 * rad, 20 * rad);
        check(std::fabs(pose.yaw) <= 35 * rad + 0.00001f && std::fabs(pose.pitch) <= 20 * rad + 0.00001f
            && std::fabs(pose.roll) <= 10 * rad + 0.00001f
            && std::sqrt(pose.yaw * pose.yaw + pose.pitch * pose.pitch + pose.roll * pose.roll) <= HeroineGesture::maxCombinedDegrees * rad, "combined correction remains bounded");
    }
    check(gesture.compose(std::numeric_limits<float>::infinity(), 0, 1, 1).yaw == 0, "invalid compose input rejected");
    std::ostringstream saved; gesture.settings.write(saved);
    HeroineGesture::Settings loaded; std::istringstream roundTrip(saved.str());
    check(loaded.read(roundTrip) && loaded.tiltDegrees == gesture.settings.tiltDegrees, "settings round trip");
    for (const char* invalid : { "heroineGestures 1 99 8 1 2", "heroineGestures 1 6 8 0 2", "heroineGestures 2 6 8 1 2", "heroineGestures 1 6 8 1 2 extra" })
    {
        std::istringstream bad(invalid);
        check(!loaded.read(bad) && loaded.nodDegrees == 6, "invalid settings rejected atomically");
    }
    HeroineGestureKind kind = HeroineGestureKind::none;
    check(parseGesture("nod", kind) && kind == HeroineGestureKind::nod, "nod name parsed");
    kind = HeroineGestureKind::none;
    check(!parseGesture("run_script", kind) && kind == HeroineGestureKind::none, "unknown action is not a gesture");
    std::cout << checks << " gesture checks passed\n";
}
