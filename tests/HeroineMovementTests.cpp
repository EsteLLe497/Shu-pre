#include "../HeroineMovement.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    int checks = 0;
    void check(bool condition, const char* message)
    {
        ++checks;
        if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
    }
    bool flat(Vector3&) { return true; }
}

int main()
{
    using State = HeroineMovement::State;
    const Vector3 origin{ 0, 0, 0 }, target{ 3, 0, 4 };
    HeroineMovement movement;
    Vector3 position = origin;
    check(movement.begin(origin, target, 1.0f), "valid request");
    for (int i = 0; i < 600; ++i) movement.update(position, 1.0f / 60.0f, flat);
    check(movement.state() == State::arrived && movement.remaining(position) <= 0.05f, "arrive within radius");
    check(position.x <= target.x && position.z <= target.z, "no overshoot");
    check(movement.completedLegs() == 1, "arrival counted once");
    const Vector3 arrived = position;
    movement.update(position, 0.1f, flat);
    check(position == arrived && movement.completedLegs() == 1, "arrived stays stopped");

    Vector3 at30 = origin, at60 = origin;
    HeroineMovement a, b;
    a.begin(origin, target, 1); b.begin(origin, target, 1);
    for (int i = 0; i < 30; ++i) a.update(at30, 1.0f / 30.0f, flat);
    for (int i = 0; i < 60; ++i) b.update(at60, 1.0f / 60.0f, flat);
    check((at30 - at60).lenght() < 0.0001f && fabsf(at30.lenght() - 1.0f) < 0.0001f, "frame-rate independent travel");

    position = origin; movement.begin(origin, target, 2);
    movement.setPaused(true); movement.update(position, 0.1f, flat);
    check(position == origin && movement.state() == State::paused, "pause keeps position");
    movement.setPaused(false); movement.update(position, 0.1f, flat);
    check(position.lenght() > 0 && movement.state() == State::moving, "resume request");
    movement.stop(); const Vector3 stopped = position; movement.update(position, 0.1f, flat);
    check(position == stopped && movement.state() == State::idle, "stop cancels request");

    position = origin; movement.begin(origin, { 10, 0, 0 }, 5);
    int substeps = 0;
    movement.update(position, 0.1f, [&](Vector3& next) { ++substeps; return next.x < 0.25f; });
    check(movement.state() == State::blocked && position.x < 0.25f && substeps > 1, "thin obstacle blocks substeps");
    const Vector3 blocked = position; movement.update(position, 0.1f, flat);
    check(position == blocked, "blocked does not retry automatically");

    position = origin; movement.begin(origin, { 0, 0, 1 }, 1, 0.01f, true);
    for (int i = 0; i < 260; ++i) movement.update(position, 1.0f / 60.0f, flat);
    check(movement.completedLegs() >= 4 && position.z >= 0 && position.z <= 1, "repeat reverses without teleport");
    movement.stop(); const Vector3 repeated = position;
    for (int i = 0; i < 20; ++i) movement.update(position, 0.1f, flat);
    check(position == repeated, "stop cancels repeat");

    check(movement.begin(origin, origin, 1) && movement.state() == State::arrived, "same point completes immediately");
    check(!movement.begin(origin, origin, 1, 0.05f, true), "same point cannot repeat");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    check(!movement.begin({ nan, 0, 0 }, target, 1), "reject NaN start");
    check(!movement.begin(origin, { 0, infinity, 0 }, 1), "reject infinite target");
    check(!movement.begin(origin, target, 0) && !movement.begin(origin, target, 6), "bound speed");
    check(!movement.begin(origin, target, nan) && !movement.begin(origin, target, 1, nan), "reject NaN parameters");
    check(!movement.begin(origin, target, 1, 0) && !movement.begin(origin, target, 1, 2), "bound arrival radius");
    position = origin; movement.begin(origin, target, 1);
    movement.update(position, nan, flat); movement.update(position, -1, flat); movement.update(position, 0, flat);
    check(position == origin, "invalid delta time does not move");
    movement.update(position, 100, flat);
    check(position.lenght() <= 0.10001f, "long frame is bounded");
    position = origin;
    movement.begin(origin, target, 1);
    movement.update(position, 0.1f, [&](Vector3& next) { next.y = nan; return true; });
    check(position == origin && movement.state() == State::blocked, "reject corrupt collision output");
    std::printf("HeroineMovement: %d checks passed\n", checks);
}
