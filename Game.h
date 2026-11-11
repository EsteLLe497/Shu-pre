#pragma once
#include "Scene.h"
class Game : public Scene
{
public:
	void Init() override;
	void Uninit() override;
	void Update() override;
	void Draw() override;
};

