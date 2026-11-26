#pragma once
#include "Scene.h"
#include <utility>

class Player;
class MeshField;
class Game : public Scene
{
public:
    static std::pair<Player*, MeshField*> createWorld();
	void Init() override;
	void Uninit() override;
	void Update() override;
	void Draw() override;
};

