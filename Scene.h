#pragma once
class Scene
{
public:
	virtual void Init() {};
	virtual void Uninit() {};
	virtual void Update() {};
	virtual void Draw() {};
};
