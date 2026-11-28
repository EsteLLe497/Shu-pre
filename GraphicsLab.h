#pragma once
#include <memory>
#include "Scene.h"

class GraphicsLab final : public Scene
{
public:
    GraphicsLab();
    ~GraphicsLab() override;
    void Init() override;
    void Draw() override;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
