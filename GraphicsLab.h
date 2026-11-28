// =========================================================
// ファイルの情報[GraphicsLab.h]
//
// 制作者:ozawa souma        日付:2026/11/28
// =========================================================

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
