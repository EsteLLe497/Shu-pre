#pragma once

#include "GameObject.h"

// The player owns movement; the visible character is Heroine.
class Player : public GameObject
{
private:
    Vector3 m_Velocity{ 0, 0, 0 };
    bool m_Ground = false;

public:
    static constexpr float BodyRadius = 0.52f;
    static constexpr float BodyLowerCenter = 0.57f;
    static constexpr float BodyUpperCenter = 1.24f;

    void Init() override;
    void Update() override;
    void Draw() override {};
};
